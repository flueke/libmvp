#ifndef ADA780E0_E8D0_41FE_9471_40EE0EA4706F
#define ADA780E0_E8D0_41FE_9471_40EE0EA4706F

#include <QMap>
#include <QString>
#include "firmware.h"

namespace mesytec::mvp
{

// Maps OTP device types to device type strings used for matching against
// firmware filenames.
// This enables device type workarounds for devices where a simple prefix match
// does not suffice. E.g. MDPP-32 contains a '-' in the device type, VMMR8 uses
// the VMMR16 firmware. This smoothes out those differences.
using DeviceTypeTranslationTable = QMap<QString, QString>;

const DeviceTypeTranslationTable &get_device_type_translation_table();
QString translate_device_type(const QString &otpDeviceType);

bool check_device_type_match(
    const QString &otpDeviceType,
    const FirmwareArchive &firmware,
    std::function<void (const QString &)> logger = {});

}

#endif /* ADA780E0_E8D0_41FE_9471_40EE0EA4706F */
