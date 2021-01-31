/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2020 mesytec GmbH & Co. KG <info@mesytec.com>
 *
 * Author: Florian Lüke <f.lueke@mesytec.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#include "mvlc/mvlc_trigger_io_script.h"

#include <boost/range/adaptor/indexed.hpp>
#include <boost/variant.hpp>
#include <QDebug>
#include <QMap>
#include <QVector>
#include <yaml-cpp/yaml.h>

#include "template_system.h"
#include "vme_script.h"

using boost::adaptors::indexed;

namespace mesytec
{
namespace mvme_mvlc
{
namespace trigger_io
{

struct Write
{
    // Opt_HexValue indicates that the register value should be printed in
    // hexadecimal instead of decimal.
    static const unsigned Opt_HexValue = 1u << 0;;

    // Opt_BinValue indicates that the register value should be printed in
    // binary (0bxyz literatl) instead of decimal.
    static const unsigned Opt_BinValue = 1u << 1;

    // Relative register address. Only the low two bytes are stored.
    u16 address;

    // 16 bit MVLC register value.
    u16 value;

    // Comment written one the same line as the write.
    QString comment;

    // OR of the Opt_* constants defined above.
    unsigned options = 0u;

    Write() = default;

    Write(u16 address_, u16 value_, const QString &comment_ = {}, unsigned options_ = 0u)
        : address(address_)
        , value(value_)
        , comment(comment_)
        , options(options_)
    {}

    Write(u16 address_, u16 value_, unsigned options_)
        : address(address_)
        , value(value_)
        , options(options_)
    {}
};

// Variant containing either a register write or a block comment. If the 2nd
// type is set it indicates the start of a new block in the generated script
// text. The following writes will be preceded by and empty line and a comment
// containing the string value on a separate line.
using ScriptPart = boost::variant<Write, QString>;
using ScriptParts = QVector<ScriptPart>;

ScriptPart select_unit(int level, int unit, const QString &unitName = {})
{
    auto ret = Write{ 0x0200,  static_cast<u16>(((level << 8) | unit)), Write::Opt_HexValue };

#if 1
    ret.comment = QString("select L%1.Unit%2").arg(level).arg(unit);

    if (!unitName.isEmpty())
        ret.comment += unitName;
#endif

    return ret;
};

// Note: the desired unit must be selected prior to calling this function.
ScriptPart write_unit_reg(u16 reg, u16 value, const QString &comment, unsigned writeOpts = 0u)
{
    auto ret = Write { static_cast<u16>(0x0300u + reg), value, comment, writeOpts };

    return ret;
}

ScriptPart write_unit_reg(u16 reg, u16 value, unsigned writeOpts = 0u)
{
    return write_unit_reg(reg, value, {}, writeOpts);
}

// Note: the desired unit must be selected prior to calling this function.
ScriptPart write_connection(u16 offset, u16 value, const QString &sourceName = {})
{
    auto ret = Write { static_cast<u16>(0x0380u + offset), value };

    if (!sourceName.isEmpty())
        ret.comment = QString("connect input%1 to '%2'")
            .arg(offset / 2).arg(sourceName);

    return ret;
}

ScriptPart write_strobe_connection(u16 offset, u16 value, const QString &sourceName = {})
{
    auto ret = Write { static_cast<u16>(0x0380u + offset), value };

    if (!sourceName.isEmpty())
        ret.comment = QString("connect strobe_input to '%1'").arg(sourceName);

    return ret;
}

ScriptParts generate(const trigger_io::Timer &unit, int index)
{
    (void) index;

    ScriptParts ret;
    ret += write_unit_reg(2, static_cast<u16>(unit.range), "range (0:ns, 1:us, 2:ms, 3:s)");
    ret += write_unit_reg(4, unit.delay_ns, "delay [ns]");
    ret += write_unit_reg(6, unit.period, "period [in range units]");
    return ret;
}

ScriptParts generate(const trigger_io::IRQ_Unit &unit, int index)
{
    (void) index;

    ScriptParts ret;
    ret += write_unit_reg(0, static_cast<u16>(unit.irqIndex),
                          "irq_index (zero-based: 0: IRQ1, .., 6: IRQ7)");
    return ret;
}

ScriptParts generate(const trigger_io::SoftTrigger &unit)
{
    ScriptParts ret;
    ret += write_unit_reg(2, static_cast<u16>(unit.activation),
                          "output activation: 0=level, 1=pulse");
    return ret;
}

namespace io_flags
{
    using Flags = u8;
    static const Flags HasDirection    = 1u << 0;
    static const Flags HasActivation   = 1u << 1;
    static const Flags StrobeGGOffsets = 1u << 2;

