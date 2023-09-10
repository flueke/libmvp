#ifndef EXTERNAL_LIBMVP_SRC_MVP_CONNECTOR_INTERFACE_H_
#define EXTERNAL_LIBMVP_SRC_MVP_CONNECTOR_INTERFACE_H_

#include <QObject>
#include "flash.h"

namespace mesytec::mvp
{

class MvpConnectorInterface: public QObject
{
    Q_OBJECT
        void connectorEnabledChanged();

    public:
        MvpConnectorInterface(QObject *parent = nullptr)
            : QObject(parent)
        {}
        virtual ~MvpConnectorInterface();

        virtual FlashInterface *getFlash() = 0;

    public slots:
        virtual void setConnectInfo(const QVariantMap &info) = 0;
        virtual void setConnectorEnabled(bool enabled) = 0;
};

}

#endif // EXTERNAL_LIBMVP_SRC_MVP_CONNECTOR_INTERFACE_H_