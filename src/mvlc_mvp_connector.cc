#include "mvlc_mvp_connector.h"

#include "mvlc_mvp_flash.h"

namespace mesytec::mvp
{

struct MvlcMvpConnector::Private
{
    mvlc::MVLC mvlc_;
    MvlcMvpFlash *flash_;
    QVariantMap connectInfo_;
};

MvlcMvpConnector::MvlcMvpConnector(QObject *parent)
    : MvpConnectorInterface(parent)
    , d(std::make_unique<Private>())
{
    d->flash_ = new MvlcMvpFlash(this);
}

MvlcMvpConnector::~MvlcMvpConnector()
{
}

void MvlcMvpConnector::open()
{
}

void MvlcMvpConnector::close()
{
}

FlashInterface *MvlcMvpConnector::getFlash()
{
    return d->flash_;
}

void MvlcMvpConnector::setConnectInfo(const QVariantMap &info)
{
    d->connectInfo_ = info;
}

}
