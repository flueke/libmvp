#include "device_type_check.h"

//input is:
//- device type read from otp
//- a FirmwareArchive object
//- the static DeviceTypeTranslate table
//
//bool check_device_type_match(const std::string & otpDeviceType

namespace mesytec::mvp
{

const DeviceTypeTranslationTable &get_device_type_translation_table()
{
  static const QMap<QString, QString> DeviceTypeTranslate =
  {
    // OTP device type -> device type for matching against firmware filenames
    //                    (not the package filename but the .bin filename!)
    { "MDPP-32",  "MDPP32" },
    { "VMMR8",    "VMMR16" },
    { "MCPD8",    "MCPD-8" },
  };

  return DeviceTypeTranslate;
}

QString translate_device_type(const QString &deviceType)
{
    return get_device_type_translation_table().value(deviceType.trimmed(), deviceType);
}

bool check_device_type_match(
    const QString &otpDeviceType,
    const FirmwareArchive &firmware,
    std::function<void (const QString &)> logger)
{
    auto deviceType = translate_device_type(otpDeviceType.trimmed()); // e.g. "MDPP-32" -> "MDPP32"

    for (const auto &part: firmware.get_area_specific_parts())
    {
      if (!is_binary_part(part) || !part->has_base())
        continue;

        auto partBase = part->get_base();
        // Prefix match of the firmware part base against the translated device
        // type, both lowercased.
        if (!partBase.toLower().startsWith(deviceType.toLower()))
        {
            if (logger)
            {
                logger(QSL("Firmware '%1' does not match target device type '%2'! Aborting.")
                    .arg(partBase).arg(deviceType));
            }
            return false;
        }
    }

    return true;
}

}
