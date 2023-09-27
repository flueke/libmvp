#include "mvlc_mvp_connector.h"

#include <mesytec-mvlc/mvlc_impl_usb.h>
#include <mesytec-mvlc/scanbus_support.h>

#include "mvlc_mvp_flash.h"

namespace mesytec::mvp
{

struct MvlcMvpConnector::Private
{
    mvlc::MVLC mvlc_;
    MvlcMvpFlash *flash_;
    QVariantMap connectInfo_;
    QVariantMap activeConnectInfo_;
};

MvlcMvpConnector::MvlcMvpConnector(QObject *parent)
    : MvpConnectorInterface(parent)
    , d(std::make_unique<Private>())
{
    d->flash_ = new MvlcMvpFlash(this);

    auto refreshTimer = new QTimer(this);
    connect(refreshTimer, &QTimer::timeout, this, &MvlcMvpConnector::refreshUsbDevices);
    refreshTimer->setInterval(1000);
    refreshTimer->start();
}

MvlcMvpConnector::~MvlcMvpConnector()
{
}

void MvlcMvpConnector::open()
{
    close();

    const auto &m = d->connectInfo_;

    if (m["method"] == "eth")
        d->mvlc_ = mvlc::make_mvlc_eth(m["address"].toString().toStdString());
    else if (m["method"] == "usb")
        d->mvlc_ = mvlc::make_mvlc_usb(m["serial"].toString().toStdString());
    else
        throw std::runtime_error("MvlcMvpConnector error: could not parse connectionInfo map");

    if (auto ec = d->mvlc_.connect())
        throw std::system_error(ec);

    emit connectedToMVLC(m);

    bool addrOk = false;
    auto vmeAddress = m["vme_address"].toString().toUInt(&addrOk, 0);

    if (!addrOk)
        throw std::runtime_error("MvlcMvpConnector error: could not parse target VME address");

    d->flash_->setMvlc(d->mvlc_);
    d->flash_->setVmeAddress(vmeAddress);
}

void MvlcMvpConnector::close()
{
    d->flash_->maybe_disable_flash_interface();
    if (d->mvlc_)
        d->mvlc_.disconnect();
    d->mvlc_ = mvlc::MVLC();
}

FlashInterface *MvlcMvpConnector::getFlash()
{
    return d->flash_;
}

void MvlcMvpConnector::setConnectInfo(const QVariantMap &info)
{
    d->connectInfo_ = info;
}

void MvlcMvpConnector::refreshUsbDevices()
{
    auto devices = mvlc::usb::get_device_info_list();
    QVariantList usbInfos;

    for (const auto &dev: devices)
    {
        QVariantMap m;
        m["method"] = "usb";
        m["index"] = dev.index;
        m["serial"] = QString::fromStdString(dev.serial);
        m["description"] = QString::fromStdString(dev.description);
        usbInfos.push_back(m);
    }

    emit usbDevicesChanged(usbInfos);
}

QVariantList MvlcMvpConnector::scanbus()
{
    open();
    auto candidates = mvlc::scanbus::scan_vme_bus_for_candidates_stacksize(
        d->mvlc_, mvlc::stacks::StackMemoryWords);
    QVariantList result;

    for (auto &addr: candidates)
    {
        mvlc::scanbus::VMEModuleInfo moduleInfo{};

        if (auto ec = mvlc::scanbus::read_module_info(d->mvlc_, addr, moduleInfo))
            throw std::system_error(ec);

        QVariantMap m;
        m["address"] = addr;
        m["hwId"] = moduleInfo.hwId;
        m["fwId"] = moduleInfo.fwId;
        m["module_type"] = QString::fromStdString(moduleInfo.moduleTypeName());
        m["firmware_type"] = QString::fromStdString(moduleInfo.mdppFirmwareTypeName());
        result.push_back(m);
    }

    emit scanbusResultReady(result);

    return result;
}

}
