#ifndef EXTERNAL_LIBMVP_SRC_MVLC_MVP_CONNECTOR_H
#define EXTERNAL_LIBMVP_SRC_MVLC_MVP_CONNECTOR_H

#include "mvp_connector_interface.h"

namespace mesytec::mvp
{

class MvlcMvpConnector: public MvpConnectorInterface
{
    Q_OBJECT
    public:
        MvlcMvpConnector(QObject *parent = nullptr);
        ~MvlcMvpConnector() override;
        void open() override;
        void close() override;
        FlashInterface *getFlash() override;

    public slots:
        void setConnectInfo(const QVariantMap &info) override;

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

}

#endif // EXTERNAL_LIBMVP_SRC_MVLC_MVP_CONNECTOR_H
