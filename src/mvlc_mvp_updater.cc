#include <filesystem>
#include <mesytec-mvlc/scanbus_support.h>
#include <mesytec-mvlc/util/string_util.h>
#include <mvlc_mvp_lib.h>
#include <mvlc_mvp_flash.h>
#include <git_version.h>

using namespace mesytec::mvlc;
using namespace mesytec::mvp;

// Parse a string to type T using the supplied converter function.
// Converter signature is 'T converter(const std::string &str)'
template<typename T, typename Converter>
bool parse_into(argh::parser &parser, const std::string &param, T &dest, Converter conv)
{
    std::string str;
    if (parser(param) >> str)
    {
        try
        {
             dest = conv(str);
             if (errno == ERANGE)
             {
                std::cerr << fmt::format("Error: value given to {} is out of range\n", param);
                return false;
             }
        }
        catch (const std::exception &e)
        {
            std::cerr << fmt::format("Error: could not parse value given to {}: {}\n", param, e.what());
            return false;
        }
    }

    return true;
}

unsigned long convert_to_unsigned(const std::string &str)
{
    return std::stoul(str, nullptr, 0);
}

using namespace mesytec;
using namespace mesytec::mvlc;

std::pair<MVLC, std::error_code> make_and_connect_default_mvlc(argh::parser &parser)
{
    // try the standard params first
    auto mvlc = make_mvlc_from_standard_params(parser);

    if (!mvlc)
    {
        std::cerr << "Error: no MVLC connection specified.\n";
        return std::pair<MVLC, std::error_code>();
    }

    auto ec = mvlc.connect();

    if (ec)
    {
        std::cerr << fmt::format("Error connecting to MVLC {}: {}\n",
            mvlc.connectionInfo(), ec.message());
    }

    return std::make_pair(mvlc, ec);
}

struct CliContext;
struct Command;

#define DEF_EXEC_FUNC(name) int name(CliContext &ctx, const Command &self, int argc, const char **argv)

using Exec = std::function<DEF_EXEC_FUNC()>;

struct Command
{
  std::string name ;
  std::string help;
  Exec exec;
};

struct CommandNameLessThan
{
    bool operator()(const Command &first, const Command &second) const
    {
        return first.name < second.name;
    }
};

using Commands = std::set<Command, CommandNameLessThan> ;

struct CliContext
{
    Commands commands;
    argh::parser parser;
};

// ========================================================================
// Command implementations start here
// ========================================================================

DEF_EXEC_FUNC(list_commands_command)
{
    (void) self; (void) argc; (void) argv;
    spdlog::trace("entered list_commands_command()");
    trace_log_parser_info(ctx.parser, "list_commands_command");

    for (const auto &cmd: ctx.commands)
    {
        std::cout << cmd.name << "\n";
    }

    return 0;
}

static const Command ListCmdsCommand =
{
    .name = "list-commands",
    .help = unindent(R"~(
List all registered commands.
)~"),
    .exec = list_commands_command,
};

