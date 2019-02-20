#ifndef __MVLC_CONSTANTS_H__
#define __MVLC_CONSTANTS_H__

#include <cstddef>
#include "typedefs.h"

namespace mesytec
{
namespace mvlc
{
    // Communication with the MVLC is done using 32-bit wide binary data words.
    // Results from commands and stack executions are also 32-bit aligned.


    static const u32 AddressIncrement = 4;
    static const u32 ReadLocalBlockMaxWords = 768;
    static const u32 CmdBufferResultSizeMask = 0xFFFF;
    static const u8 SuperResponseHeaderType = 0xF1;
    static const u8 StackResponseHeaderType = 0xF3;

    // Super commands are commands that are directly interpreted and executed
    // by the MVLC.
    // The values in the SuperCommands enum contain the 2 high bytes of the
    // command word.
    // The output of super commands always goes to pipe 0, the CommandPipe.
    static const u32 SuperCmdMask  = 0xFFFF;
    static const u32 SuperCmdShift = 16;
    static const u32 SuperCmdArgMask = 0xFFFF;
    static const u32 SuperCmdArgShift = 0;

    namespace super_commands
    {
        enum SuperCommands: u32
        {
            CmdBufferStart = 0xF100,
            CmdBufferEnd   = 0xF200,
            ReferenceWord  = 0x0101,
            ReadLocal      = 0x0102,
            ReadLocalBlock = 0x0103,
            WriteLocal     = 0x0204,
            WriteReset     = 0x0206,
        };
    } // end namespace supercommands

    // Stack-only commands. These can be written into the stack memory area
    // starting from StackMemoryBegin using WriteLocal commands.
    //
    // The output produced by a stack execution can go to either the
    // CommandPipe or the DataPipe. This is encoded in the StackStart command.

    static const u32 CmdMask      = 0xFF;
    static const u32 CmdShift     = 24;
    static const u32 CmdArg0Mask  = 0x00FF;
    static const u32 CmdArg0Shift = 16;
    static const u32 CmdArg1Mask  = 0x0000FFFF;
    static const u32 CmdArg1Shift = 0;

    namespace commands
    {
        static const u32 StackStart        = 0xF3;
        static const u32 StackEnd          = 0xF4;
        static const u32 VMEWrite          = 0x23;
        static const u32 VMERead           = 0x12;
        static const u32 WriteMarker       = 0xC2;
        static const u32 WriteSpecial      = 0xC1;
        // TODO: ScanDataRead, ReadDataLoop and masks/enums
        //static const u32 ScanDataRead      = 0x34;
    };

    // The equal the actual VME "private" address modes for the respective
    // transfer. FIXME: The other modes are also supported.
    enum AddressMode: u8
    {
        A16         = 0x2D,
        A24         = 0x3D,
        A32         = 0x0D,
        BLT32       = 0x0F,
        MBLT64      = 0x0C,
        Blk2eSST64  = 0x21,
    };

    enum VMEDataWidth
    {
        D16 = 0x1,
        D32 = 0x2
    };

    enum Blk2eSSTRate: u8
    {
        Rate160MB,
        Rate276MB,
        Rate300MB,
    };

    // Shift relative to the AddressMode argument of the read.
    static const u32 Blk2eSSTRateShift = 6;

    // For the WriteSpecial command
    enum SpecialWord: u8
    {
        Timestamp,
        StackTriggers
    };

    static const u32 InternalRegisterMin = 0x0001;
    static const u32 InternalRegisterMax = 0x5FFF;

    // Setting bit 0 to 1 enables autonomous execution of stacks in
    // reaction to triggers.
    static const u32 DAQModeEnableRegister = 0x1300;


    namespace stacks
    {
        static const u32 StackCount = 8;
        static const u32 Stack0TriggerRegister = 0x1100;
        // Note: The stack offset registers take offsets from StackMemoryBegin,
        // not absolute memory addresses.
        static const u32 Stack0OffsetRegister  = 0x1200;
        static const u32 StackMemoryBegin      = 0x2000;
        static const u32 StackMemoryWords      = 1024;
        static const u32 StackMemoryBytes      = StackMemoryWords * 4;
        static const u32 StackMemoryEnd        = StackMemoryBegin + StackMemoryBytes;
        // Mask for the number of valid bits in the stack offset register.
        // Higher order bits outside the mask are ignored by the MVLC.
        static const u32 StackOffsetBitMask    = 0x03FF;

        static const u32 ImmediateStackID = 0;
        static const u32 ImmediateStackWords = 64;

        enum TriggerType: u8
        {
            NoTrigger,
            IRQ,
            IRQNoIACK,
            External,
            TimerUnderrun,
        };

        // IMPORTANT: The IRQ bits have to be set to (IRQ - 1), e.g. value 0
        // for IRQ1!
        // TODO: rename IRQLevel to TriggerBits. I think these have different
        // meanings depending on the trigger type.
        static const u32 IRQLevelMask       = 0b11111;
        static const u32 IRQLevelShift      = 0;
        static const u32 TriggerTypeMask    = 0b111;
        static const u32 TriggerTypeShift   = 5;
        static const u32 ImmediateMask      = 0b1;
        static const u32 ImmediateShift     = 8;
    }

    namespace vme_module_interface
    {
        static const u32 VMEAddress       = 0xFFFF0000u;
    } // end namespace vme_module_interface

    // Limit imposed by FT_WritePipeEx and FT_ReadPipeEx.
    static const size_t USBSingleTransferMaxBytes = 1 * 1024 * 1024;
    static const size_t USBSingleTransferMaxWords = USBSingleTransferMaxBytes / sizeof(u32);
    static const u8 CommandPipe = 0;
    static const u8 DataPipe = 1;

} // end namespace mvlc
} // end namespace mesytec

#endif /* __MVLC_CONSTANTS_H__ */