    static const Flags None             = 0u;
    static const Flags NIM_IO_Flags     = HasDirection | HasActivation;
    static const Flags ECL_IO_Flags     = HasActivation;
    static const Flags StrobeGG_Flags   = StrobeGGOffsets;
}

/* The IO structure is used for different units sharing IO properties:
 * NIM I/Os, ECL Outputs, slave triggers, and strobe gate generators.
 * The common properties are delay, width, holdoff and invert. They start at
 * register offset 0 except for the strobe GGs where the registers are offset
 * by one address increment (2 bytes).
 * The activation and direction registers are at offsets 10 and 16. They are
 * only written out if the respective io_flags bit is set.
 */
ScriptParts generate(const trigger_io::IO &io, const io_flags::Flags &ioFlags)
{
    ScriptParts ret;

    u16 offset = (ioFlags & io_flags::StrobeGGOffsets) ? 0x32u : 0u;

    ret += write_unit_reg(offset + 0, io.delay, "delay [ns]");
    ret += write_unit_reg(offset + 2, io.width, "width [ns]");
    ret += write_unit_reg(offset + 4, io.holdoff, "holdoff [ns]");
    ret += write_unit_reg(offset + 6, static_cast<u16>(io.invert),
                          "invert (start on trailing edge of input)");

    if (ioFlags & io_flags::HasDirection)
        ret += write_unit_reg(10, static_cast<u16>(io.direction), "direction (0:in, 1:out)");

    if (ioFlags & io_flags::HasActivation)
        ret += write_unit_reg(16, static_cast<u16>(io.activate), "output activate");

    return ret;
}

ScriptParts generate(const trigger_io::StackBusy &unit, int index)
{
    (void) index;

    ScriptParts ret;
    ret += write_unit_reg(0, unit.stackIndex, "stack_index");
    return ret;
}

trigger_io::LUT_RAM make_lut_ram(const LUT &lut)
{
    trigger_io::LUT_RAM ram = {};

    for (size_t address = 0; address < lut.lutContents[0].size(); address++)
    {
        unsigned ramValue = 0u;

        // Combine the three separate output entries into a single value
        // suitable for the MVLC LUT RAM.
        for (unsigned output = 0; output < lut.lutContents.size(); output++)
        {
            if (lut.lutContents[output].test(address))
            {
                ramValue |= 1u << output;
                assert(ramValue < (1u << trigger_io::LUT::OutputBits));
            }
        }

        trigger_io::set(ram, address, ramValue);
    }

    return ram;
}

ScriptParts write_lut_ram(const trigger_io::LUT_RAM &ram)
{
    ScriptParts ret;

    for (const auto &kv: ram | indexed(0))
    {
        u16 reg = kv.index() * sizeof(u16); // register address increment is 2 bytes
        u16 cell = reg * 2;
        auto comment = QString("cells %1-%2").arg(cell).arg(cell + 3);
        ret += write_unit_reg(reg, kv.value(), comment, Write::Opt_HexValue);
    }

    return ret;
}

ScriptParts write_lut(const LUT &lut)
{
    return write_lut_ram(make_lut_ram(lut));
}

ScriptParts generate(const trigger_io::StackStart &unit, int index)
{
    (void) index;

    ScriptParts ret;
    ret += write_unit_reg(0, static_cast<u16>(unit.activate), "activate");
    ret += write_unit_reg(2, unit.stackIndex, "stack index");
    ret += write_unit_reg(4, unit.delay_ns, "delay [ns]");
    return ret;
}

ScriptParts generate(const trigger_io::MasterTrigger &unit, int index)
{
    (void) index;

    ScriptParts ret;
    ret += write_unit_reg(0, static_cast<u16>(unit.activate), "activate");
    return ret;
}

ScriptParts generate(const trigger_io::Counter &unit, int index)
{
    (void) index;

    ScriptParts ret;
    ret += write_unit_reg(14, static_cast<u16>(unit.clearOnLatch), "clear on latch");
    return ret;
}

ScriptParts generate_trigger_io_script(const TriggerIO &ioCfg)
{
    ScriptParts ret;

    //
    // Level0
    //

    ret += "Level0 #####################################################";

    for (const auto &kv: ioCfg.l0.timers | indexed(0))
    {
        ret += ioCfg.l0.DefaultUnitNames[kv.index()];
        ret += select_unit(0, kv.index());
        ret += generate(kv.value(), kv.index());
    }

    for (const auto &kv: ioCfg.l0.irqUnits | indexed(0))
    {
        ret += ioCfg.l0.DefaultUnitNames[kv.index() + ioCfg.l0.IRQ_UnitOffset];
        ret += select_unit(0, kv.index() + ioCfg.l0.IRQ_UnitOffset);
        ret += generate(kv.value(), kv.index());
    }

    for (const auto &kv: ioCfg.l0.softTriggers | indexed(0))
    {
        ret += ioCfg.l0.DefaultUnitNames[kv.index() + ioCfg.l0.SoftTriggerOffset];
        ret += select_unit(0, kv.index() + ioCfg.l0.SoftTriggerOffset);
        ret += generate(kv.value());
    }

    for (const auto &kv: ioCfg.l0.slaveTriggers | indexed(0))
    {
        ret += ioCfg.l0.DefaultUnitNames[kv.index() + ioCfg.l0.SlaveTriggerOffset];
        ret += select_unit(0, kv.index() + ioCfg.l0.SlaveTriggerOffset);
        ret += generate(kv.value(), io_flags::None);
    }

    for (const auto &kv: ioCfg.l0.stackBusy | indexed(0))
    {
        ret += ioCfg.l0.DefaultUnitNames[kv.index() + ioCfg.l0.StackBusyOffset];
        ret += select_unit(0, kv.index() + ioCfg.l0.StackBusyOffset);
        ret += generate(kv.value(), kv.index());
    }

    for (const auto &kv: ioCfg.l0.ioNIM | indexed(0))
    {
        ret += ioCfg.l0.DefaultUnitNames[kv.index() + ioCfg.l0.NIM_IO_Offset];
        ret += select_unit(0, kv.index() + ioCfg.l0.NIM_IO_Offset);
        ret += generate(kv.value(), io_flags::NIM_IO_Flags);
    }

    for (const auto &kv: ioCfg.l0.ioIRQ | indexed(0))
    {
        ret += ioCfg.l0.DefaultUnitNames[kv.index() + ioCfg.l0.IRQ_Inputs_Offset];
        ret += select_unit(0, kv.index() + ioCfg.l0.IRQ_Inputs_Offset);
        ret += generate(kv.value(), io_flags::None);
    }

    //
    // Level1
    //

    ret += "Level1 #####################################################";

    for (const auto &kv: ioCfg.l1.luts | indexed(0))
    {
        unsigned unitIndex = kv.index();
        ret += QString("L1.LUT%1").arg(unitIndex);
        ret += select_unit(1, unitIndex);
        ret += write_lut(kv.value());
    }

    //
    // Level2
    //

    ret += "Level2 #####################################################";

    for (const auto &kv: ioCfg.l2.luts | indexed(0))
    {
        unsigned unitIndex = kv.index();
        ret += QString("L2.LUT%1").arg(unitIndex);
        ret += select_unit(2, unitIndex);
        ret += write_lut(kv.value());
        ret += write_unit_reg(0x20, kv.value().strobedOutputs.to_ulong(),
                              "strobed_outputs", Write::Opt_BinValue);

        const auto &l2InputChoices = Level2::DynamicInputChoices[unitIndex];

        for (size_t input = 0; input < Level2::LUT_DynamicInputCount; input++)
        {
            unsigned conValue = ioCfg.l2.lutConnections[unitIndex][input];
            UnitAddress conAddress = l2InputChoices.lutChoices[input][conValue];
            u16 regOffset = input * 2;

            ret += write_connection(regOffset, conValue, lookup_name(ioCfg, conAddress));
        }

        // strobe GG
        ret += QString("L2.LUT%1 strobe gate generator").arg(unitIndex);
        ret += generate(kv.value().strobeGG, io_flags::StrobeGG_Flags);

        // strobe_input
        {
            unsigned conValue = ioCfg.l2.strobeConnections[unitIndex];
            UnitAddress conAddress = l2InputChoices.strobeChoices[conValue];
            u16 regOffset = 6;

            ret += write_strobe_connection(regOffset, conValue, lookup_name(ioCfg, conAddress));
        }
    }

    //
    // Level3
    //

    ret += "Level3 #####################################################";

    for (const auto &kv: ioCfg.l3.stackStart | indexed(0))
    {
        unsigned unitIndex = kv.index();

        ret += ioCfg.l3.DefaultUnitNames[unitIndex];
        ret += select_unit(3, unitIndex);
        ret += generate(kv.value(), unitIndex);

        unsigned conValue = ioCfg.l3.connections[unitIndex][0];
        UnitAddress conAddress = ioCfg.l3.DynamicInputChoiceLists[unitIndex][0][conValue];

        ret += write_connection(0, conValue, lookup_name(ioCfg, conAddress));
    }

    for (const auto &kv: ioCfg.l3.masterTriggers | indexed(0))
    {
        unsigned unitIndex = kv.index() + ioCfg.l3.MasterTriggersOffset;

        ret += ioCfg.l3.DefaultUnitNames[unitIndex];
        ret += select_unit(3, unitIndex);
        ret += generate(kv.value(), unitIndex);

        unsigned conValue = ioCfg.l3.connections[unitIndex][0];
        UnitAddress conAddress = ioCfg.l3.DynamicInputChoiceLists[unitIndex][0][conValue];

        ret += write_connection(0, conValue, lookup_name(ioCfg, conAddress));
    }

    for (const auto &kv: ioCfg.l3.counters | indexed(0))
    {
        unsigned unitIndex = kv.index() + ioCfg.l3.CountersOffset;

        ret += ioCfg.l3.DefaultUnitNames[unitIndex];
        ret += select_unit(3, unitIndex);
        ret += generate(kv.value(), unitIndex);

        // counter input
        unsigned conValue = ioCfg.l3.connections[unitIndex][0];
        UnitAddress conAddress = ioCfg.l3.DynamicInputChoiceLists[unitIndex][0][conValue];

        ret += write_connection(0, conValue, lookup_name(ioCfg, conAddress));

        // latch input
        conValue = ioCfg.l3.connections[unitIndex][1];
        conAddress = ioCfg.l3.DynamicInputChoiceLists[unitIndex][1][conValue];

        ret += write_connection(2, conValue, lookup_name(ioCfg, conAddress));
    }

    // Level3 NIM connections
    ret += "NIM unit connections (Note: NIM setup is done in the Level0 section)";
    for (size_t nim = 0; nim < trigger_io::NIM_IO_Count; nim++)
    {
        unsigned unitIndex = nim + ioCfg.l3.NIM_IO_Unit_Offset;

        ret += ioCfg.l3.DefaultUnitNames[unitIndex];
        ret += select_unit(3, unitIndex);

        unsigned conValue = ioCfg.l3.connections[unitIndex][0];
        UnitAddress conAddress = ioCfg.l3.DynamicInputChoiceLists[unitIndex][0][conValue];

        ret += write_connection(0, conValue, lookup_name(ioCfg, conAddress));

    }

    for (const auto &kv: ioCfg.l3.ioECL | indexed(0))
    {
        unsigned unitIndex = kv.index() + ioCfg.l3.ECL_Unit_Offset;

        ret += ioCfg.l3.DefaultUnitNames[unitIndex];
        ret += select_unit(3, unitIndex);
        ret += generate(kv.value(), io_flags::ECL_IO_Flags);

        unsigned conValue = ioCfg.l3.connections[unitIndex][0];
        UnitAddress conAddress = ioCfg.l3.DynamicInputChoiceLists[unitIndex][0][conValue];

        ret += write_connection(0, conValue, lookup_name(ioCfg, conAddress));
    }

    return ret;
}

class ScriptGenPartVisitor: public boost::static_visitor<>
{
    public:
        explicit ScriptGenPartVisitor(QStringList &lineBuffer)
            : m_lineBuffer(lineBuffer)
        { }