DEF_EXEC_FUNC(scanbus_command)
{
    (void) self; (void) argc; (void) argv;
    spdlog::trace("entered mvlc_scanbus_command()");

    auto parser = ctx.parser;
    parser.add_params({"--scan-begin", "--scan-end", "--probe-register", "--probe-amod", "--probe-datawidth"});
    parser.parse(argv);
    trace_log_parser_info(parser, "mvlc_scanbus_command");

    u16 scanBegin = 0x0u;
    u16 scanEnd   = 0xffffu;
    u16 probeRegister = 0;
    u8  probeAmod = 0x09;
    VMEDataWidth probeDataWidth = VMEDataWidth::D16;
    std::string str;

    if (parser("--scan-begin") >> str)
    {
        try { scanBegin = std::stoul(str, nullptr, 0); }
        catch (const std::exception &e)
        {
            std::cerr << "Error: could not parse address value for --scan-begin\n";
            return 1;
        }
    }

    if (parser("--scan-end") >> str)
    {
        try { scanEnd = std::stoul(str, nullptr, 0); }
        catch (const std::exception &e)
        {
            std::cerr << "Error: could not parse address value for --scan-end\n";
            return 1;
        }
    }

    if (parser("--probe-register") >> str)
    {
        try { probeRegister = std::stoul(str, nullptr, 0); }
        catch (const std::exception &e)
        {
            std::cerr << "Error: could not parse address value for --probe-register\n";
            return 1;
        }
    }

    if (parser("--probe-amod") >> str)
    {
        try { probeAmod = std::stoul(str, nullptr, 0); }
        catch (const std::exception &e)
        {
            std::cerr << "Error: could not parse value for --probe-amod\n";
            return 1;
        }
    }

    if (parser("--probe-datawidth") >> str)
    {
        auto dwStr = str_tolower(str);

        if (dwStr == "d16" || dwStr == "16")
            probeDataWidth = VMEDataWidth::D16;
        else if (dwStr == "d32" || dwStr == "32")
            probeDataWidth = VMEDataWidth::D32;
        else
        {
            std::cerr << fmt::format("Error: invalid --probe-datawidth given: {}\n", str);
            return 1;
        }

        str.clear();
    }

    if (scanEnd < scanBegin)
        std::swap(scanEnd, scanBegin);

    auto [mvlc, ec] = make_and_connect_default_mvlc(ctx.parser);

    if (!mvlc || ec)
        return 1;

    std::cout << fmt::format("scanbus scan range: [{:#06x}, {:#06x}), {} addresses, probeRegister={:#06x}"
        ", probeAmod={:#04x}, probeDataWidth={}\n",
        scanBegin, scanEnd, scanEnd - scanBegin + 1, probeRegister,
        probeAmod, probeDataWidth == VMEDataWidth::D16 ? "d16" : "d32");

    using namespace scanbus;

    auto candidates = scan_vme_bus_for_candidates(mvlc, scanBegin, scanEnd,
        probeRegister, probeAmod, probeDataWidth);

    if (!candidates.empty())
    {
        if (candidates.size() == 1)
            std::cout << fmt::format("Found {} module candidate address: {:#010x}\n",
                candidates.size(), fmt::join(candidates, ", "));
        else
            std::cout << fmt::format("Found {} module candidate addresses: {:#010x}\n",
                candidates.size(), fmt::join(candidates, ", "));

        for (auto addr: candidates)
        {
            VMEModuleInfo moduleInfo{};

            if (auto ec = mvlc.vmeRead(addr + FirmwareRegister, moduleInfo.fwId, vme_amods::A32, VMEDataWidth::D16))
            {
                std::cout << fmt::format("Error checking address {#:010x}: {}\n", addr, ec.message());
                continue;
            }

            if (auto ec = mvlc.vmeRead(addr + HardwareIdRegister, moduleInfo.hwId, vme_amods::A32, VMEDataWidth::D16))
            {
                std::cout << fmt::format("Error checking address {#:010x}: {}\n", addr, ec.message());
                continue;
            }

            if (moduleInfo.hwId == 0 && moduleInfo.fwId == 0)
            {
                if (auto ec = mvlc.vmeRead(addr + MVHV4FirmwareRegister, moduleInfo.fwId, vme_amods::A32, VMEDataWidth::D16))
                {
                    std::cout << fmt::format("Error checking address {#:010x}: {}\n", addr, ec.message());
                    continue;
                }

                if (auto ec = mvlc.vmeRead(addr + MVHV4HardwareIdRegister, moduleInfo.hwId, vme_amods::A32, VMEDataWidth::D16))
                {
                    std::cout << fmt::format("Error checking address {#:010x}: {}\n", addr, ec.message());
                    continue;
                }
            }

            auto msg = fmt::format("Found module at {:#010x}: hwId={:#06x}, fwId={:#06x}, type={}",
                addr, moduleInfo.hwId, moduleInfo.fwId, moduleInfo.moduleTypeName());

            if (is_mdpp(moduleInfo.hwId))
                msg += fmt::format(", mdpp_fw_type={}", moduleInfo.mdppFirmwareTypeName());

            std::cout << fmt::format("{}\n", msg);
        }
    }
    else
        std::cout << fmt::format("scanbus did not find any mesytec VME modules\n");

    return 0;
}

static const Command ScanbusCommand
{
    .name = "scanbus",
    .help = unindent(R"~(
Usage: scanbus [--scan-begin=<addr>] [--scan-end=<addr>] [--probe-register=<addr>]
               [--probe-amod=<amod>] [--probe-datawidth=<datawidth>]

    Scans the upper 16 bits of the VME address space for the presence of mesytec VME modules.
    Displays the hardware and firmware revisions of found modules and additionally the loaded
    firmware type for MDPP-style modules.

Options:
    --scan-begin=<addr> (default=0x0000)
        16-bit start address for the scan.

    --scan-end=<addr> (default=0xffff)
        16-bit one-past-end address for the scan.

    --probe-register=<addr> (default=0)
        The 16-bit register address to read from.

    --probe-amod=<amod> (default=0x09)
        The VME amod to use when reading the probe register.

    --probe-datawidth=(d16|16|d32|32) (default=d16)
        VME datawidth to use when reading the probe register.
)~"),
    .exec = scanbus_command,
};

