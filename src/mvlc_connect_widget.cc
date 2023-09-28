#include "mvlc_connect_widget.h"
#include "ui_mvlc_connect_widget.h"

#include <mesytec-mvlc/mvlc_impl_usb.h>
#include <QDialog>
#include <QHeaderView>
#include <QSettings>
#include <QTableWidget>
#include <QTextStream>

namespace mesytec::mvp
{

namespace
{

std::vector<QVariantMap> load_eth_history()
{
    QSettings settings;
    auto hosts = settings.value("mvp/mvlc_eth_history").toStringList();
    std::vector<QVariantMap> result;
    std::transform(std::begin(hosts), std::end(hosts), std::back_inserter(result),
        [] (const QString &host)
        {
            QVariantMap m;
            m["method"] = "eth";
            m["address"] = host;
            return m;
        });
    return result;
}

void store_eth_history(const std::vector<QVariantMap> &entries)
{
    QStringList hosts;

    for (const auto &entry: entries)
    {
        if (entry["method"] == "eth")
            hosts.push_back(entry["address"].toString());
    }

    QSettings settings;
    settings.setValue("mvp/mvlc_eth_history", hosts);
}

std::vector<QVariantMap> list_usb_devices()
{
    auto devices = mesytec::mvlc::usb::get_device_info_list();
    std::vector<QVariantMap> result;

    std::transform(std::begin(devices), std::end(devices), std::back_inserter(result),
        [] (const mesytec::mvlc::usb::DeviceInfo &device)
        {
            QVariantMap m;
            m["method"] = "usb";
            m["index"] = device.index;
            m["serial"] = QString::fromStdString(device.serial);
            m["description"] = QString::fromStdString(device.description);
            return m;
        });

    return result;
}

} // end anon namespace

struct MvlcConnectWidget::Private
{
    MvlcConnectWidget *q;
    Ui::MvlcConnectWidget ui_;
    bool isConnected_ = false;
    QVariantList prevUsbDevices_;
    // Stores connection info for each successfully connected MVLC.
    std::vector<QVariantMap> connectHistory_;

    bool isEth() const
    {
        return ui_.tabs_connectMethod->currentIndex() == 1;
    }

    bool isUsb() const
    {
        return ui_.tabs_connectMethod->currentIndex() == 0;
    }

    QComboBox *activeConnectCombo() const
    {
        if (isEth())
            return ui_.combo_eth;
        else if (isUsb())
            return ui_.combo_usb;
        return nullptr;
    }

    QPushButton *activeConnectButton()
    {
        if (isEth())
            return ui_.pb_connect_eth;
        else if (isUsb())
            return ui_.pb_connect_usb;
        return nullptr;
    }
};

MvlcConnectWidget::MvlcConnectWidget(QWidget *parent)
    : QWidget(parent)
    , d(std::make_unique<Private>())
{
    d->q = this;
    d->ui_.setupUi(this);
    d->ui_.combo_vmeAddress->setCurrentText("0x00000000");

    connect(d->ui_.pb_scanbus, &QPushButton::clicked, this, &MvlcConnectWidget::scanbusRequested);
    connect(d->ui_.pb_connect_eth, &QPushButton::clicked, this, &MvlcConnectWidget::onConnectButtonClicked);
    connect(d->ui_.pb_connect_usb, &QPushButton::clicked, this, &MvlcConnectWidget::onConnectButtonClicked);

    connect(d->ui_.combo_eth, &QComboBox::currentTextChanged,
        this, &MvlcConnectWidget::onConnectInfoChangedInWidget);
    connect(d->ui_.combo_eth, qOverload<int>(&QComboBox::currentIndexChanged),
        this, &MvlcConnectWidget::onConnectInfoChangedInWidget);
    connect(d->ui_.combo_eth, qOverload<int>(&QComboBox::activated),
        this, &MvlcConnectWidget::onConnectButtonClicked);

    connect(d->ui_.combo_usb, qOverload<int>(&QComboBox::currentIndexChanged),
        this, &MvlcConnectWidget::onConnectInfoChangedInWidget);

    connect(d->ui_.combo_vmeAddress, &QComboBox::currentTextChanged,
        this, &MvlcConnectWidget::onConnectInfoChangedInWidget);
    connect(d->ui_.combo_vmeAddress, qOverload<int>(&QComboBox::currentIndexChanged),
        this, &MvlcConnectWidget::onConnectInfoChangedInWidget);

    // Switching between the USB and ETH tabs also needs to update the
    // connection info.
    connect(d->ui_.tabs_connectMethod, &QTabWidget::currentChanged,
        this, &MvlcConnectWidget::onConnectInfoChangedInWidget);

    if (auto ethInfos = load_eth_history(); !ethInfos.empty())
    {
        d->ui_.combo_eth->clear();
        for (const auto &info: ethInfos)
        {
            d->ui_.combo_eth->addItem(info["address"].toString());
            d->connectHistory_.push_back(info);
        }
    }
}

MvlcConnectWidget::~MvlcConnectWidget()
{
    store_eth_history(d->connectHistory_);
}

QVariantMap MvlcConnectWidget::getConnectInfo()
{
    if (auto combo = d->activeConnectCombo())
    {
        QVariantMap result;

        if (d->isEth())
        {
            result["method"] = "eth";
            result["address"] = combo->currentText();
        }
        else if (d->isUsb())
        {
            result["method"] = "usb";
            result = combo->currentData().toMap();
        }

        result["vme_address"] = d->ui_.combo_vmeAddress->currentText();

        return result;
    }
    return {};
}

void MvlcConnectWidget::setConnectInfo(const QVariantMap &info)
{
    if (auto combo = d->activeConnectCombo())
    {
        auto idx = combo->findData(info);
        if (idx >= 0)
            combo->setCurrentIndex(idx);
        else
        {
            combo->addItem(get_mvlc_connect_info_title(info), info);
            combo->setCurrentIndex(combo->count() - 1);
        }
    }
}

void MvlcConnectWidget::setScanbusResult(const QVariantList &scanbusResult)
{
    const QStringList headerLabels =
    {
        "Address",
        "HardwareId",
        "FirmwareId",
        "Module Type",
        "Firmware Type"
    };

    auto table = new QTableWidget;
    table->setRowCount(scanbusResult.size());
    table->setColumnCount(headerLabels.size());
    table->setHorizontalHeaderLabels(headerLabels);
    table->verticalHeader()->hide();
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);

