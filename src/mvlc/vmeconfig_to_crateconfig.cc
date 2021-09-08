#include "mvlc/vmeconfig_to_crateconfig.h"
#include "vme_config_scripts.h"

namespace mesytec
{
namespace mvme
{

mvlc::StackCommand convert_command(const vme_script::Command &srcCmd)
{
    using namespace vme_script;
    using mvlcCT = mesytec::mvlc::StackCommand::CommandType;

    mesytec::mvlc::StackCommand dstCmd;

    switch (srcCmd.type)
    {
        case CommandType::Read:
        case CommandType::ReadAbs:
            dstCmd.type = mvlcCT::VMERead;
            dstCmd.address = srcCmd.address;
            dstCmd.amod = srcCmd.addressMode;
            dstCmd.dataWidth = (srcCmd.dataWidth == DataWidth::D16
                                ? mesytec::mvlc::VMEDataWidth::D16
                                : mesytec::mvlc::VMEDataWidth::D32);
            break;

        case CommandType::Write:
        case CommandType::WriteAbs:
            dstCmd.type = mvlcCT::VMEWrite;
            dstCmd.address = srcCmd.address;
            dstCmd.value = srcCmd.value;
            dstCmd.amod = srcCmd.addressMode;
            dstCmd.dataWidth = (srcCmd.dataWidth == DataWidth::D16
                                ? mesytec::mvlc::VMEDataWidth::D16
                                : mesytec::mvlc::VMEDataWidth::D32);
            break;

        case CommandType::Wait:
            dstCmd.type = mvlcCT::SoftwareDelay;
            dstCmd.value = srcCmd.delay_ms;
            break;

        case CommandType::Marker:
            dstCmd.type = mvlcCT::WriteMarker;
            dstCmd.value = srcCmd.value;
            break;

        case CommandType::BLT:
        case CommandType::BLTFifo:
            dstCmd.type = mvlcCT::VMERead;
            dstCmd.amod = mesytec::mvlc::vme_amods::BLT32;
            dstCmd.address = srcCmd.address;
            dstCmd.transfers = srcCmd.transfers;
            break;

        case CommandType::MBLT:
        case CommandType::MBLTFifo:
            dstCmd.type = mvlcCT::VMERead;
            dstCmd.amod = mesytec::mvlc::vme_amods::MBLT64;
            dstCmd.address = srcCmd.address;
            dstCmd.transfers = srcCmd.transfers;
            break;

        case CommandType::MBLTSwapped:
            dstCmd.type = mvlcCT::VMEMBLTSwapped;
            dstCmd.amod = mesytec::mvlc::vme_amods::MBLT64;
            dstCmd.address = srcCmd.address;
            dstCmd.transfers = srcCmd.transfers;
            break;

        case CommandType::MVLC_WriteSpecial:
            dstCmd.type = mvlcCT::WriteSpecial;
            dstCmd.value = srcCmd.value;
            break;

        case CommandType::MVLC_Custom:
            dstCmd.type = mvlcCT::Custom;
            dstCmd.transfers = srcCmd.transfers;
            for (u32 value: srcCmd.mvlcCustomStack)
                dstCmd.customValues.push_back(value);

            break;


        case CommandType::SetBase:
        case CommandType::ResetBase:
        case CommandType::MetaBlock:
        case CommandType::SetVariable:
        case CommandType::Print:
            break;

        default:
            qDebug() << __PRETTY_FUNCTION__ << "unhandled command type"
                << to_string(srcCmd.type)
                << static_cast<int>(srcCmd.type);
            assert(!"unhandled command type");
            break;
    }

    return dstCmd;
}

std::vector<mvlc::StackCommand> convert_script(const vme_script::VMEScript &contents)
{
    std::vector<mvlc::StackCommand> ret;

    std::transform(
        std::begin(contents), std::end(contents),
        std::back_inserter(ret),
        convert_command);

    return ret;
}

std::vector<mvlc::StackCommand> convert_script(const VMEScriptConfig *script, u32 baseAddress)
{
    return convert_script(parse(script, baseAddress));
}

mvlc::CrateConfig vmeconfig_to_crateconfig(const VMEConfig *vmeConfig)
{
    using namespace vme_script;

    auto add_stack_group = [](
        mesytec::mvlc::StackCommandBuilder &stack, const std::string &groupName,
        const vme_script::VMEScript &contents)
    {
        if (!contents.isEmpty())
        {
            stack.beginGroup(groupName);

            for (const auto &srcCmd: contents)
            {
                if (auto dstCmd = convert_command(srcCmd))
                    stack.addCommand(dstCmd);
            }
        }

        return stack;
    };

    mesytec::mvlc::CrateConfig dstConfig;

    auto ctrlSettings = vmeConfig->getControllerSettings();

    switch (vmeConfig->getControllerType())
    {
        case VMEControllerType::MVLC_ETH:
            dstConfig.connectionType = mesytec::mvlc::ConnectionType::ETH;
            dstConfig.ethHost = ctrlSettings["mvlc_hostname"].toString().toStdString();
            dstConfig.ethJumboEnable = ctrlSettings["mvlc_eth_enable_jumbos"].toBool();
            break;

        case VMEControllerType::MVLC_USB:
            dstConfig.connectionType = mesytec::mvlc::ConnectionType::USB;

            if (ctrlSettings.value("method") == QSL("by_index"))
                dstConfig.usbIndex = ctrlSettings.value("index", "-1").toInt();

            if (ctrlSettings.value("method") == QSL("by_serial"))
                dstConfig.usbSerial = ctrlSettings["serial"].toString().toStdString();

            break;

        default:
            std::cerr << "Warning: mvme config does not use an MVLC VME controller."
                << " Leaving MVLC connection information empty in generated config." << std::endl;
            break;
    }

    const auto eventConfigs = vmeConfig->getEventConfigs();

    // readout stacks
    for (const auto &eventConfig: eventConfigs)
    {
        const auto moduleConfigs = eventConfig->getModuleConfigs();

        mesytec::mvlc::StackCommandBuilder readoutStack(eventConfig->objectName().toStdString());

        add_stack_group(
            readoutStack, "readout_start",
            mesytec::mvme::parse(eventConfig->vmeScripts["readout_start"]));

        for (const auto &moduleConfig: moduleConfigs)
        {
            if (!moduleConfig->isEnabled())
                continue;

            auto moduleName = moduleConfig->objectName().toStdString();

            add_stack_group(
                readoutStack,
                moduleName,
                mesytec::mvme::parse(
                    moduleConfig->getReadoutScript(), moduleConfig->getBaseAddress()));
        }

        add_stack_group(
            readoutStack, "readout_end",
            mesytec::mvme::parse(eventConfig->vmeScripts["readout_end"]));

        dstConfig.stacks.emplace_back(readoutStack);
    }

    // triggers
    for (const auto &eventConfig: eventConfigs)
    {
        using namespace mesytec::mvlc;

        switch (eventConfig->triggerCondition)
        {
            case TriggerCondition::Interrupt:
                {
                    auto triggerType = (eventConfig->triggerOptions["IRQUseIACK"].toBool()
                                        ? stacks::IRQWithIACK
                                        : stacks::IRQNoIACK);
                    dstConfig.triggers.push_back(trigger_value(triggerType, eventConfig->irqLevel));
                }
                break;

            // Note: periodic triggers are implement via the TriggerIO system.
            // This happens as soon as the periodic event is created.
            case TriggerCondition::TriggerIO:
            case TriggerCondition::Periodic:
                dstConfig.triggers.push_back(trigger_value(stacks::External));
                break;

            default:
                std::cerr << "Warning: unhandled trigger type for event '"
                    << eventConfig->objectName().toStdString()
                    << "'. Defaulting to 'TriggerIO'."
                    << std::endl;
                dstConfig.triggers.push_back(trigger_value(stacks::External));
                break;
        }
    }

    // init_trigger_io
    {
        dstConfig.initTriggerIO.setName("init_trigger_io");

        auto script = qobject_cast<VMEScriptConfig *>(
            vmeConfig->getGlobalObjectRoot().findChildByName("mvlc_trigger_io"));

        if (script)
        {
            add_stack_group(
                dstConfig.initTriggerIO,
                {},
                mesytec::mvme::parse(script));
        }
    }

    // init_commands
    // order is global_daq_start, module_init
    // Does not include the event multicast daq start scripts
    dstConfig.initCommands.setName("init_commands");

    // global daq start
    auto startScripts = vmeConfig->getGlobalObjectRoot().findChild<ContainerObject *>(
        "daq_start")->findChildren<VMEScriptConfig *>();

    for (const auto &script: startScripts)
    {
        add_stack_group(
            dstConfig.initCommands,
            script->objectName().toStdString(),
            mesytec::mvme::parse(script));
    }

    // module init
    for (const auto &eventConfig: eventConfigs)
    {
        auto eventName = eventConfig->objectName().toStdString();
        const auto moduleConfigs = eventConfig->getModuleConfigs();

        for (const auto &moduleConfig: moduleConfigs)
        {
            if (!moduleConfig->isEnabled())
                continue;

            auto moduleName = moduleConfig->objectName().toStdString();

            add_stack_group(
                dstConfig.initCommands,
                eventName + "." + moduleName + ".reset",
                mesytec::mvme::parse(
                    moduleConfig->getResetScript(), moduleConfig->getBaseAddress()));

            for (const auto &script: moduleConfig->getInitScripts())
            {
                add_stack_group(
                    dstConfig.initCommands,
                    eventName + "." + moduleName + "." + script->objectName().toStdString(),
                    mesytec::mvme::parse(
                        script, moduleConfig->getBaseAddress()));
            }
        }
    }

    // stop_commands
    // contains onyl the global daq stop commands
    dstConfig.stopCommands.setName("stop_commands");

    auto stopScripts = vmeConfig->getGlobalObjectRoot().findChild<ContainerObject *>(
        "daq_stop")->findChildren<VMEScriptConfig *>();

    for (const auto &script: stopScripts)
    {
        add_stack_group(
            dstConfig.stopCommands,
            script->objectName().toStdString(),
            mesytec::mvme::parse(script));
    }

    // mcst daq start
    dstConfig.mcstDaqStart.setName("mcst_daq_start");

    for (const auto &eventConfig: eventConfigs)
    {
        auto eventName = eventConfig->objectName().toStdString();

        auto script = eventConfig->vmeScripts["daq_start"];

        add_stack_group(
            dstConfig.mcstDaqStart,
            eventName + "." + script->objectName().toStdString(),
            mesytec::mvme::parse(script));
    }

    // mcst daq stop
    dstConfig.mcstDaqStop.setName("mcst_daq_stop");

    for (const auto &eventConfig: eventConfigs)
    {
        auto eventName = eventConfig->objectName().toStdString();

        auto script = eventConfig->vmeScripts["daq_stop"];

        add_stack_group(
            dstConfig.mcstDaqStop,
            eventName + "." + script->objectName().toStdString(),
            mesytec::mvme::parse(script));
    }

    return dstConfig;
}

}
}
