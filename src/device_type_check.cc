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

bool check_device_type_match(const QString &otp_devicetype, const FirmwarePart &part)
{
    if (part.get_section() == constants::otp_section)
    {
        // To be able to initially program the OTP we have to skip the check
        // here as the device does not have a type yet. Chicken and egg problem.
        return true;
    }

    auto deviceType = translate_device_type(otp_devicetype); // e.g. "MDPP-32" -> "MDPP32"

    if (!part.has_base())
        return false;

    // Partbase if the basename of the firmware part.
    auto partBase = part.get_base();

    // Compare the translated device type against the start of the OTP string
    // data, up until the first underscore.

    if (auto underscorePos = partBase.indexOf('_'); underscorePos != -1)
    {
        partBase = partBase.left(underscorePos);
    }

    return deviceType.toLower().trimmed() == partBase.toLower().trimmed();
}

bool check_device_type_match(
    const QString &otpDeviceType,
    const FirmwareArchive &firmware,
    std::function<void (const QString &)> logger)
{
    auto deviceType = translate_device_type(otpDeviceType.trimmed()); // e.g. "MDPP-32" -> "MDPP32"
    size_t matchesFound = 0;

    for (const auto &part: firmware.get_area_specific_parts())
    {
      if (!is_binary_part(part) || !part->has_base())
        continue;

        auto partBase = part->get_base();
        if (!check_device_type_match(otpDeviceType, *part))
        {
            if (logger)
            {
                logger(QSL("Firmware '%1' does not match target device type '%2'! Ignoring.")
                    .arg(partBase).arg(deviceType));
            }
            continue;
        }
        matchesFound++;
    }

    for (const auto &part: firmware.get_non_area_specific_parts())
    {
        if (!part->has_base())
            continue;

        auto partBase = part->get_base();
        if (!check_device_type_match(otpDeviceType, *part))
        {
            if (logger)            {
                logger(QSL("Firmware '%1' does not match target device type '%2'! Ignoring.")
                    .arg(partBase).arg(deviceType));
            }
            continue;
        }
        matchesFound++;
    }

    return matchesFound > 0;
}

}
