#include "mvlc_connect_widget.h"
#include "ui_mvlc_connect_widget.h"

#include <mesytec-mvlc/mvlc_impl_usb.h>
#include <QSettings>

namespace mesytec::mvp
{

std::vector<QVariantMap> get_eth_history()
{
    QSettings settings;
    auto entries = settings.value("mvp/mvlc_eth_history").toStringList();
    std::vector<QVariantMap> result;
    std::transform(std::begin(entries), std::end(entries), std::back_inserter(result),
        [] (const QString &entry)
        {
            QVariantMap m;
            m["method"] = "eth";
            m["address"] = entry;
            return m;
        });
    return result;
}

void store_eth_history(const std::vector<QVariantMap> &entries)
{
}

std::vector<QVariantMap> get_usb_devices()
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

QString connect_info_title(const QVariantMap &info)
{
    if (info["method"] == "eth")
        return info["address"].toString();
    else if (info["method"] == "usb")
        return QStringLiteral("%1 - %2").arg(info["description"].toString()).arg(info["serial"].toString());
    return {};
}

struct MvlcConnectWidget::Private
{
    MvlcConnectWidget *q;
    Ui::MvlcConnectWidget ui_;
    bool isConnected_ = false;

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
}

MvlcConnectWidget::~MvlcConnectWidget()
{
}

void MvlcConnectWidget::setIsConnected(bool isConnected)
{
    if (auto pb = d->activeConnectButton())
    {
        if (isConnected)
            pb->setText("Disconnect");
        else
            pb->setText("Connect");
    }
}

QVariantMap MvlcConnectWidget::getConnectInfo()
{
    if (auto combo = d->activeConnectCombo())
        return combo->currentData().toMap();
    return {};
}

void MvlcConnectWidget::setConnectInfo(const QVariantMap &info)
{
    if (auto combo = d->activeConnectCombo())
    {
        auto idx = combo->findData(info);
        if (idx >= 0)
            combo->setCurrentIndex(idx);
    }
}

}