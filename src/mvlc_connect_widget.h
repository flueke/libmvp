#ifndef EXTERNAL_LIBMVP_SRC_MVLC_CONNECT_WIDGET_H_
#define EXTERNAL_LIBMVP_SRC_MVLC_CONNECT_WIDGET_H_

#include <memory>
#include <QWidget>
#include <QVariant>

namespace mesytec::mvp
{

class MvlcConnectWidget: public QWidget
{
    Q_OBJECT
    signals:
        void connectMvlc(const QVariantMap &info);
        void disconnectMvlc();

    public:
        MvlcConnectWidget(QWidget *parent = nullptr);
        ~MvlcConnectWidget() override;

        void setIsConnected(bool isConnected);
        QVariantMap getConnectInfo();

    public slots:
        void setConnectInfo(const QVariantMap &info);

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

}

#endif // EXTERNAL_LIBMVP_SRC_MVLC_CONNECT_WIDGET_H_