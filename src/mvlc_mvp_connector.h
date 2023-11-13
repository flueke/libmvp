#ifndef EXTERNAL_LIBMVP_SRC_MVLC_MVP_CONNECTOR_H
#define EXTERNAL_LIBMVP_SRC_MVLC_MVP_CONNECTOR_H

#include "mvp_connector_interface.h"
#include <mesytec-mvlc/util/int_types.h>

namespace mesytec::mvp
{

class MvlcMvpConnector: public MvpConnectorInterface
{
    Q_OBJECT
    signals:
        void scanbusResultReady(const QVariantList &scanbusResult);
        void usbDevicesChanged(const QVariantList &deviceInfos);
        void logMessage(const QString &msg);
        // Emitted if successfully connected to the MVLC specified by the
        // mvlcInfo map.
        void connectedToMVLC(const QVariantMap &mvlcInfo);

    public:
        MvlcMvpConnector(QObject *parent = nullptr);
        ~MvlcMvpConnector() override;
        void open() override;
        void close() override;
        FlashInterface *getFlash() override;
        QVariantList scanbus();

    public slots:
        void setConnectInfo(const QVariantMap &info) override;
        void refreshUsbDevices();

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

// Map has the same structure as a scanbus result entry:
// m["address"], m["hwId"], ["fwId"], m["module_type"], m["firmware_type"]
// Returns true if the module can be flashed. Otherwise returns false and stores
// a diagnostic message in the result string member.
std::pair<bool, std::string> can_flash_through_vme(const QVariantMap &deviceInfo);
std::pair<bool, std::string> can_flash_through_vme(mvlc::u32 hwId, mvlc::u32 fwId);

}

#endif // EXTERNAL_LIBMVP_SRC_MVLC_MVP_CONNECTOR_H
