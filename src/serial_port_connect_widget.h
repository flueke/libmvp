#ifndef EXTERNAL_LIBMVP_SRC_SERIAL_PORT_CONNECT_WIDGET_H
#define EXTERNAL_LIBMVP_SRC_SERIAL_PORT_CONNECT_WIDGET_H

#include <memory>
#include <QWidget>

namespace mesytec::mvp
{

class SerialPortConnectWidget: public QWidget
{
    Q_OBJECT
    signals:
        void serialPortRefreshRequested();

    public:
        SerialPortConnectWidget(QWidget *parent = nullptr);
        ~SerialPortConnectWidget() override;

        QString getSelectedPortName() const;

    public slots:
        void setAvailablePorts(const QStringList &ports);

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

}

#endif // EXTERNAL_LIBMVP_SRC_SERIAL_PORT_CONNECT_WIDGET_H
