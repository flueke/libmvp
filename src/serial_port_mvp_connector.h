#ifndef EXTERNAL_LIBMVP_SRC_SERIAL_PORT_MVP_CONNECTOR_H
#define EXTERNAL_LIBMVP_SRC_SERIAL_PORT_MVP_CONNECTOR_H

#include "mvp_connector_interface.h"

namespace mesytec::mvp
{

class PortHelper;

class SerialPortMvpConnector: public MvpConnectorInterface
{
    Q_OBJECT
    public:
        SerialPortMvpConnector(QObject *parent = nullptr);
        ~SerialPortMvpConnector() override;

        void open() override;
        void close() override;
        FlashInterface *getFlash() override;
        PortHelper *getPortHelper();

    public slots:
        void setConnectInfo(const QVariantMap &info) override;

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

}

#endif // EXTERNAL_LIBMVP_SRC_SERIAL_PORT_MVP_CONNECTOR_H
