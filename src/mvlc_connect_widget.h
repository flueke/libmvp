#ifndef EXTERNAL_LIBMVP_SRC_MVLC_CONNECT_WIDGET_H_
#define EXTERNAL_LIBMVP_SRC_MVLC_CONNECT_WIDGET_H_

#include <memory>
#include <QWidget>
#include <QVariant>

namespace mesytec::mvp
{

// Note: connection info is stored in QVariantMaps. Structure of the map:
//   method: "eth"|"usb"
//   address: eth host/ip
//   index: usb device index
//   serial: usb device serial string
//   description: usb device description
//   vme_address: target vme address

class MvlcConnectWidget: public QWidget
{
    Q_OBJECT
    signals:
        // Explicit connection request via the connect button. Can be used to
        // verify the connection works.
        void connectMvlc(const QVariantMap &info);

        // GUI changes to the connection info, e.g. by editing some combo box
        // text or selecting a usb device.
        void mvlcConnectInfoChanged(const QVariantMap &info);
        void scanbusRequested();
        void usbRefreshRequested();
        void logMessage(const QString &msg);

    public:
        MvlcConnectWidget(QWidget *parent = nullptr);
        ~MvlcConnectWidget() override;

        QVariantMap getConnectInfo();

    public slots:
        void setConnectInfo(const QVariantMap &info);
        void setScanbusResult(const QVariantList &scanbusResult);
        void setUsbDevices(const QVariantList &usbDevices);
        void mvlcSuccessfullyConnected(const QVariantMap &info);

    private slots:
        void onConnectButtonClicked();
        void onConnectInfoChangedInWidget();

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

QString get_mvlc_connect_info_title(const QVariantMap &info);

}

#endif // EXTERNAL_LIBMVP_SRC_MVLC_CONNECT_WIDGET_H_
