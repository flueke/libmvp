#include "serial_port_mvp_connector.h"

#include "serial_port_flash.h"
#include "port_helper.h"

namespace mesytec::mvp
{

struct SerialPortMvpConnector::Private
{
    QSerialPort *serialPort_;
    SerialPortFlash *flash_;
    PortHelper *portHelper_;
    QVariantMap connectInfo_;
};

SerialPortMvpConnector::SerialPortMvpConnector(QObject *parent)
    : MvpConnectorInterface(parent)
    , d(std::make_unique<Private>())
{
    d->serialPort_ = new QSerialPort(this);
    d->flash_ = new SerialPortFlash(d->serialPort_, this);
    d->portHelper_ = new PortHelper(d->serialPort_, this);

    auto refreshTimer = new QTimer(this);
    connect(refreshTimer, &QTimer::timeout, d->portHelper_, &PortHelper::refresh);
    refreshTimer->setInterval(1000);
    refreshTimer->start();
}

SerialPortMvpConnector::~SerialPortMvpConnector()
{
}

void SerialPortMvpConnector::open()
{
    d->portHelper_->open_port();
}

void SerialPortMvpConnector::close()
{
    d->serialPort_->close();
}

FlashInterface *SerialPortMvpConnector::getFlash()
{
    return d->flash_;
}

PortHelper *SerialPortMvpConnector::getPortHelper()
{
    return d->portHelper_;
}

void SerialPortMvpConnector::setConnectInfo(const QVariantMap &info)
{
    d->connectInfo_ = info;
    d->portHelper_->set_selected_port_name(info["serialport"].toString());
}

}