        void operator()(const Write &write)
        {
            QString prefix;
            int width = 6;
            int base = 10;
            char fill = ' ';

            if (write.options & Write::Opt_HexValue)
            {
                prefix = "0x";
                width = 4;
                base = 16;
                fill = '0';
            }
            else if (write.options & Write::Opt_BinValue)
            {
                prefix = "0b";
                width = 4;
                base = 2;
                fill ='0';
            }

            auto line = QString("0x%1 %2%3")
                .arg(write.address, 4, 16, QLatin1Char('0'))
                .arg(prefix)
                .arg(write.value, width, base, QLatin1Char(fill));

            if (!write.comment.isEmpty())
                line += "    # " + write.comment;

            m_lineBuffer.push_back(line);
        }

        void operator() (const QString &blockComment)
        {
            if (!blockComment.isEmpty())
            {
                m_lineBuffer.push_back({});
                m_lineBuffer.push_back("# " + blockComment);
            }
        }

    private:
        QStringList &m_lineBuffer;
};

static QString generate_mvlc_meta_block(
    const TriggerIO &ioCfg,
    const gen_flags::Flag &flags)
{
    // unit number -> unit name
    using NameMap = std::map<unsigned, std::string>;

    YAML::Emitter out;
    assert(out.good()); // initial consistency check
    out << YAML::BeginMap;
    out << YAML::Key << "names" << YAML::Value << YAML::BeginMap;

    // Level0 - flat list of unitnames
    {
        NameMap m;

        for (const auto &kv: ioCfg.l0.DefaultUnitNames | indexed(0))
        {
            const auto &unitIndex = kv.index();
            const auto &defaultName = kv.value();
            const auto &unitName = ioCfg.l0.unitNames.value(unitIndex);

            if (defaultName == UnitNotAvailable)
                continue;

            if (unitName != defaultName ||
                (flags & gen_flags::MetaIncludeDefaultUnitNames))
            {
                m[unitIndex] = unitName.toStdString();
            }
        }

        if (!m.empty())
            out << YAML::Key << "level0" << YAML::Value << m;
    }

    // Level1 - per lut output names
    {
        // Map structure is  [lutIndex][outputIndex] = outputName

        std::map<unsigned, NameMap> lutMaps;

        for (const auto &kv: ioCfg.l1.luts | indexed(0))
        {
            const auto &unitIndex = kv.index();
            const auto &lut = kv.value();

            NameMap m;

            for (const auto &kv2: lut.outputNames | indexed(0))
            {
                const auto &outputIndex = kv2.index();
                const auto &outputName = kv2.value();
                const auto DefaultOutputName = QString("L1.LUT%1.OUT%2")
                    .arg(unitIndex).arg(outputIndex);

                if (outputName != DefaultOutputName ||
                    (flags & gen_flags::MetaIncludeDefaultUnitNames))
                {
                    m[outputIndex] = outputName.toStdString();
                }
            }

            if (!m.empty())
                lutMaps[unitIndex] = m;
        }

        if (!lutMaps.empty())
            out << YAML::Key << "level1" << YAML::Value << lutMaps;
    }

    // Level2 - per lut output names
    {
        // Map structure is  [lutIndex][outputIndex] = outputName

        std::map<unsigned, NameMap> lutMaps;

        for (const auto &kv: ioCfg.l2.luts | indexed(0))
        {
            const auto &unitIndex = kv.index();
            const auto &lut = kv.value();

            NameMap m;

            for (const auto &kv2: lut.outputNames | indexed(0))
            {
                const auto &outputIndex = kv2.index();
                const auto &outputName = kv2.value();
                const auto DefaultOutputName = QString("L2.LUT%1.OUT%2")
                    .arg(unitIndex).arg(outputIndex);

                if (outputName != DefaultOutputName ||
                    (flags & gen_flags::MetaIncludeDefaultUnitNames))
                {
                    m[outputIndex] = outputName.toStdString();
                }
            }

            if (!m.empty())
                lutMaps[unitIndex] = m;
        }

        if (!lutMaps.empty())
            out << YAML::Key << "level2" << YAML::Value << lutMaps;
    }

    // Level3 - flat list of unitnames
    {
        NameMap m;

        for (const auto &kv: ioCfg.l3.DefaultUnitNames | indexed(0))
        {
            const size_t unitIndex = kv.index();

            // Skip NIM I/Os as these are included in level0
            if (Level3::NIM_IO_Unit_Offset <= unitIndex
                && unitIndex < Level3::NIM_IO_Unit_Offset + trigger_io::NIM_IO_Count)
            {
                continue;
            }

            const auto &defaultName = kv.value();
            const auto &unitName = ioCfg.l3.unitNames.value(unitIndex);

            if (defaultName == UnitNotAvailable)
                continue;

            if (unitName != defaultName ||
                (flags & gen_flags::MetaIncludeDefaultUnitNames))
            {
                m[unitIndex] = unitName.toStdString();
            }
        }

        if (!m.empty())
            out << YAML::Key << "level3" << YAML::Value << m;
    }

    out << YAML::EndMap;
    assert(out.good()); // consistency check

    //
    // Additional settings stored in the meta block, e.g. soft activate flags.
    //

    out << YAML::Key << "settings" << YAML::Value << YAML::BeginMap;

    {
        out << YAML::Key << "level0" << YAML::Value << YAML::BeginMap;

        for (const auto &kv: ioCfg.l0.timers | indexed(0))
        {
            auto &unit = kv.value();
            unsigned unitIndex = kv.index();

            out << YAML::Key << unitIndex << YAML::Value << YAML::BeginMap;
            out << YAML::Key << "soft_activate" << YAML::Value << unit.softActivate;
            out << YAML::EndMap;
        }

        out << YAML::EndMap;
        assert(out.good()); // consistency check
    }

    {
        out << YAML::Key << "level3" << YAML::Value << YAML::BeginMap;

        for (const auto &kv: ioCfg.l3.counters | indexed(ioCfg.l3.CountersOffset))
        {
            auto &unit = kv.value();
            unsigned unitIndex = kv.index();

            out << YAML::Key << unitIndex << YAML::Value << YAML::BeginMap;
            out << YAML::Key << "soft_activate" << YAML::Value << unit.softActivate;
            out << YAML::EndMap;
        }

        out << YAML::EndMap;
        assert(out.good()); // consistency check
    }

    out << YAML::EndMap;
    assert(out.good()); // consistency check

    return QString(out.c_str());
}

static const u32 MVLC_VME_InterfaceAddress = 0xffff0000u;

QString generate_trigger_io_script_text(
    const TriggerIO &ioCfg,
    const gen_flags::Flag &flags)
{
    QStringList lines =
    {
        "##############################################################",
        "# MVLC Trigger I/O  setup via internal VME interface         #",
        "##############################################################",
        "",
        "# Note: This file was generated by mvme. Editing existing",
        "# values is allowed and these changes will used by mvme",
        "# when next parsing the script. Changes to the basic",
        "# structure, like adding new write or read commands, are not",
        "# allowed. These changes will be lost the next time the file",
        "# is modified by mvme.",
        "",
        "# Internal MVLC VME interface address",
        QString("setbase 0x%1").arg(MVLC_VME_InterfaceAddress, 8, 16, QLatin1Char('0'))
    };

    ScriptGenPartVisitor visitor(lines);
    auto parts = generate_trigger_io_script(ioCfg);

    for (const auto &part: parts)
    {
        boost::apply_visitor(visitor, part);
    }

    lines.append(
    {
        "",
        "##############################################################",
        "# MVLC Trigger I/O specific meta information                 #",
        "##############################################################",
        vme_script::MetaBlockBegin + " " + MetaTagMVLCTriggerIO,
        generate_mvlc_meta_block(ioCfg, flags),
        vme_script::MetaBlockEnd
    });

    return lines.join("\n");
}

static const size_t LevelCount = 4;
static const u16 UnitSelectRegister = 0x200u;
static const u16 UnitRegisterBase = 0x300u;
static const u16 UnitConnectBase = 0x80u;
//static const u16 UnitConnectMask = UnitConnectBase;

// Maps register address to register value
using RegisterWrites = QMap<u16, u16>;

// Holds per unit address register writes
using UnitWrites = QMap<u16, RegisterWrites>;

// Holds per level UnitWrites
using LevelWrites = std::array<UnitWrites, LevelCount>;

trigger_io::IO parse_io(const RegisterWrites &writes, const io_flags::Flags &ioFlags)
{
    trigger_io::IO io = {};

    u16 offset = (ioFlags & io_flags::StrobeGGOffsets) ? 0x32u : 0u;

    io.delay     = writes[offset + 0];
    io.width     = writes[offset + 2];
    io.holdoff   = writes[offset + 4];
    io.invert    = static_cast<bool>(writes[offset + 6]);

    io.direction = static_cast<trigger_io::IO::Direction>(writes[10]);
    io.activate  = static_cast<bool>(writes[16]);

    return io;
}

trigger_io::LUT_RAM parse_lut_ram(const RegisterWrites &writes)
{
    trigger_io::LUT_RAM ram = {};

    for (size_t line = 0; line < ram.size(); line++)
    {
        u16 regAddress = line * 2;
        ram[line] = writes[regAddress];
    }

    return ram;
}

LUT parse_lut(
    const RegisterWrites &writes,
    const std::array<QString, LUT::OutputBits> &outputNames,
    const std::array<QString, LUT::OutputBits> &defaultOutputNames)
{
    auto ram = parse_lut_ram(writes);

    LUT lut = {};

    for (size_t address = 0; address < lut.lutContents[0].size(); address++)
    {
        u8 ramValue = trigger_io::lookup(ram, address);

        // Distribute the 3 output bits stored in a single RAM cell to the 3
        // output arrays in lut.lutContents.
        for (unsigned output = 0; output < lut.lutContents.size(); output++)
        {
            lut.lutContents[output][address] = (ramValue >> output) & 0b1;
        }
    }

    lut.strobedOutputs = writes[0x20];
    lut.strobeGG = parse_io(writes, io_flags::StrobeGG_Flags);

    std::copy(outputNames.begin(), outputNames.end(),
              lut.outputNames.begin());

    std::copy(defaultOutputNames.begin(), defaultOutputNames.end(),
              lut.defaultOutputNames.begin());

    return lut;
}

void parse_mvlc_meta_block(const vme_script::MetaBlock &meta, TriggerIO &ioCfg)
{
    auto y_to_qstr = [](const YAML::Node &y) -> QString
    {
        assert(y);
        return QString::fromStdString(y.as<std::string>());
    };

    assert(meta.tag() == MetaTagMVLCTriggerIO);

    YAML::Node yRoot = YAML::Load(meta.textContents.toStdString());

    if (!yRoot || !yRoot["names"]) return;

    //
    // Names
    //
    const auto &yLevelNames = yRoot["names"];

    // Level0 - flat list of unitnames
    if (const auto &yNames = yLevelNames["level0"])
    {
        for (const auto &kv: ioCfg.l0.unitNames | indexed(0))
        {
            const auto &unitIndex = kv.index();
            auto &unitName = kv.value();

            if (yNames[unitIndex])
                unitName = y_to_qstr(yNames[unitIndex]);

            // Copy NIM_IO names to the level3 structure.
            if (Level3::NIM_IO_Unit_Offset <= static_cast<unsigned>(unitIndex)
                && static_cast<unsigned>(unitIndex) < (Level3::NIM_IO_Unit_Offset +
                                                       trigger_io::NIM_IO_Count))
            {
                ioCfg.l3.unitNames[unitIndex] = unitName;
            }
        }
    }

    // Level1 - per lut output names
    if (const auto &yUnitMaps = yLevelNames["level1"])
    {
        for (const auto &kv: ioCfg.l1.luts | indexed(0))
        {
            const auto &unitIndex = kv.index();
            auto &lut = kv.value();

            if (const auto &yNames = yUnitMaps[unitIndex])
            {
                for (const auto &kv2: lut.outputNames | indexed(0))
                {
                    const auto &outputIndex = kv2.index();
                    auto &outputName = kv2.value();

                    if (yNames[outputIndex])
                        outputName = y_to_qstr(yNames[outputIndex]);
                }
            }
        }
    }

    // Level2 - per lut output names
    if (const auto &yUnitMaps = yLevelNames["level2"])
    {
        for (const auto &kv: ioCfg.l2.luts | indexed(0))
        {
            const auto &unitIndex = kv.index();
            auto &lut = kv.value();

            if (const auto &yNames = yUnitMaps[unitIndex])
            {
                for (const auto &kv2: lut.outputNames | indexed(0))
                {
                    const auto &outputIndex = kv2.index();
                    auto &outputName = kv2.value();

                    if (yNames[outputIndex])
                        outputName = y_to_qstr(yNames[outputIndex]);
                }
            }
        }
    }

    // Level3 - flat list of unitnames
    if (const auto &yNames = yLevelNames["level3"])
    {
        for (const auto &kv: ioCfg.l3.unitNames | indexed(0))
        {
            const size_t &unitIndex = kv.index();
            auto &unitName = kv.value();

            // Skip NIM I/Os as these are included in level0
            if (Level3::NIM_IO_Unit_Offset <= unitIndex
                && unitIndex < Level3::NIM_IO_Unit_Offset + trigger_io::NIM_IO_Count)
            {
                continue;
            }

            if (yNames[unitIndex])
                unitName = y_to_qstr(yNames[unitIndex]);
        }
    }

    //
    // Additional settings stored in the meta block, e.g. soft activate flags.
    //

    if (!yRoot["settings"]) return;

    const auto &ySettings = yRoot["settings"];

    if (const auto &yLevelSettings = ySettings["level0"])
    {
        for (const auto &kv: ioCfg.l0.timers | indexed(0))
        {
            auto &unit = kv.value();
            unsigned unitIndex = kv.index();

            try
            {
                unit.softActivate = yLevelSettings[unitIndex]["soft_activate"].as<bool>();
            }
            catch (const std::runtime_error &)
            {}
        }
    }

    if (const auto &yLevelSettings = ySettings["level3"])
    {
        for (const auto &kv: ioCfg.l3.counters | indexed(ioCfg.l3.CountersOffset))
        {
            auto &unit = kv.value();
            unsigned unitIndex = kv.index();
            try
            {
                unit.softActivate = yLevelSettings[unitIndex]["soft_activate"].as<bool>();
            }
            catch (const std::runtime_error &)
            {}
        }
    }
}

TriggerIO build_config_from_writes(const LevelWrites &levelWrites)
{
    TriggerIO ioCfg;

    // level0
    {
        const auto &writes = levelWrites[0];

        for (const auto &kv: ioCfg.l0.timers | indexed(0))
        {
            unsigned unitIndex = kv.index();
            auto &unit = kv.value();

            unit.range = static_cast<trigger_io::Timer::Range>(writes[unitIndex][2]);
            unit.delay_ns = writes[unitIndex][4];
            unit.period = writes[unitIndex][6];
        }

        for (const auto &kv: ioCfg.l0.irqUnits | indexed(0))
        {
            unsigned unitIndex = kv.index() + Level0::IRQ_UnitOffset;
            auto &unit = kv.value();

            unit.irqIndex = writes[unitIndex][0];
        }

        for (const auto &kv: ioCfg.l0.softTriggers | indexed(0))
        {
            unsigned unitIndex = kv.index() + Level0::SoftTriggerOffset;
            auto &unit = kv.value();

            unit.activation = static_cast<SoftTrigger::Activation>(writes[unitIndex][2]);
        }

        for (const auto &kv: ioCfg.l0.slaveTriggers | indexed(0))
        {
            unsigned unitIndex = kv.index() + Level0::SlaveTriggerOffset;
            auto &unit = kv.value();

            unit = parse_io(writes[unitIndex], io_flags::None);
        }

        for (const auto &kv: ioCfg.l0.stackBusy | indexed(0))
        {

            unsigned unitIndex = kv.index() + Level0::StackBusyOffset;
            auto &unit = kv.value();

            unit.stackIndex = writes[unitIndex][0];
        }

        for (const auto &kv: ioCfg.l0.ioNIM | indexed(0))
        {

            unsigned unitIndex = kv.index() + Level0::NIM_IO_Offset;
            auto &unit = kv.value();

            unit = parse_io(writes[unitIndex], io_flags::NIM_IO_Flags);
        }

        for (const auto &kv: ioCfg.l0.ioIRQ | indexed(0))
        {

            unsigned unitIndex = kv.index() + Level0::IRQ_Inputs_Offset;
            auto &unit = kv.value();

            unit = parse_io(writes[unitIndex], io_flags::None);
        }
    }

    // level1
    {
        const auto &writes = levelWrites[1];

        for (const auto &kv: ioCfg.l1.luts | indexed(0))
        {
            unsigned unitIndex = kv.index();
            auto &unit = kv.value();
            unit = parse_lut(writes[unitIndex], unit.outputNames, unit.defaultOutputNames);
        }
    }

    // level2
    {
        const auto &writes = levelWrites[2];

        for (const auto &kv: ioCfg.l2.luts | indexed(0))
        {
            unsigned unitIndex = kv.index();
            auto &unit = kv.value();
            // This parses the LUT and the strobe GG settings
            unit = parse_lut(writes[unitIndex], unit.outputNames, unit.defaultOutputNames);

            // dynamic input connections
            for (size_t input = 0; input < Level2::LUT_DynamicInputCount; ++input)
            {
                ioCfg.l2.lutConnections[unitIndex][input] =
                    writes[unitIndex][UnitConnectBase + 2 * input];
            }

            // strobe GG connection
            ioCfg.l2.strobeConnections[unitIndex] = writes[unitIndex][UnitConnectBase + 6];
        }
    }

    // level3
    {
        // Copy NIM settings parsed from level0 data to the level3 NIM
        // structures.
        std::copy(ioCfg.l0.ioNIM.begin(),
                  ioCfg.l0.ioNIM.end(),
                  ioCfg.l3.ioNIM.begin());


        const auto &writes = levelWrites[3];

        for (const auto &kv: ioCfg.l3.stackStart | indexed(0))
        {
            unsigned unitIndex = kv.index();
            auto &unit = kv.value();

            unit.activate = static_cast<bool>(writes[unitIndex][0]);
            unit.stackIndex = writes[unitIndex][2];
            unit.delay_ns = writes[unitIndex][4];

            ioCfg.l3.connections[unitIndex] = { writes[unitIndex][0x80] };
        }

        for (const auto &kv: ioCfg.l3.masterTriggers | indexed(0))
        {
            unsigned unitIndex = kv.index() + Level3::MasterTriggersOffset;
            auto &unit = kv.value();

            unit.activate = static_cast<bool>(writes[unitIndex][0]);

            ioCfg.l3.connections[unitIndex] = { writes[unitIndex][0x80] };
        }

        for (const auto &kv: ioCfg.l3.counters | indexed(0))
        {
            unsigned unitIndex = kv.index() + Level3::CountersOffset;
            auto &unit = kv.value();

            unit.clearOnLatch = static_cast<bool>(writes[unitIndex][14]);

            ioCfg.l3.connections[unitIndex] = { writes[unitIndex][0x80], writes[unitIndex][0x82] };
        }

        for (const auto &kv: ioCfg.l3.ioNIM | indexed(0))
        {
            // level3 NIM connections (setup is done in level0)
            unsigned unitIndex = kv.index() + Level3::NIM_IO_Unit_Offset;

            ioCfg.l3.connections[unitIndex] = { writes[unitIndex][0x80] };
        }

        for (const auto &kv: ioCfg.l3.ioECL | indexed(0))
        {
            unsigned unitIndex = kv.index() + Level3::ECL_Unit_Offset;
            auto &unit = kv.value();

            unit = parse_io(writes[unitIndex], io_flags::ECL_IO_Flags);

            ioCfg.l3.connections[unitIndex] = { writes[unitIndex][0x80] };
        }
    }

    return ioCfg;
}

TriggerIO parse_trigger_io_script_text(const QString &text)
{
    auto commands = vme_script::parse(text);

    LevelWrites levelWrites;

    u16 level = 0;
    u16 unit  = 0;

    for (const auto &cmd: commands)
    {
        if (!(cmd.type == vme_script::CommandType::Write))
            continue;

        u32 address = cmd.address;

        // Clear the uppper 16 bits of the 32 bit address value. In the
        // generated script these are set by the setbase command on the very first line.
        address &= ~MVLC_VME_InterfaceAddress;

        if (address == UnitSelectRegister)
        {
            level = (cmd.value >> 8) & 0b11;
            unit  = (cmd.value & 0xff);
        }
        else if (level < levelWrites.size())
        {
            // Store all other writes in the map structure under the current
            // level and unit. Also subtract the UnitRegisterBase from the
            // write address to get the plain register address.
            address -= UnitRegisterBase;
            levelWrites[level][unit][address] = cmd.value;
        }
    }

    auto ioCfg = build_config_from_writes(levelWrites);

    // meta block handling
    auto metaCmd = get_first_meta_block(commands);

    if (metaCmd.metaBlock.tag() == MetaTagMVLCTriggerIO)
        parse_mvlc_meta_block(metaCmd.metaBlock, ioCfg);

    return ioCfg;
}

TriggerIO load_default_trigger_io()
{
    auto scriptContents = vats::read_default_mvlc_trigger_io_script().contents;
    return parse_trigger_io_script_text(scriptContents);
}

} // end namespace mvme_mvlc
} // end namespace mesytec
} // end namespace trigger_io
