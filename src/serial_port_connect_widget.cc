#include "serial_port_connect_widget.h"

#include <QBoxLayout>
#include <QComboBox>
#include <QFormLayout>
#include <QPushButton>
#include <QSerialPortInfo>

namespace mesytec::mvp
{

struct SerialPortConnectWidget::Private
{
    QComboBox *combo_ports_;
    QPushButton *pb_refresh_;
    QList<QSerialPortInfo> prevPorts_;
};

SerialPortConnectWidget::SerialPortConnectWidget(QWidget *parent)
    : QWidget(parent)
    , d(std::make_unique<Private>())
{
    d->combo_ports_ = new QComboBox;
    d->combo_ports_->setMinimumWidth(150);
    d->pb_refresh_ = new QPushButton;
    d->pb_refresh_->setIcon(QIcon(":/connecting.png"));
    auto lineLayout = new QHBoxLayout;
    lineLayout->addWidget(d->combo_ports_);
    lineLayout->addWidget(d->pb_refresh_);
    lineLayout->setStretch(0, 1);

    auto layout = new QFormLayout(this);
    layout->addRow("Serial Port", lineLayout);

    connect(d->pb_refresh_, &QPushButton::clicked,
        this, &SerialPortConnectWidget::serialPortRefreshRequested);

    connect(d->combo_ports_, qOverload<int>(&QComboBox::currentIndexChanged),
        this, [this] (int idx)
        {
            emit serialPortChanged(getSelectedPortName());
        });
}

SerialPortConnectWidget::~SerialPortConnectWidget()
{
}

QString SerialPortConnectWidget::getSelectedPortName() const
{
    return d->combo_ports_->currentData().toString();
}

void SerialPortConnectWidget::setAvailablePorts(const QList<QSerialPortInfo> &portInfos)
{
    if (portInfos == d->prevPorts_)
        return;

    const auto currentPort = getSelectedPortName();

    QSignalBlocker b(d->combo_ports_);

    d->combo_ports_->clear();

    for (const auto &info: portInfos)
    {
        if (!info.serialNumber().isEmpty())
        {
            QString s;

            if (!info.description().isEmpty())
                s = QStringLiteral("%1 - %2 - %3").arg(info.portName()).arg(info.description()).arg(info.serialNumber());
            else
                s = QStringLiteral("%1 - %2").arg(info.portName()).arg(info.serialNumber());

            d->combo_ports_->addItem(s, info.portName());
        }
        else
        {
            d->combo_ports_->addItem(info.portName(), info.portName());
        }
    }

    int idx = 0;

    if (!currentPort.isEmpty())
    {
        idx = d->combo_ports_->findData(currentPort);
        idx = idx >= 0 ? idx : 0;
    }

    if (idx < portInfos.size() && portInfos[idx].serialNumber().isEmpty())
    {
        // The previously selected port does not have a serial number so it cannot
        // be a mesytec device. Look for the first port that has a serial number and
        // select that instead.
        auto it = std::find_if(std::begin(portInfos), std::end(portInfos),
                               [](const QSerialPortInfo &port)
                               { return !port.serialNumber().isEmpty(); });
        if (it != std::end(portInfos))
            idx = it - std::begin(portInfos);
    }

    d->combo_ports_->setCurrentIndex(idx);
    d->prevPorts_ = portInfos;
    emit serialPortChanged(getSelectedPortName());
}

}