DEF_EXEC_FUNC(dump_memory_command)
{
    (void) self; (void) argc; (void) argv;
    spdlog::trace("entered dump_memory_command()");
    u32 vmeAddress = 0x0u;
    unsigned area = 0;
    unsigned section = 0;
    unsigned memAddress = 0;
    size_t len = mesytec::mvp::constants::page_size;
    std::string str;

    auto parser = ctx.parser;
    parser.add_params({"--vme-address", "--area", "--section", "--mem-address", "--len"});
    parser.parse(argv);
    trace_log_parser_info(parser, "dump_memory_command");

    if (!parse_into(parser, "--vme-address", vmeAddress, convert_to_unsigned))
        return 1;

    if (!parse_into(parser, "--area", area, convert_to_unsigned))
        return 1;

    if (!parse_into(parser, "--section", section, convert_to_unsigned))
        return 1;

    if (!parse_into(parser, "--mem-address", memAddress, convert_to_unsigned))
        return 1;

    if (!parse_into(parser, "--len", len, convert_to_unsigned))
        return 1;

    std::cout << fmt::format("dump_memory: vmeAddress=0x{:08x}, area={}, memAddress=0x{:08x}, section={}, len={}\n",
        vmeAddress, area, memAddress, section, len);

    auto [mvlc, ec] = make_and_connect_default_mvlc(ctx.parser);

    if (!mvlc || ec)
        return 1;

    std::vector<u8> memDest;

    if (auto ec = read_flash_memory(mvlc, vmeAddress, area, memAddress, section, len, memDest))
    {
        std::cout << fmt::format("Error reading flash memory from vme address 0x{:08x}: {}\n",
            vmeAddress, ec.message());
        return 1;
    }

    log_page_buffer(memDest);

    return 0;
}

static const Command DumpMemoryCommand
{
    .name = "dump-memory",
    .help = unindent(R"~(
Usage: dump-memory --vme-address=<addr> --area=<area_index> --setion=<sec> --mem-address=<mem_addr> --len=<len>

    Dumps the specified flash memory range to stdout.

Options:
    --vme-address=<addr> (default=0x0)
        32-bit VME address of the target device

    --area=<area_index> (default=0)
        Flash area index to read from. Valid values in [0, 3]

    --section=<sec> (default=0)
        Flash section to read from. Valid values in [0, 3] and [8, 12]

    --mem-address=addr> (default=0)
        24-bit flash address to start reading from.

    --len=<len> (default=256)
        Length in bytes to read.

)~"),
    .exec = dump_memory_command,
};

