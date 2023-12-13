#include "gui.h"

#include "ui_gui.h"
#include "util.h"
#include "file_dialog.h"
#include "firmware_ops.h"
#include "git_version.h"

#include <mvp_advanced_widget.h>

#include <QCheckBox>
#include <QComboBox>
#include <QCloseEvent>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QGroupBox>
#include <QLabel>
#include <QMessageBox>
#include <QMetaObject>
#include <QProgressBar>
#include <QSerialPort>
#include <QSignalBlocker>
#include <QtConcurrent>
#include <QtDebug>
#include <QThread>
#include <QPushButton>

#include <utility>

#include "mdpp16.h"

#include <mvlc_connect_widget.h>
#include <serial_port_connect_widget.h>

namespace mesytec
{
namespace mvp
{

MVPLabGui::MVPLabGui(QWidget *parent)
  : QMainWindow(parent)
  , ui(new Ui::MVPLabGui)
  , m_object_holder(new QObject)
  , serialPortConnector_(new SerialPortMvpConnector(m_object_holder))
  , mvlcConnector_(new MvlcMvpConnector(m_object_holder))
  , tabs_connectors_(new QTabWidget)
  , firmwareSelectWidget_(new FirmwareSelectionWidget)
  , m_advancedwidget(new MvpAdvancedWidget)
  , m_progressbar(new QProgressBar)
{
  ui->setupUi(this);

  m_object_holder->setObjectName("object holder");
  m_advancedwidget->setObjectName("advanced");

  // connector uis
  auto serialConnectWidget = new SerialPortConnectWidget;
  auto mvlcConnectWidget = new MvlcConnectWidget;
  tabs_connectors_->addTab(serialConnectWidget, "Onboard USB");
  tabs_connectors_->addTab(mvlcConnectWidget, "MVLC VME");
  auto gb_connectors = new QGroupBox("MVP Connection");
  auto gb_connectors_layout = new QHBoxLayout(gb_connectors);
  gb_connectors_layout->setContentsMargins(0, 0, 0, 0);
  gb_connectors_layout->addWidget(tabs_connectors_);

  connect(tabs_connectors_, &QTabWidget::currentChanged,
    this, &MVPLabGui::onActiveConnectorChanged);

  // serial port interactions
  auto portHelper = serialPortConnector_->getPortHelper();
  assert(portHelper);

  connect(serialConnectWidget, &SerialPortConnectWidget::serialPortRefreshRequested,
    portHelper, &PortHelper::refresh);

  connect(portHelper, &PortHelper::available_ports_changed,
    serialConnectWidget, &SerialPortConnectWidget::setAvailablePorts);

  connect(serialConnectWidget, &SerialPortConnectWidget::serialPortChanged,
    this, [this] (const QString &port) {
       QVariantMap m;
       m["serialport"] = port;
       serialPortConnector_->setConnectInfo(m);
    });

  // mvlc interactions
  connect(mvlcConnectWidget, &MvlcConnectWidget::mvlcConnectInfoChanged,
    mvlcConnector_, &MvlcMvpConnector::setConnectInfo);

  connect(mvlcConnectWidget, &MvlcConnectWidget::usbRefreshRequested,
    mvlcConnector_, &MvlcMvpConnector::refreshUsbDevices);

  connect(mvlcConnectWidget, &MvlcConnectWidget::connectMvlc,
    this, &MVPLabGui::mvlc_connect);

  connect(mvlcConnectWidget, &MvlcConnectWidget::scanbusRequested,
    this, &MVPLabGui::mvlc_scanbus);

  connect(mvlcConnector_, &MvlcMvpConnector::usbDevicesChanged,
    mvlcConnectWidget, &MvlcConnectWidget::setUsbDevices);

  connect(mvlcConnector_, &MvlcMvpConnector::scanbusResultReady,
    mvlcConnectWidget, &MvlcConnectWidget::setScanbusResult);

  connect(mvlcConnector_, &MvlcMvpConnector::logMessage,
    this, &MVPLabGui::append_to_log);

  connect(mvlcConnector_, &MvlcMvpConnector::connectedToMVLC,
    mvlcConnectWidget, &MvlcConnectWidget::mvlcSuccessfullyConnected);

  // firmware selection, steps and start button
  auto gb_fwSelect = new QGroupBox("Firmware Programming");
  auto gb_fwSelect_layout = new QVBoxLayout(gb_fwSelect);
  gb_fwSelect_layout->setContentsMargins(0, 0, 0, 0);
  gb_fwSelect_layout->addWidget(firmwareSelectWidget_);

  // Actions common for mvplab and mvp: show device info (without key details,
  // boot into area).
  auto pb_deviceInfo = new QPushButton("Read Device Info");
  auto gb_boot = new QGroupBox;
  auto gb_boot_layout = new QHBoxLayout(gb_boot);
  auto combo_bootArea = new QComboBox;
  for (auto i=0; i<constants::area_count; ++i)
    combo_bootArea->addItem(QSL("%1").arg(i), i);
  auto pb_boot = new QPushButton("Boot Device");
  gb_boot_layout->addWidget(new QLabel("Flash Area"));
  gb_boot_layout->setContentsMargins(2, 2, 2, 2);
  gb_boot_layout->addWidget(combo_bootArea);
  gb_boot_layout->addWidget(pb_boot);

  gb_actions_ = new QGroupBox("Actions");
  auto actions_layout = new QHBoxLayout(gb_actions_);
  actions_layout->setContentsMargins(0, 0, 0, 0);
  actions_layout->addStretch(1);
  actions_layout->addWidget(pb_deviceInfo);
  actions_layout->addWidget(gb_boot);
  actions_layout->addStretch(1);

  auto top_layout = new QGridLayout;
  top_layout->addWidget(gb_connectors, 0, 0);
  top_layout->addWidget(gb_fwSelect, 0, 1);
  top_layout->addWidget(gb_actions_, 1, 1);

  connect(pb_deviceInfo, &QPushButton::clicked,
    this, &MVPLabGui::show_device_info);

  connect(pb_boot, &QPushButton::clicked,
    this, [=] {
      auto area = combo_bootArea->currentData().toUInt();
      this->adv_boot(area);
    });

  // Advanced widget for mvplab, hidden in mvp.
  advanced_widget_gb_ = new QGroupBox("Advanced");
  auto advanced_widget_layout = new QHBoxLayout(advanced_widget_gb_);
  advanced_widget_layout->setContentsMargins(2, 2, 2, 2);
  advanced_widget_layout->addWidget(m_advancedwidget);

  auto layout = qobject_cast<QBoxLayout *>(centralWidget()->layout()); assert(layout);
  int row = 0;
  layout->insertLayout(row++, top_layout);
  layout->insertWidget(row++, advanced_widget_gb_);
  layout->setStretch(row, 1);

  ui->statusbar->addPermanentWidget(m_progressbar);
  ui->statusbar->setSizeGripEnabled(false);
  ui->logview->document()->setMaximumBlockCount(10000);
  auto font = QFont("monospace");
  font.setStyleHint(QFont::Monospace);
  ui->logview->setFont(font);
  ui->logview->setContextMenuPolicy(Qt::CustomContextMenu);

  connect(ui->logview, &QTextBrowser::customContextMenuRequested,
      this, &MVPLabGui::_show_logview_context_menu);

  m_progressbar->setVisible(false);
  m_progressbar->setRange(0, 0);

  connect(&m_fw, SIGNAL(started()), this, SLOT(handle_future_started()));
  connect(&m_fw, SIGNAL(finished()), this, SLOT(handle_future_finished()));

  for (auto connector: getConnectors())
  {
    auto flash = connector->getFlash();

    connect(flash, &FlashInterface::progress_range_changed,
        m_progressbar, &QProgressBar::setRange, Qt::QueuedConnection);

    connect(flash, &FlashInterface::progress_changed,
        m_progressbar, &QProgressBar::setValue, Qt::QueuedConnection);

    connect(flash, &FlashInterface::progress_text_changed, this, [=](const QString &text) {
        append_to_log_queued(text);
        }, Qt::QueuedConnection);

    connect(flash, &FlashInterface::statusbyte_received, this, [=](const uchar &ss) {
      if (!bool(ss & status::inst_success)) {
        append_to_log(QString("statusbyte(bin)=%1, inst_success=%2, area=%3, dipsw=%4")
                      .arg(ss, 0, 2)
                      .arg(bool(ss & status::inst_success))
                      .arg(get_area(ss))
                      .arg(get_dipswitch(ss)));
      }
    }, Qt::QueuedConnection);
  }

  #if 0
    connect(flash, &FlashInterface::instruction_written, this, [=](const QVector<uchar> &data) {
        qDebug() << "instruction written:" << op_to_string(*data.begin()) << "," << format_bytes(data);
        //append_to_log(QString("instruction %1 written")
        //    .arg(op_to_string(*data.begin())
        //      ));
          //.arg(op_to_string.value(*data.begin(), QString::number(*data.begin(), 16))));
    }
    //);
    , Qt::QueuedConnection);

    connect(flash, &FlashInterface::response_read, this, [=](const QVector<uchar> &data) {
        qDebug() << "response read:" << format_bytes(data);
        //append_to_log(QString("instruction %1 written")
        //    .arg(op_to_string(*data.begin())
        //      ));
          //.arg(op_to_string.value(*data.begin(), QString::number(*data.begin(), 16))));
    }
    //);
    , Qt::QueuedConnection);

    connect(flash, &FlashInterface::data_written, this, [=](const QVector<uchar> &data) {
        qDebug() << "data written:" << format_bytes(data);
    }, Qt::QueuedConnection);
  #endif

#if 0
  // Serial port debugging
  connect(m_port_helper, &PortHelper::available_ports_changed,
      this, [=](const PortInfoList &ports) {

        for (const auto &port: ports) {

        auto str = QString("port name=%1, system location=%2, manufacturer=%3, "
            "serial number=%4, product id=%5, vendor id=%6, description=\"%7\"")
          .arg(port.portName())
          .arg(port.systemLocation())
          .arg(port.manufacturer())
          .arg(port.serialNumber())
          .arg(port.productIdentifier())
          .arg(port.vendorIdentifier())
          .arg(port.description())
          ;

        append_to_log(str);
        }
      }
      , Qt::QueuedConnection);
#endif

  connect(firmwareSelectWidget_, SIGNAL(firmware_file_changed(const QString &)),
      this, SLOT(_on_firmware_file_changed(const QString &)));

  connect(firmwareSelectWidget_, SIGNAL(start_button_clicked()),
      this, SLOT(_on_start_button_clicked()));

  // advanced widget
  connect(m_advancedwidget, SIGNAL(sig_dump_to_console()),
      this, SLOT(adv_dump_to_console()));

  connect(m_advancedwidget, SIGNAL(sig_save_to_file(const QString &)),
      this, SLOT(adv_save_to_file(const QString &)));

  connect(m_advancedwidget, SIGNAL(sig_load_from_file(const QString &)),
      this, SLOT(adv_load_from_file(const QString &)));

  connect(m_advancedwidget, SIGNAL(sig_boot(uchar)),
      this, SLOT(adv_boot(uchar)));

  connect(m_advancedwidget, SIGNAL(sig_nop_recovery()),
      this, SLOT(adv_nop_recovery()));

  connect(m_advancedwidget, SIGNAL(sig_erase_section(uchar, uchar)),
      this, SLOT(adv_erase_section(uchar, uchar)));

  connect(m_advancedwidget, SIGNAL(sig_read_hardware_id()),
      this, SLOT(adv_read_hardware_id()));

  connect(m_advancedwidget, SIGNAL(sig_keys_info()),
      this, SLOT(adv_keys_info()));

  connect(m_advancedwidget, SIGNAL(sig_manage_keys()),
      this, SLOT(adv_manage_keys()));

  connect(m_advancedwidget, SIGNAL(sig_mdpp16_cal_dump_to_console()),
      this, SLOT(adv_mdpp16_cal_dump_to_console()));

  connect(m_advancedwidget, SIGNAL(sig_mdpp16_cal_save_to_file(const QString &)),
      this, SLOT(adv_mdpp16_cal_save_to_file(const QString &)));

  connect(m_advancedwidget, SIGNAL(sig_mdpp32_cal_dump_to_console()),
      this, SLOT(adv_mdpp32_cal_dump_to_console()));

  connect(m_advancedwidget, SIGNAL(sig_mdpp32_cal_save_to_file(const QString &)),
      this, SLOT(adv_mdpp32_cal_save_to_file(const QString &)));

  QSettings settings;
  restoreGeometry(settings.value("mainWindowGeometry").toByteArray());
  restoreState(settings.value("mainWindowState").toByteArray());
}

MVPLabGui::~MVPLabGui()
{
  delete m_object_holder;
}

QTextBrowser *MVPLabGui::getLogview()
{
  return ui->logview;
}

void MVPLabGui::_on_start_button_clicked()
{
  write_firmware();
  handle_keys();
}

void MVPLabGui::_on_firmware_file_changed(const QString &filename)
{
  try {
    m_firmware = run_in_thread_wait_in_loop<FirmwareArchive>([&] {

      FirmwareArchive firmware;
      QFileInfo fi(filename);

      if (fi.suffix() == QSL("bin") || fi.suffix() == QSL("key") || fi.suffix() == QSL("hex"))
      {
          firmware = from_single_file(filename);
      }
      else if (fi.isDir())
      {
          firmware = from_dir(filename);
      }
      else
      {
          firmware = from_zip(filename);
      }

      qDebug() << "Firmware object created from" << fi.filePath();

      return firmware;
    }, m_object_holder, m_fw);

    append_to_log(QString("Loaded firmware from %1")
        .arg(m_firmware.get_filename()));

    if (auto areaSpecificParts = m_firmware.get_area_specific_parts();
        !areaSpecificParts.empty())
    {
        append_to_log("Area specific parts:");

        for (const auto &part: areaSpecificParts)
        {
            append_to_log(QString("\tfn=%1, area=%2, sec=%3, sz=%4")
                          .arg(part->get_filename())
                          .arg(part->has_area() ? QString::number(*part->get_area()) : QString("None"))
                          .arg(part->has_section() ? QString::number(*part->get_section()) : QString("None"))
                          .arg(part->get_contents_size())
                         );
        }
    }

    if (auto nonAreaSpecificParts = m_firmware.get_non_area_specific_parts();
        !nonAreaSpecificParts.empty())
    {
        append_to_log(QString("Non-area specific parts:"));

        for (const auto &part: nonAreaSpecificParts)
        {
            append_to_log(QString("\tfn=%1, sec=%3, sz=%4")
                          .arg(part->get_filename())
                          .arg(part->has_section() ? QString::number(*part->get_section()) : QString("None"))
                          .arg(part->get_contents_size())
                         );
        }
    }

    if (auto keyParts = m_firmware.get_key_parts();
        !keyParts.empty())
    {
        append_to_log(QString("Key parts:"));

        for (const auto &part: m_firmware.get_key_parts()) {
            append_to_log(QString("\tfn=%1, sz=%4")
                          .arg(part->get_filename())
                          .arg(part->get_contents_size())
                         );
        }
    }
    firmwareSelectWidget_->set_start_button_enabled(true);
  } catch (const std::exception &e) {
    m_firmware = FirmwareArchive();
    append_to_log(QString(e.what()));
    firmwareSelectWidget_->set_start_button_enabled(false);
  }

  // Enable/disable the area selection combo box based on the contents of the
  // firmware archive.
  // If there is at least one area specific part in the firmware and it does
  // not have the area encoded in its name, then enable the area selection.
  // Otherwise disable it.

  bool enableAreaSelect = false;

  for (const auto &part: m_firmware.get_area_specific_parts())
  {
      if (!part->get_area())
      {
          enableAreaSelect = true;
          break;
      }
  }

  firmwareSelectWidget_->set_area_select_enabled(enableAreaSelect);
}

void MVPLabGui::write_firmware()
{
  if (m_fw.isRunning()) {
    append_to_log("Error: operation in progress");
    return;
  }

  if (m_firmware.is_empty()) {
    append_to_log("Error: no or empty firmware loaded");
    return;
  }

  auto steps = firmwareSelectWidget_->get_firmware_steps();

  if (steps == 0)
  {
    append_to_log("Nothing to do, no steps have been enabled.");
    return;
  }

  const bool do_erase       = steps & FirmwareSteps::Step_Erase;
  const bool do_program     = steps & FirmwareSteps::Step_Program;
  const bool do_verify      = steps & FirmwareSteps::Step_Verify;

  // Device type workarounds for devices where a simple prefix match does not
  // suffice. MDPP-32 contains a '-' in the device type, VMMR8 uses the VMMR16
  // firmware.
  static const QMap<QString, QString> DeviceTypeTranslate =
  {
    // OTP device type -> device type for matching against firmware filenames
    //                    (not the package filename but the .bin filename!)
    { "MDPP-32",  "MDPP32" },
    { "VMMR8",    "VMMR16" },
    { "MCPD8",    "MCPD-8" },
  };

  try {
    auto otp = run_in_thread_wait_in_loop<OTP>([&] {
      auto connector = getActiveConnector();
      connector->open();
      auto flash = connector->getFlash();
      return flash->read_otp();
    }, m_object_holder, m_fw);

    auto deviceType = otp.get_device().trimmed(); // e.g. "MDPP-32"
    deviceType = DeviceTypeTranslate.value(deviceType, deviceType); // translated, e.g. MDPP32

    for (const auto &part: m_firmware.get_area_specific_parts())
    {
      if (!is_binary_part(part) || !part->has_base())
        continue;

        auto partBase = part->get_base();
        if (!partBase.startsWith(deviceType)) // prefix match of the part base against the translated device type
        {
          append_to_log(QSL("Firmware '%1' does not match current device type '%2'! Aborting.")
            .arg(partBase).arg(deviceType));
          return;
        }
    }
  } catch (const std::exception &e) {
    append_to_log(QString(e.what()));
    return;
  }

  auto area_index = firmwareSelectWidget_->get_area_index();

  try {
    run_in_thread_wait_in_loop([&] {
      auto connector = getActiveConnector();
      connector->open();
      auto flash = connector->getFlash();

      qDebug() << "Firmware: ensure clean state";
      flash->ensure_clean_state();

      qDebug() << "Firmware: set area index" << area_index;
      flash->set_area_index(area_index);

      FirmwareWriter fw_writer(m_firmware, flash);

      fw_writer.set_do_erase(do_erase);
      fw_writer.set_do_program(do_program);
      fw_writer.set_do_verify(do_verify);

      connect(&fw_writer, SIGNAL(status_message(const QString &)),
          this, SLOT(append_to_log(const QString &)),
          Qt::QueuedConnection);

      fw_writer.write();

    }, m_object_holder, m_fw);

    auto flash = getActiveConnector()->getFlash();
    auto ss = flash->get_last_status();
    auto dips = get_dipswitch(ss);

    append_to_log(
          QString("Processed firmware from %1.")
          .arg(firmwareSelectWidget_->get_firmware_file()));

    append_to_log(
          QString("Boot area on power cycle is %1 (dipswitches).\n")
          .arg(dips));

  } catch (const FlashInstructionError &e) {
    append_to_log(e.to_string());
  } catch (const FlashVerificationError &e) {
    append_to_log(e.to_string());
  } catch (const std::exception &e) {
    append_to_log(QString(e.what()));
  }
}

void MVPLabGui::handle_keys()
{
  if (m_fw.isRunning()) {
    append_to_log("Error: operation in progress");
    return;
  }

  try {
    auto fw_keys = m_firmware.get_key_parts();

    if (fw_keys.isEmpty())
      return;

    auto flash = getActiveConnector()->getFlash();

    auto keys_handler = std::unique_ptr<KeysHandler>(
        new KeysHandler(
          m_firmware,
          flash,
          m_object_holder));

    connect(keys_handler.get(), SIGNAL(status_message(const QString &)),
            this, SLOT(append_to_log(const QString &)),
            Qt::QueuedConnection);

    // read key info from device and firmware
    auto keys_info = run_in_thread_wait_in_loop<KeysInfo>([&] {
        auto connector = getActiveConnector();
        connector->open();
        auto flash = connector->getFlash();
        flash->ensure_clean_state();
        return keys_handler->get_keys_info();
      }, m_object_holder, m_fw);

    for (auto key: keys_info.get_mismatched_firmware_keys())
    {
        append_to_log(QString("!!! OTP/Key mismatch detected: %1").arg(key.to_string()));
    }

    auto new_keys = keys_info.get_new_firmware_keys();
    if (!new_keys.isEmpty())
    {

        append_to_log("New keys:");

        for (const auto &key: new_keys)
            append_to_log(QString("  ") + key.to_string());
    }
    else
    {
        append_to_log("No new keys to write");
    }

    // ask the user what to do
    if (keys_info.need_to_erase()) {
      auto answer = QMessageBox::question(
          this,
          "Key limit reached",
          "The device key storage is full. "
          "To write the new keys to the device the current set of keys has to be erased.\n"
          "Do you want to erase the set of device keys and replace them with the firmware keys?",
          QMessageBox::Yes | QMessageBox::Cancel
          );

      if (answer != QMessageBox::Yes)
        return;
    }

    run_in_thread_wait_in_loop([&] {
        auto connector = getActiveConnector();
        connector->open();
        auto flash = connector->getFlash();
        flash->ensure_clean_state();
        keys_handler->write_keys();
      }, m_object_holder, m_fw);

  } catch (const std::exception &e) {
    append_to_log(QString(e.what()));
    return;
  }
}

void MVPLabGui::mvlc_connect()
{
  if (m_fw.isRunning()) {
    append_to_log("Error: operation in progress");
    return;
  }

  try {
    run_in_thread_wait_in_loop([&] {
      mvlcConnector_->open();
    }, m_object_holder, m_fw);
    //append_to_log("Connected to MVLC"); // TODO: somehow get connection info and display it
  } catch (const std::exception &e) {
    append_to_log(QString(e.what()));
    return;
  }
}

void MVPLabGui::mvlc_scanbus()
{
  if (m_fw.isRunning()) {
    append_to_log("Error: operation in progress");
    return;
  }

  try {
    // Side effect is that mvlcConnector_ emits scanbusResultReady(), which is
    // connected to mvlcConnectWidget_::setScanbusResult.
    run_in_thread_wait_in_loop([&] {
      mvlcConnector_->scanbus();
    }, m_object_holder, m_fw);
  } catch (const std::exception &e) {
    append_to_log(QString(e.what()));
    return;
  }
}

void MVPLabGui::onActiveConnectorChanged()
{
  try
  {
    run_in_thread_wait_in_loop([&]
    {
      for (auto &con: getConnectors())
      {
        if (con != getActiveConnector())
          con->close();
      }
    }, m_object_holder, m_fw);
  }
  catch(const std::exception& e)
  {
    append_to_log(tr("Warning: error closing MVP connection: %1").arg(e.what()));
  }
}

void MVPLabGui::closeEvent(QCloseEvent *event)
{
  if (!m_fw.isRunning()) {
    event->accept();
  } else {
    if (!m_quit)
      append_to_log("Quitting after the current operation finishes");

    m_quit = true;
    event->ignore();
  }

  QSettings settings;
  settings.setValue("mainWindowGeometry", saveGeometry());
  settings.setValue("mainWindowState", saveState());
}

void MVPLabGui::append_to_log(const QString &s)
{
  auto str(QString("%1: %2")
      .arg(QDateTime::currentDateTime().toString("HH:mm:ss"))
      .arg(s));

  ui->logview->append(str);
  //qDebug() << s;
}

void MVPLabGui::handle_future_started()
{
  tabs_connectors_->setEnabled(false);
  gb_actions_->setEnabled(false);
  m_advancedwidget->setEnabled(false);
  firmwareSelectWidget_->setEnabled(false);
  m_progressbar->setVisible(true);
}

void MVPLabGui::handle_future_finished()
{
  m_loop.quit();

  tabs_connectors_->setEnabled(true);
  gb_actions_->setEnabled(true);
  m_advancedwidget->setEnabled(true);
  firmwareSelectWidget_->setEnabled(true);
  m_progressbar->setVisible(false);
  m_progressbar->setRange(0, 0);

  if (m_quit)
    close();
}

void MVPLabGui::append_to_log_queued(const QString &s)
{
  QMetaObject::invokeMethod(
        this,
        "append_to_log",
        Qt::QueuedConnection,
        Q_ARG(QString, s));
}

void MVPLabGui::on_actionAbout_triggered()
{
    const auto appName = qApp->applicationName();
    const auto appDisplayName = qApp->applicationDisplayName();
    const auto appVersion = qApp->applicationVersion();

    auto dialog = new QDialog(this);
    dialog->setWindowTitle(QSL("About %1").arg(appName));

    auto tb_license = new QTextBrowser(dialog);
    tb_license->setWindowFlags(Qt::Window);
    tb_license->setWindowTitle(QSL("%1 license").arg(appName));

    {
        QFile licenseFile(":/gpl-notice.txt");
        licenseFile.open(QIODevice::ReadOnly);
        tb_license->setText(licenseFile.readAll());
    }

    auto layout = new QVBoxLayout(dialog);

    {
        auto label = new QLabel;
        label->setPixmap(QPixmap(":/mesytec-logo.png").
                              scaledToWidth(300, Qt::SmoothTransformation));
        layout->addWidget(label);
    }

    layout->addWidget(new QLabel(appDisplayName));
    layout->addWidget(new QLabel(QString("Version %1").arg(appVersion)));
    layout->addWidget(new QLabel(QSL("© 2015-2023 mesytec GmbH & Co. KG")));
    layout->addWidget(new QLabel(QSL("Authors: F. Lüke")));

    {
        QString text(QSL("<a href=\"mailto:info@mesytec.com\">info@mesytec.com</a> - <a href=\"http://www.mesytec.com\">www.mesytec.com</a>"));
        auto label = new QLabel(text);
        label->setOpenExternalLinks(true);
        layout->addWidget(label);
    }

    layout->addSpacing(20);

    auto buttonLayout = new QHBoxLayout;

    {
        auto button = new QPushButton(QSL("&License"));
        connect(button, &QPushButton::clicked, this, [this, tb_license]() {
            auto sz = tb_license->size();
            sz = sz.expandedTo(QSize(500, 300));
            tb_license->resize(sz);
            tb_license->show();
            tb_license->raise();
        });

        buttonLayout->addWidget(button);
    }

    {
        auto button = new QPushButton(QSL("&Close"));
        connect(button, &QPushButton::clicked, dialog, &QDialog::close);
        button->setAutoDefault(true);
        button->setDefault(true);
        buttonLayout->addWidget(button);
    }

    layout->addLayout(buttonLayout);

    for (int i=0; i<layout->count(); ++i)
    {
        auto item = layout->itemAt(i);
        item->setAlignment(Qt::AlignHCenter);

        auto label = qobject_cast<QLabel *>(item->widget());
        if (label)
            label->setTextInteractionFlags(Qt::TextBrowserInteraction);
    }

    dialog->exec();
}

void MVPLabGui::on_actionAbout_Qt_triggered()
{
    QMessageBox::aboutQt(this, "About Qt");
}

void MVPLabGui::on_actionShowAdvanced_toggled(bool checked)
{
  advanced_widget_gb_->setVisible(checked);
}

void MVPLabGui::_show_logview_context_menu(const QPoint &pos)
{
  auto menu = std::unique_ptr<QMenu>(ui->logview->createStandardContextMenu(pos));
  auto action = menu->addAction("Clear");
  connect(action, &QAction::triggered, ui->logview, &QTextBrowser::clear);
  menu->exec(ui->logview->mapToGlobal(pos));
}

// Similar to adv_keys_info() but does not display the actual key. Also shows
// the selected boot area (dip switches).
void MVPLabGui::show_device_info()
{
  try {
    auto keys_info = read_device_keys();
    auto otp = keys_info.get_otp();
    auto devName = otp.get_device().trimmed();

    append_to_log(QString("Device Info: type='<b>%1</b>', serial=<b>%2</b>")
                  .arg(devName)
                  .arg(otp.get_sn(), 8, 16, QLatin1Char('0'))
                 );

    const auto device_keys = keys_info.get_device_keys();

    append_to_log(QString("  %1/%2 keys on device%3")
    .arg(device_keys.size())
    .arg(constants::max_keys)
    .arg(device_keys.size() ? ":" : "")
    );

    for (const auto &key: device_keys)
    {
      auto sw_name = QString::fromStdString(mesytec::mvlc::vme_modules::mdpp_firmware_name(key.get_sw()));
      auto str = QSL("    id=%1, name=%2")
        .arg(key.get_sw(), 4, 16, QLatin1Char('0'))
        .arg(sw_name);
      append_to_log(str);
    }
  //} catch (const std::system_error &e) {
  //  append_to_log(QSL("%1: %2")
  //  .arg(e.code().category().name())
  //  .arg(e.what()));
  } catch (const std::exception &e) {
    append_to_log(QString(e.what()));
  }
}

void MVPLabGui::adv_dump_to_console()
{
  try {
    auto data = perform_memory_dump();
    qDebug() << "data.size() =" << data.size();
    append_to_log(QString("data.size()=%1").arg(data.size()));
    append_to_log("\n" + format_bytes(data));
  } catch (const std::exception &e) {
    append_to_log(QString(e.what()));
  }
}

void MVPLabGui::adv_save_to_file(const QString &filename)
{
  QVector<uchar> data;

  try {
    data = perform_memory_dump();
  } catch (const std::exception &e) {
    append_to_log(QString(e.what()));
    return;
  }

  QFile f(filename);
  if (!f.open(QIODevice::WriteOnly)) {
    ui->statusbar->showMessage(QString("Error opening %1: %2")
        .arg(filename).arg(f.errorString()));
    return;
  }

  ThreadMover tm(&f, 0);
  m_fw.setFuture(run_in_thread<void>([&] {
        auto sz = f.write(reinterpret_cast<char *>(data.data()), data.size());
        if (sz != data.size()) {
          throw std::runtime_error(QString("Error writing to %1: %2")
              .arg(filename).arg(f.errorString()).toStdString());
        }
      }, &f));

  if (!m_fw.isFinished())
    m_loop.exec();

  try {
    m_fw.waitForFinished();
    append_to_log(QString("Memory written to %1").arg(filename));
  } catch (const std::exception &e) {
    auto errstr(QString("Error: ") + e.what());
    append_to_log(errstr);
  }
}

void MVPLabGui::adv_load_from_file(const QString &filename)
{
  QFile f(filename);

  if (!f.open(QIODevice::ReadOnly)) {
    ui->statusbar->showMessage(QString("Error opening %1: %2")
        .arg(filename).arg(f.errorString()));
    return;
  }

  QVector<uchar> buf(f.bytesAvailable());

  {
    ThreadMover tm(&f, 0);

    m_fw.setFuture(run_in_thread<void>([&] {
          auto sz = f.read(reinterpret_cast<char *>(buf.data()), buf.size());
          if (sz < 0) {
            throw std::runtime_error(QString("Error reading from %1: %2")
                .arg(filename).arg(f.errorString()).toStdString());
          }
        }, &f));

    if (!m_fw.isFinished())
      m_loop.exec();

    try {
      m_fw.waitForFinished();
    } catch (const std::exception &e) {
      auto errstr(QString("Error: ") + e.what());
      append_to_log(errstr);
    }
  }

  auto a_start    = m_advancedwidget->get_start_address();
  auto area       = m_advancedwidget->get_selected_area();
  auto section    = m_advancedwidget->get_selected_section();

  ThreadMover tm(m_object_holder, 0);

  m_fw.setFuture(run_in_thread<void>([&] {
      auto connector = getActiveConnector();
      connector->open();
      auto flash = connector->getFlash();
      flash->ensure_clean_state();
      flash->set_area_index(area);
      flash->write_memory(a_start, section, buf);
    }, m_object_holder));

  if (!m_fw.isFinished())
    m_loop.exec();

  try {
    m_fw.waitForFinished();
  } catch (const std::exception &e) {
    append_to_log(QString(e.what()));
  }
}

void MVPLabGui::adv_boot(uchar area)
{
  append_to_log(QString("Booting area %1").arg(area));

  ThreadMover tm(m_object_holder, 0);

  m_fw.setFuture(run_in_thread<void>([&] {
    auto connector = getActiveConnector();
    connector->open();
    auto flash = connector->getFlash();
    flash->boot(area);
  }, m_object_holder));

  if (!m_fw.isFinished())
    m_loop.exec();

  try {
    m_fw.waitForFinished();
  } catch (const std::exception &e) {
    append_to_log(QString(e.what()));
  }
}

void MVPLabGui::adv_nop_recovery()
{
  ThreadMover tm(m_object_holder, 0);

  m_fw.setFuture(run_in_thread<void>([&] {
        qDebug() << "open_port()";
        auto connector = getActiveConnector();
        connector->open();
        auto flash = connector->getFlash();
        qDebug() << "recover";
        flash->recover();
      }, m_object_holder));

  if (!m_fw.isFinished())
    m_loop.exec();

  try {
    m_fw.waitForFinished();
  } catch (const std::exception &e) {
    qDebug() << "exception from future:" << e.what();
    append_to_log(QString(e.what()));
  }
}

void MVPLabGui::adv_erase_section(uchar area, uchar section)
{
  append_to_log(QString("Erasing section %1 (area=%2)")
                .arg(section)
                .arg(area));

  ThreadMover tm(m_object_holder, 0);
  m_fw.setFuture(run_in_thread<void>([&] {
        auto connector = getActiveConnector();
        connector->open();
        auto flash = connector->getFlash();
        flash->ensure_clean_state();
        flash->set_area_index(area);
        flash->erase_section(section);
      }, m_object_holder));

  if (!m_fw.isFinished())
    m_loop.exec();

  try {
    m_fw.waitForFinished();
  } catch (const std::exception &e) {
    append_to_log(QString(e.what()));
  }
}

void MVPLabGui::adv_read_hardware_id()
{
  ThreadMover tm(m_object_holder, 0);
  auto f_result = run_in_thread<uchar>([&] {
        qDebug() << "gui rdi: open_port";
        auto connector = getActiveConnector();
        connector->open();
        auto flash = connector->getFlash();
        qDebug() << "gui rdi: ensure clean state";
        flash->ensure_clean_state();
        qDebug() << "gui rdi: read area index";
        return flash->read_hardware_id();
      }, m_object_holder);

  m_fw.setFuture(f_result);

  if (!m_fw.isFinished())
    m_loop.exec();

  try {
    append_to_log(QString("Hardware ID = 0x%1").arg(
          QString::number(static_cast<int>(f_result.result()), 16)));
  } catch (const std::exception &e) {
    append_to_log(QString("Error from read_hardware_id(): %1").arg(e.what()));
  }
}

KeysInfo MVPLabGui::read_device_keys()
{
    auto flash = getActiveConnector()->getFlash();
    auto keys_handler = std::make_unique<KeysHandler>(
          m_firmware,
          flash,
          m_object_holder);

    auto keys_info = run_in_thread_wait_in_loop<KeysInfo>([&] {
        auto connector = getActiveConnector();
        connector->open();
        auto flash = connector->getFlash();
        flash->ensure_clean_state();
        return keys_handler->get_keys_info();
      }, m_object_holder, m_fw);

    return keys_info;
}

void MVPLabGui::adv_keys_info()
{
  try {
    auto keys_info = read_device_keys();
    auto otp = keys_info.get_otp();
    auto devName = otp.get_device();
    devName.replace(' ', "&nbsp;");

    append_to_log(QString("Device Info: OTP(dev='<b>%1</b>', serial=<b>%2</b>)")
                  .arg(devName)
                  .arg(otp.get_sn(), 8, 16, QLatin1Char('0'))
                 );

    append_to_log(QString("  %1 keys on device, %2 keys in firmware, %3 new keys")
        .arg(keys_info.get_device_keys().size())
        .arg(keys_info.get_firmware_keys().size())
        .arg(keys_info.get_new_firmware_keys().size()));

    const auto device_keys = keys_info.get_device_keys();

    if (device_keys.size()) {
      append_to_log("  Device keys:");

      for (const auto &key: device_keys) {
        append_to_log("    " + key.to_string());
      }
    }
  } catch (const std::exception &e) {
    append_to_log(QString(e.what()));
  }
}

// Creates a KeyFirmwarePart from the given Key.
// This can be used to download a keys from a device and later upload it again
// using the normal FirmwarePart mechanism.
KeyFirmwarePart *firmware_part_from_key(const Key &key)
{
    QStringList instructionLines =
    {
        "@0x00",
        ">" + key.get_prefix(),
        "@0x08",
        QString("%%1").arg(key.get_sn(), 8, 16, QLatin1Char('0')),
        "@0x0C",
        QString("%%1").arg(key.get_sw(), 4, 16, QLatin1Char('0')),
        "@0x10",
        QString("%%1").arg(key.get_key(), 8, 16, QLatin1Char('0')),
    };

    QString instructionText = instructionLines.join("\n");

    QVector<uchar> contents;

    for (auto c: instructionText)
        contents.push_back(c.toLatin1());

    return new KeyFirmwarePart("<none>", contents);
}

void MVPLabGui::adv_manage_keys()
{
    // - download keys from device (same as in adv_keys_info())
    // - create a dialog with a list of checkable keys.
    //   all keys checked by default. user can uncheck entries to delete corresponding keys
    // - ok/cancel buttons
    // - on ok: delete all device keys (this is done by just earsing the keys
    //          section), then rewrite the ones that should be kept.

  try {
    auto keys_info = read_device_keys();
    auto key_list = keys_info.get_device_keys().values();

    std::sort(key_list.begin(), key_list.end(), [] (const Key &ka, const Key &kb)
              {
                  if (ka.get_prefix() != kb.get_prefix())
                      return ka.get_prefix() < kb.get_prefix();

                  if (ka.get_sn() != kb.get_sn())
                      return ka.get_sn() < kb.get_sn();

                  if (ka.get_sw() != kb.get_sw())
                      return ka.get_sw() < kb.get_sw();

                  return ka.get_key() < kb.get_key();
              });

    KeySelectionDialog dialog(key_list, this);

    if (dialog.exec() != QDialog::Accepted)
        return;

    auto keys_to_keep = dialog.get_selected_keys();

    assert(keys_to_keep.size() <= key_list.size());

    if (keys_to_keep == key_list)
        return;

    FirmwareArchive fwa;

    for (const auto &key: keys_to_keep)
        fwa.add_part(std::shared_ptr<FirmwarePart>(firmware_part_from_key(key)));

    auto flash = getActiveConnector()->getFlash();

    auto keys_handler = std::unique_ptr<KeysHandler>(
        new KeysHandler(
          fwa,
          flash,
          m_object_holder));

    connect(keys_handler.get(), SIGNAL(status_message(const QString &)),
            this, SLOT(append_to_log(const QString &)),
            Qt::QueuedConnection);

    run_in_thread_wait_in_loop([&] {
        auto connector = getActiveConnector();
        connector->open();
        auto flash = connector->getFlash();
        flash->ensure_clean_state();
        flash->erase_section(constants::keys_section);
        keys_handler->write_keys();
      }, m_object_holder, m_fw);
  } catch (const std::exception &e) {
    append_to_log(QString(e.what()));
  }
}

//
// mdpp16
//
void MVPLabGui::adv_mdpp16_cal_dump_to_console()
{
  try
  {
    auto data = read_mdpp16_calibration_data();
    QString buf;
    QTextStream strm(&buf);
    strm << Qt::endl;
    mdpp16::format_calibration_data(gsl::span(data), strm);
    append_to_log(buf);
  }
  catch (const std::exception &e)
  {
    append_to_log(QString(e.what()));
  }
}

void MVPLabGui::adv_mdpp16_cal_save_to_file(const QString &filename)
{
  try {
    auto data = read_mdpp16_calibration_data();

    QFile f(filename);
    if (!f.open(QIODevice::WriteOnly)) {
      ui->statusbar->showMessage(QString("Error opening %1: %2")
          .arg(filename).arg(f.errorString()));
      return;
    }

    QTextStream strm(&f);
    mdpp16::format_calibration_data(gsl::span(data), strm);
    append_to_log(QString("Calibration data written to %1").arg(filename));
  } catch (const std::exception &e) {
    append_to_log(QString(e.what()));
  }
}

QVector<uchar> MVPLabGui::read_mdpp16_calibration_data()
{
  return run_in_thread_wait_in_loop<QVector<uchar>>([&] {
      auto connector = getActiveConnector();
      connector->open();
      auto flash = connector->getFlash();
      flash->ensure_clean_state();

      return flash->read_memory(
          {0, 0, 0},
          constants::common_calibration_section,
          mdpp16::calib_data_size,
          get_default_mem_read_chunk_size());
    }, m_object_holder, m_fw);
}

//
// mdpp32
//
void MVPLabGui::adv_mdpp32_cal_dump_to_console()
{
  try
  {
    auto data = read_mdpp32_calibration_data();
    QString buf;
    QTextStream strm(&buf);
    strm << Qt::endl;
    mdpp32::format_calibration_data(gsl::span(data), strm);
    append_to_log(buf);
  }
  catch (const std::exception &e)
  {
    append_to_log(QString(e.what()));
  }
}

void MVPLabGui::adv_mdpp32_cal_save_to_file(const QString &filename)
{
  try {
    auto data = read_mdpp32_calibration_data();

    QFile f(filename);
    if (!f.open(QIODevice::WriteOnly)) {
      ui->statusbar->showMessage(QString("Error opening %1: %2")
          .arg(filename).arg(f.errorString()));
      return;
    }

    QTextStream strm(&f);
    mdpp32::format_calibration_data(gsl::span(data), strm);
    append_to_log(QString("Calibration data written to %1").arg(filename));
  } catch (const std::exception &e) {
    append_to_log(QString(e.what()));
  }
}

QVector<uchar> MVPLabGui::read_mdpp32_calibration_data()
{
  return run_in_thread_wait_in_loop<QVector<uchar>>([&] {
      auto connector = getActiveConnector();
      connector->open();
      auto flash = connector->getFlash();
      flash->ensure_clean_state();

      return flash->read_memory(
          {0, 0, 0},
          constants::common_calibration_section,
          mdpp32::calib_data_size,
          get_default_mem_read_chunk_size());
    }, m_object_holder, m_fw);
}

QVector<uchar> MVPLabGui::perform_memory_dump()
{
  auto a_start    = m_advancedwidget->get_start_address();
  auto len_bytes  = m_advancedwidget->get_len_bytes();
  auto area       = m_advancedwidget->get_selected_area();
  auto section    = m_advancedwidget->get_selected_section();

  auto f = [&]
  {
    auto connector = getActiveConnector();
    connector->open();
    auto flash = connector->getFlash();
    flash->ensure_clean_state();
    flash->set_area_index(area);
    const size_t chunk_size = 60;
    return flash->read_memory(a_start, section, len_bytes, chunk_size);
  };

  auto result = run_in_thread_wait_in_loop<QVector<uchar>>(f, m_object_holder, m_fw);
  return result;
}

MvpConnectorInterface *MVPLabGui::getActiveConnector()
{
  if (tabs_connectors_->currentIndex() == 0)
    return serialPortConnector_;
  else if (tabs_connectors_->currentIndex() == 1)
    return mvlcConnector_;

  return nullptr;
}

KeySelectionDialog::KeySelectionDialog(const KeyList &keys, QWidget *parent)
    : QDialog(parent)
    , m_keys(keys)
    , m_listWidget(new QListWidget)
{
    auto bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

    connect(bb, &QDialogButtonBox::accepted,
            this, &QDialog::accept);

    connect(bb, &QDialogButtonBox::rejected,
            this, &QDialog::reject);

    auto bb_layout = new QHBoxLayout;
    bb_layout->setContentsMargins(2, 2, 2, 2);
    bb_layout->addStretch();
    bb_layout->addWidget(bb);

    auto widgetLayout = new QVBoxLayout(this);
    widgetLayout->setContentsMargins(2, 2, 2, 2);
    widgetLayout->setSpacing(2);
    widgetLayout->addWidget(m_listWidget);
    widgetLayout->addLayout(bb_layout);

    setWindowTitle("Key Management");
    resize(500, 300);

    populate();
}

KeyList KeySelectionDialog::get_selected_keys() const
{
    KeyList result;

    for (int row = 0; row < m_listWidget->count(); row++)
    {
        auto item = m_listWidget->item(row);

        if (row < m_keys.size() && item->checkState() == Qt::Checked)
        {
            result.push_back(m_keys.at(row));
        }
    }

    return result;
}

void KeySelectionDialog::populate()
{
    for (const auto &key: m_keys)
    {
        auto item = new QListWidgetItem(key.to_string());
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Checked);
        m_listWidget->addItem(item);
    }
}

} // ns mvp
} // ns mesytec
