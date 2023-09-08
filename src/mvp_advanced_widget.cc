#include "mvp_advanced_widget.h"
#include "ui_mvp_advanced_widget.h"
#include "flash.h"
#include <QFileDialog>
#include <QMenu>

namespace mesytec
{
namespace mvp
{

MvpAdvancedWidget::MvpAdvancedWidget(QWidget *parent)
  : QWidget(parent)
  , ui(new Ui::MvpAdvancedWidget)
{
  ui->setupUi(this);

  for (auto sec: get_valid_sections()) {
    ui->combo_section->addItem(QString::number(sec), sec);
  }

  m_hex_dec_spins = {
    ui->spin_a1_begin, ui->spin_a1_end,
    ui->spin_a2_begin, ui->spin_a2_end,
    ui->spin_len,
  };

  m_address_spins = {
    ui->spin_a1_begin, ui->spin_a1_end,
    ui->spin_a2_begin, ui->spin_a2_end,
  };

  for (auto spin: m_address_spins) {
    connect(spin, SIGNAL(valueChanged(int)), this, SLOT(address_spin_value_changed()));
  }

  connect(ui->pb_dump_to_console, SIGNAL(clicked()),
      this, SIGNAL(sig_dump_to_console()));

  connect(ui->pb_nop_recovery, SIGNAL(clicked()),
      this, SIGNAL(sig_nop_recovery()));

  connect(ui->pb_rdi, SIGNAL(clicked()),
      this, SIGNAL(sig_read_hardware_id()));

  connect(ui->pb_keys_info, SIGNAL(clicked()),
      this, SIGNAL(sig_keys_info()));

  connect(ui->pb_manage_keys, SIGNAL(clicked()),
      this, SIGNAL(sig_manage_keys()));

  // mdpp16
  {
      auto menu = new QMenu(this);

      menu->addAction("Dump calibration data to console",
          this, SIGNAL(sig_mdpp16_cal_dump_to_console()));

      menu->addAction("Save calibration data to file",
          this, SLOT(slt_mdpp16_cal_save_to_file()));

      ui->pb_mdpp16_cal->setMenu(menu);
  }

  // mdpp32
  {
      auto menu = new QMenu(this);

      menu->addAction("Dump calibration data to console",
          this, SIGNAL(sig_mdpp32_cal_dump_to_console()));

      menu->addAction("Save calibration data to file",
          this, SLOT(slt_mdpp32_cal_save_to_file()));

      ui->pb_mdpp32_cal->setMenu(menu);
  }
}

Address MvpAdvancedWidget::get_start_address() const
{
  return Address(
      0u,
      ui->spin_a1_begin->value(),
      ui->spin_a2_begin->value());
}

Address MvpAdvancedWidget::get_end_address() const
{
  return Address(
      0u,
      ui->spin_a1_end->value(),
      ui->spin_a2_end->value());
}

size_t MvpAdvancedWidget::get_len_bytes() const
{
  return (get_end_address() - get_start_address()).to_int() + constants::page_size;
}

uchar MvpAdvancedWidget::get_selected_area() const
{
  return static_cast<uchar>(ui->combo_area->currentIndex());
}

uchar MvpAdvancedWidget::get_selected_section() const
{
  return static_cast<uchar>(
      ui->combo_section->currentData().toInt());
}

void MvpAdvancedWidget::set_start_address(const Address &a)
{
  qDebug() << "set_start_address" << a.to_int();

  if (get_start_address() == a)
    return;

  auto b = QSignalBlocker(ui->spin_a1_begin);
  ui->spin_a1_begin->setValue(a[1]);

  b = QSignalBlocker(ui->spin_a2_begin);
  ui->spin_a2_begin->setValue(a[2]);

  update_page_display();
}

void MvpAdvancedWidget::set_end_address(const Address &a)
{
  qDebug() << "set_end_address" << a.to_int();

  if (get_end_address() == a)
    return;

  auto b = QSignalBlocker(ui->spin_a1_end);
  ui->spin_a1_end->setValue(a[1]);

  b = QSignalBlocker(ui->spin_a2_end);
  ui->spin_a2_end->setValue(a[2]);

  update_page_display();
}

void MvpAdvancedWidget::on_spin_len_valueChanged(int len)
{
  qDebug() << "begin len_spin_value_changed" << len;

  auto i(get_start_address().to_int());
  i = std::min((i+(len-1)*constants::page_size), constants::address_max);
  set_end_address(Address(i));

  auto start(get_start_address());
  auto end(get_end_address());

  if (start > end) {
    end = start;
  }

  set_start_address(start);
  set_end_address(end);

  len = (end.to_int()-start.to_int());
  auto blocks(len / constants::page_size);

  qDebug("len_spin_value_changed: len=%d, blocks=%ld", len, blocks);

  update_page_display();
}

void MvpAdvancedWidget::on_rb_dec_toggled(bool checked)
{
  int base = checked ? 10 : 16;

  for (auto spin: m_hex_dec_spins) {
    spin->setDisplayIntegerBase(base);
  }
}

void MvpAdvancedWidget::address_spin_value_changed()
{
  auto start(get_start_address());
  auto end(get_end_address());

  if (start > end) {
    end = start;
  }

  set_start_address(start);
  set_end_address(end);

  update_page_display();
}

void MvpAdvancedWidget::on_pb_boot_clicked()
{
  emit sig_boot(get_selected_area());
}

void MvpAdvancedWidget::on_pb_save_to_file_clicked()
{
  QString dir = QStandardPaths::standardLocations(
        QStandardPaths::DocumentsLocation).value(0, QString());

  const QString key(QStringLiteral("directories/memory_dump_save"));

  QSettings settings;

  dir = settings.value(key, dir).toString();

  QString filename(QFileDialog::getSaveFileName(this,
        tr("Save memory to file"), dir, tr("bin files (*.bin)")));

  if (filename.isEmpty())
    return;

  QFileInfo fi(filename);

  settings.setValue(key, fi.path());

  if (fi.suffix().isEmpty())
    filename += ".bin";

  emit sig_save_to_file(filename);
}

void MvpAdvancedWidget::on_pb_load_from_file_clicked()
{
  QString dir = QStandardPaths::standardLocations(
        QStandardPaths::DocumentsLocation).value(0, QString());

  const QString key(QStringLiteral("directories/memory_dump_load"));

  QSettings settings;

  dir = settings.value(key, dir).toString();

  QString filename(QFileDialog::getOpenFileName(this,
      tr("Open bin file"), dir, tr("bin files (*.bin)")));

  if (filename.isEmpty())
    return;

  QFileInfo fi(filename);

  settings.setValue(key, fi.path());

  emit sig_load_from_file(filename);
}

void MvpAdvancedWidget::on_pb_erase_section_clicked()
{
  emit sig_erase_section(
      get_selected_area(),
      get_selected_section());
}

void MvpAdvancedWidget::update_page_display()
{
  auto a_start    = get_start_address();
  auto a_end      = get_end_address();
  auto len_bytes  = (a_end - a_start).to_int() + constants::page_size;
  auto len_pages  = len_bytes / constants::page_size;

  qDebug("update_page_display: start=%d, end=%d, len=%ld, pages=%ld",
      a_start.to_int(), a_end.to_int(), len_bytes, len_pages);

  auto b = QSignalBlocker(ui->spin_len);
  ui->spin_len->setValue(len_pages);

  ui->label_bytes->setText(QString("%L1").arg(len_bytes));
}

void MvpAdvancedWidget::slt_mdpp16_cal_save_to_file()
{
  QString dir = QStandardPaths::standardLocations(
        QStandardPaths::DocumentsLocation).value(0, QString());

  const QString key(QStringLiteral("directories/calibration_data_save"));

  QSettings settings;

  dir = settings.value(key, dir).toString();

  QString filename(QFileDialog::getSaveFileName(this,
        tr("Save calibration data to file"), dir, tr("cal files (*.cal)")));

  if (filename.isEmpty())
    return;

  QFileInfo fi(filename);

  settings.setValue(key, fi.path());

  if (fi.suffix().isEmpty())
    filename += ".cal";

  emit sig_mdpp16_cal_save_to_file(filename);
}

void MvpAdvancedWidget::slt_mdpp32_cal_save_to_file()
{
  QString dir = QStandardPaths::standardLocations(
        QStandardPaths::DocumentsLocation).value(0, QString());

  const QString key(QStringLiteral("directories/calibration_data_save"));

  QSettings settings;

  dir = settings.value(key, dir).toString();

  QString filename(QFileDialog::getSaveFileName(this,
        tr("Save calibration data to file"), dir, tr("cal files (*.cal)")));

  if (filename.isEmpty())
    return;

  QFileInfo fi(filename);

  settings.setValue(key, fi.path());

  if (fi.suffix().isEmpty())
    filename += ".cal";

  emit sig_mdpp32_cal_save_to_file(filename);
}

} // ns mvp
} // ns mesytec
