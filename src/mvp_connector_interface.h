#ifndef EXTERNAL_LIBMVP_SRC_MVP_CONNECTOR_INTERFACE_H_
#define EXTERNAL_LIBMVP_SRC_MVP_CONNECTOR_INTERFACE_H_

#include <QObject>
#include "flash.h"

namespace mesytec::mvp
{

class MvpConnectorInterface: public QObject
{
    Q_OBJECT
    signals:
        void connectorEnabledChanged(bool enabled);
        void logMessage(const QString &msg);

    public:
        MvpConnectorInterface(QObject *parent = nullptr)
            : QObject(parent)
        {}
        virtual ~MvpConnectorInterface();

        virtual void open() = 0;
        virtual void close() = 0;
        // Note: ownership of the flash
        virtual FlashInterface *getFlash() = 0; // owned by MvpConnectorInterface as a QObject child

    public slots:
        virtual void setConnectInfo(const QVariantMap &info) = 0;
        virtual void setConnectorEnabled(bool enabled)
        {
            if (enabled != connectorEnabled_)
            {
                if (!enabled)
                    close();
                connectorEnabled_ = enabled;
                emit connectorEnabledChanged(connectorEnabled_);
            }
        }

    private:
        bool connectorEnabled_ = true;
};

}

#endif // EXTERNAL_LIBMVP_SRC_MVP_CONNECTOR_INTERFACE_H_