DEF_EXEC_FUNC(write_firmware_command)
{
    (void) self; (void) argc; (void) argv;
    spdlog::trace("entered write_firmware_command()");

    using namespace mesytec::mvp;

    u32 vmeAddress = 0;
    unsigned area = 0;
    std::string firmwareInput;
    bool doErase = true;

    auto parser = ctx.parser;
    parser.add_params({"--vme-address", "--area", "--firmware"});
    parser.parse(argv);
    trace_log_parser_info(parser, "write_firmware_command");

    if (!parse_into(parser, "--vme-address", vmeAddress, convert_to_unsigned))
        return 1;

    if (!parse_into(parser, "--area", area, convert_to_unsigned))
        return 1;

    if (!(parser("--firmware") >> firmwareInput))
    {
        std::cerr << "Error: missing --firmware <file|dir> parameter!\n";
        return 1;
    }

    if (parser["--no-erase"])
        doErase = false;

    mesytec::mvp::FirmwareArchive firmware;
    namespace fs = std::filesystem;

    auto st = fs::status(firmwareInput);
    auto qFirmwareInput = QString::fromStdString(firmwareInput);

    try
    {
        if (st.type() == fs::file_type::directory)
            firmware = mesytec::mvp::from_dir(qFirmwareInput);
        else
        {
            auto ext = str_tolower(fs::path(firmwareInput).extension().string());
            if (ext == ".bin" || ext == ".key" || ext == ".hex")
                firmware = mesytec::mvp::from_single_file(qFirmwareInput);
            else
                firmware = mesytec::mvp::from_zip(qFirmwareInput);
        }

        if (firmware.is_empty())
        {
            std::cerr << "Error: empty firmware data from " << firmwareInput << "\n";
            return 1;
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << fmt::format("Error reading firmware from {}: {}\n", firmwareInput, e.what());
        return 1;
    }

    auto [mvlc, ec] = make_and_connect_default_mvlc(ctx.parser);

    if (!mvlc || ec)
        return 1;

    try
    {
        MvlcMvpFlash flash(mvlc, vmeAddress);
        mesytec::mvp::FirmwareWriter writer(firmware, &flash);
        writer.set_do_erase(doErase);
        int maxProgress = 0u;

        QObject::connect(&flash, &mesytec::mvp::FlashInterface::progress_range_changed, [&maxProgress] (int min, int max) {
            //std::cout << fmt::format("FlashInterface::progressRange: [{}, {}]\n", min, max);
            maxProgress = max;
        });

        QObject::connect(&flash, &mesytec::mvp::FlashInterface::progress_changed, [&maxProgress] (int progress) {
            std::cout << fmt::format("FlashInterface::progress: {}/{}\n", progress, maxProgress);
        });

        QObject::connect(&flash, &mesytec::mvp::FlashInterface::progress_text_changed, [] (const QString &txt) {
            std::cout << fmt::format("FlashInterface::progressText: {}\n", txt.toStdString());
        });

        QObject::connect(&flash, &mesytec::mvp::FlashInterface::statusbyte_received, [] (const u8 &status) {
            std::cout << fmt::format("FlashInterface::statusbyte: 0x{:02x}\n", status);
        });

        QObject::connect(&writer, &mesytec::mvp::FirmwareWriter::status_message, [] (const QString &msg) {
            std::cout << "FirmwareWriter status: " << msg.toStdString() << "\n";
        });

        writer.write();
    } catch (const std::exception &e)
    {
        std::cerr << fmt::format("Error writing firmware to VME address 0x{:08x}: {}\n", vmeAddress, e.what());
        return 1;
    }

    return 0;
}

static const Command WriteFirmwareCommand
{
    .name = "write-firmware",
    .help = unindent(R"~(
Usage: write-firmware --firmware=<file|dir> [--vme-address=<addr>] [--area=<area>]

    Writes the given MVP firmware package/file to the specified destination device and area.

Options:
    --firmware=<file|dir>
        Path to the input file or directory. Usually a *.mvp file but can also be single *.bin or *.hex files.

    --vme-address=<addr>
        32-bit VME address of the target device. Must be an MDPP-style device supporting the MVP protocol.

    --area=<area>
        Flash area to write the firmware to. Not needed if a *.mvp package is
        used as these usually contain the target area encoded in the contained filenames.

    --no-erase
        If specified the target flash sections will not be erased prior to writing.

)~"),
    .exec = write_firmware_command,
};

DEF_EXEC_FUNC(boot_module_command)
{
    (void) self; (void) argc; (void) argv;
    spdlog::trace("entered boot_module_command()");

    using namespace mesytec::mvp;

    u32 vmeAddress = 0;
    unsigned area = 0;

    auto parser = ctx.parser;
    parser.add_params({"--vme-address", "--area"});
    parser.parse(argv);
    trace_log_parser_info(parser, "boot_module_command");

    if (!parse_into(parser, "--vme-address", vmeAddress, convert_to_unsigned))
        return 1;

    if (!parse_into(parser, "--area", area, convert_to_unsigned))
        return 1;

    auto [mvlc, ec] = make_and_connect_default_mvlc(ctx.parser);

    if (!mvlc || ec)
        return 1;

    try
    {
        MvlcMvpFlash flash(mvlc, vmeAddress);
        flash.boot(area);
    } catch (const std::exception &e)
    {
        // Note: ignoring errors here as the module immediately boots without
        // sending a response.  Side effect is that VME-level errors from the
        // MVLC like 'No VME Response' are also suppressed :(
    }

    std::cout << fmt::format("Sent boot command to VME module 0x{:08x} (area={})\n", vmeAddress, area);

    return 0;
}

static const Command BootModuleCommand
{
    .name = "boot-module",
    .help = unindent(R"~(
Usage: boot-module --vme-address=<addr> --area=<area>

    Boot the target module into the specified flash area.

Options:
    --vme-address=<addr> (default=0x0)
        32-bit VME address of the target device. Must be an MDPP-style device supporting the MVP protocol.

    --area=<area> (default=0)
        Flash area to boot into. Range [0,3].
)~"),
    .exec = boot_module_command,
};

inline Command make_command(const std::string &name)
{
    Command ret;
    ret.name = name;
    return ret;
}

static const std::string GeneralHelp = unindent(R"~(
Command line mesytec (VME) firmware updater - MVLC VME version.

Uses the mesytec MVLC VME controller to issue firmware update and related
commands to mesytec MDPP-style VME modules.

Usage: mvlc_mvp_updater [-v | --version] [-h | --help [-a]] [--log-level=(off|error|warn|info|debug|trace)]
                        [--mvlc <url> | --mvlc-usb | --mvlc-usb-index <index> |
                         --mvlc-usb-serial <serial> | --mvlc-eth <hostname>]
                        <command> [<args>]

Core Commands:
    help <command>
        Show help for the given command and exit.

    list-commands | help -a
        Print list of available commands.

Core Switches:
    -v | --version
        Show mvlc-cli and mesytec-mvlc versions.

    -h <command> | --help <command>
        Show help for the given command and exit.

    -h -a | --help -a
        Same as list-commands: print a list of available commands.

MVLC connection URIs:

    mvlc-cli supports the following URI schemes with --mvlc <uri> to connect to MVLCs:
        usb://                   Use the first USB device
        usb://<serial-string>    USB device matching the given serial number
        usb://@<index>           USB device with the given logical FTDI driver index
        eth://<hostname|ip>      ETH/UDP with a hostname or an ip-address
        udp://<hostname|ip>      ETH/UDP with a hostname or an ip-address
        hostname                 No scheme part -> interpreted as a hostname for ETH/UDP

    Alternatively the transport specific options --mvlc-usb, --mvlc-usb-index,
    --mvlc-usb-serial and --mvlc-eth may be used.

    If none of the above is given MVLC_ADDRESS from the environment is used as
    the MVLC URI. Use e.g. `export MVLC_ADDRESS=usb://` to connect to the first
    MVLC USB device.
)~");

int main(int argc, char *argv[])
{
    spdlog::set_level(spdlog::level::info);
    mesytec::mvlc::set_global_log_level(spdlog::level::info);

    argh::parser parser({"-h", "--help", "--log-level"});
    add_mvlc_standard_params(parser);
    parser.parse(argv);

    {
        std::string logLevelName;
        if (parser("--log-level") >> logLevelName)
            logLevelName = str_tolower(logLevelName);
        else if (parser["--trace"])
            logLevelName = "trace";
        else if (parser["--debug"])
            logLevelName = "debug";

        if (!logLevelName.empty())
        {
            auto level = spdlog::level::from_str(logLevelName);
            if (level == spdlog::level::off && logLevelName != "off")
            {
                std::cerr << fmt::format("Error: invalid spdlog level name '{}'.\n", logLevelName);
                return 1;
            }
            spdlog::set_level(level);
        }
    }

    CliContext ctx;
    ctx.parser = parser;
    ctx.commands.insert(ListCmdsCommand);
    ctx.commands.insert(ScanbusCommand);
    ctx.commands.insert(DumpMemoryCommand);
    ctx.commands.insert(WriteFirmwareCommand);
    ctx.commands.insert(BootModuleCommand);

    {
        std::string cmdName;
        if ((parser({"-h", "--help"}) >> cmdName))
        {
            if (auto cmd = ctx.commands.find(make_command(cmdName));
                cmd != ctx.commands.end())
            {
                std::cout << cmd->help;
                return 0;
            }

            std::cerr << fmt::format("Error: no such command '{}'\n"
                                     "Use 'mvlc-cli list-commands' to get a list of commands\n",
                                     cmdName);
            return 1;
        }
    }

    if (auto cmdName = parser[1]; !cmdName.empty())
    {
        if (auto cmd = ctx.commands.find(make_command(parser[1]));
            cmd != ctx.commands.end())
        {
            spdlog::trace("parsed cli: found cmd='{}'", cmd->name);
            if (parser[{"-h", "--help"}])
            {
                spdlog::trace("parsed cli: found -h flag for command {}, directly displaying help text",
                    cmd->name);
                std::cout << cmd->help;
                return 0;
            }

            spdlog::trace("parsed cli: executing cmd='{}'", cmd->name);
            return cmd->exec(ctx, *cmd, argc, const_cast<const char **>(argv));
        }
        else
        {
            std::cerr << fmt::format("Error: no such command '{}'\n", parser[1]);
            return 1;
        }
    }

    assert(parser[1].empty());

    if (parser[{"-h", "--help"}])
    {
        if (parser["-a"])
            return ListCmdsCommand.exec(ctx, ListCmdsCommand, argc, const_cast<const char **>(argv));
        std::cout << GeneralHelp;
        return 0;
    }

    if (parser[{"-v", "--version"}])
    {
        std::cout << fmt::format("mvlc_mvp_updater - version {}\n", mesytec::mvp::library_version());
        std::cout << fmt::format("mesytec-mvlc     - version {}\n", mesytec::mvlc::library_version());
        return 0;
    }

    return 0;
}