    unsigned currentRow = 0;

    for (const auto &var: scanbusResult)
    {
        const auto m = var.toMap();
        mvlc::u32 vmeAddress = m["address"].toUInt();
        auto item0 = new QTableWidgetItem(tr("0x%1").arg(vmeAddress, 8, 16, QLatin1Char('0')));
        auto item1 = new QTableWidgetItem(tr("0x%1").arg(m["hwId"].toUInt(), 4, 16, QLatin1Char('0')));
        auto item2 = new QTableWidgetItem(tr("0x%1").arg(m["fwId"].toUInt(), 4, 16, QLatin1Char('0')));
        auto item3 = new QTableWidgetItem(m["module_type"].toString());
        auto item4 = new QTableWidgetItem(m["firmware_type"].toString());

        auto items = { item0, item1, item2, item3, item4 };
        unsigned col = 0;

        for (auto item: items)
        {
            item->setData(Qt::UserRole, vmeAddress);
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            table->setItem(currentRow, col++, item);
        }

        ++currentRow;
    }

    table->resizeColumnsToContents();
    table->resizeRowsToContents();

    QDialog dlg;
    dlg.setWindowTitle("Scanbus Results");
    dlg.setWindowIcon(QIcon(":/window-icon.png"));
    auto dlgLayout = new QVBoxLayout(&dlg);
    auto explanation = new QLabel("Double click a line to use the module address as the VME target device address.");
    explanation->setWordWrap(true);
    dlgLayout->addWidget(explanation);
    dlgLayout->addWidget(table);
    dlg.resize(600, 400);

    mvlc::u32 selectedVMEAddress = 0u;

    connect(table, &QTableWidget::itemDoubleClicked,
        &dlg, [&] (QTableWidgetItem *item)
        {
            selectedVMEAddress = item->data(Qt::UserRole).toUInt();
            dlg.accept();
        });

    if (dlg.exec() == QDialog::Accepted)
    {
        auto &combo = d->ui_.combo_vmeAddress;
        auto addressText = tr("0x%1").arg(selectedVMEAddress, 8, 16, QLatin1Char('0'));

        if (int idx = combo->findText(addressText); idx >= 0)
        {
            combo->setCurrentIndex(idx);
        }
        else
        {
            combo->addItem(addressText);
            combo->setCurrentIndex(combo->count() - 1);
        }
    }
}

void MvlcConnectWidget::setUsbDevices(const QVariantList &usbDevices)
{
    if (usbDevices == d->prevUsbDevices_)
        return;

    auto &combo = d->ui_.combo_usb;

    const auto currentInfo = combo->currentData().toMap();

    QSignalBlocker b(combo);

    combo->clear();

    for (const auto &var: usbDevices)
    {
        const auto m = var.toMap();
        combo->addItem(get_mvlc_connect_info_title(m), m);
    }

    int idx = 0;

    if (!currentInfo.isEmpty())
    {
        for (int i=0; i<combo->count(); ++i)
        {
            auto m = combo->itemData(i).toMap();
            if (m["serial"] == currentInfo["serial"])
            {
                idx = i;
                break;
            }
        }
    }

    combo->setCurrentIndex(idx);
    d->prevUsbDevices_ = usbDevices;

    // Note: USB might not be the currently active connection method but it's
    // easier to just emit the signal anyways. getConnectInfo() will correctly
    // determine the active connection method and return the correct info.
    emit mvlcConnectInfoChanged(getConnectInfo());
}

void MvlcConnectWidget::mvlcSuccessfullyConnected(const QVariantMap &info)
{
    auto it = std::remove_if(
        std::begin(d->connectHistory_), std::end(d->connectHistory_),
        [&info] (const auto &entry) { return entry["address"] == info["address"]; });

    d->connectHistory_.erase(it, std::end(d->connectHistory_));
    d->connectHistory_.push_back(info);
}

void MvlcConnectWidget::onConnectButtonClicked()
{
    emit connectMvlc(getConnectInfo());
}

void MvlcConnectWidget::onConnectInfoChangedInWidget()
{
    emit mvlcConnectInfoChanged(getConnectInfo());
}

QString get_mvlc_connect_info_title(const QVariantMap &info)
{
    if (info["method"] == "eth")
        return info["address"].toString();
    else if (info["method"] == "usb")
        return QStringLiteral("%1 - %2").arg(info["description"].toString()).arg(info["serial"].toString());
    return {};
}

}
