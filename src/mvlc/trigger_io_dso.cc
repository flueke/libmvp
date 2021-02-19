#include "mvlc/trigger_io_dso.h"
#include "mesytec-mvlc/mvlc_constants.h"
#include "mesytec-mvlc/mvlc_dialog.h"
#include "util/math.h"
#include <chrono>

#ifndef __WIN32
#include <sys/prctl.h>
#endif

#include <QDebug>

namespace mesytec
{
namespace mvme_mvlc
{
namespace trigger_io
{

namespace
{

static const unsigned UnitNumber = 48;
static const u32 DSOReadAddress = mvlc::SelfVMEAddress + 4;

bool is_fatal(const std::error_code &ec)
{
    return (ec == mvlc::ErrorType::ConnectionError
            || ec == mvlc::ErrorType::ProtocolError);
}

void self_write_throw(mvlc::MVLCDialog &mvlc, u32 addr, u16 value)
{
    if (auto ec = mvlc.vmeWrite(
            mvlc::SelfVMEAddress + addr, value,
            mvlc::vme_amods::A32, mvlc::VMEDataWidth::D16))
        throw ec;
}

std::error_code start_dso(mvlc::MVLCDialog &mvlc, DSOSetup setup)
{
    try
    {
        self_write_throw(mvlc, 0x0200, UnitNumber); // select DSO unit
        self_write_throw(mvlc, 0x0300, setup.preTriggerTime);
        self_write_throw(mvlc, 0x0302, setup.postTriggerTime);
        self_write_throw(mvlc, 0x0304, setup.nimTriggers.to_ulong());
        self_write_throw(mvlc, 0x0308, setup.irqTriggers.to_ulong());
        self_write_throw(mvlc, 0x030A, setup.utilTriggers.to_ulong());
        self_write_throw(mvlc, 0x0306, 1); // start capturing
    }
    catch (const std::error_code &ec)
    {
        return ec;
    }

    return {};
}

std::error_code stop_dso(mvlc::MVLCDialog &mvlc)
{
    try
    {
        self_write_throw(mvlc, 0x0200, UnitNumber); // select DSO unit
        self_write_throw(mvlc, 0x0306, 0); // stop the DSO
    }
    catch (const std::error_code &ec)
    {
        return ec;
    }

    return {};
}

std::error_code read_dso(mvlc::MVLCDialog &mvlc, std::vector<u32> &dest)
{
    // block read
    return mvlc.vmeBlockRead(
        DSOReadAddress, mvlc::vme_amods::MBLT64,
        std::numeric_limits<u16>::max(), dest);
}
}

CombinedTriggers
get_combined_triggers(const DSOSetup &setup)
{
    std::bitset<CombinedTriggerCount> result;

    assert(result.size() == setup.nimTriggers.size() + setup.irqTriggers.size() + setup.utilTriggers.size());

    size_t bitIndex = 0;

    for (size_t i=0; i<setup.nimTriggers.size(); ++i, ++bitIndex)
        result.set(bitIndex, setup.nimTriggers.test(i));

    for (size_t i=0; i<setup.irqTriggers.size(); ++i, ++bitIndex)
        result.set(bitIndex, setup.irqTriggers.test(i));

    for (size_t i=0; i<setup.utilTriggers.size(); ++i, ++bitIndex)
        result.set(bitIndex, setup.utilTriggers.test(i));

    return result;
}

void
set_combined_triggers(DSOSetup &setup, const CombinedTriggers &combinedTriggers)
{
    size_t cIndex = 0;

    for (size_t i=0; i<setup.nimTriggers.size(); ++i, ++cIndex)
    {
        assert(cIndex < combinedTriggers.size());
        setup.nimTriggers.set(i, combinedTriggers.test(cIndex));
    }

    for (size_t i=0; i<setup.irqTriggers.size(); ++i, ++cIndex)
    {
        assert(cIndex < combinedTriggers.size());
        setup.irqTriggers.set(i, combinedTriggers.test(cIndex));
    }

    for (size_t i=0; i<setup.utilTriggers.size(); ++i, ++cIndex)
    {
        assert(cIndex < combinedTriggers.size());
        setup.utilTriggers.set(i, combinedTriggers.test(cIndex));
    }

    assert(cIndex == combinedTriggers.size());
}

std::error_code acquire_dso_sample(
    mvlc::MVLC mvlc, DSOSetup setup,
    std::vector<u32> &dest,
    std::atomic<bool> &cancel,
    const std::chrono::milliseconds &timeout)
{
    auto tStart = std::chrono::steady_clock::now();

    // Stop the stack error poller so that it doesn't read our samples off the
    // command pipe.
    auto errPollerLock = mvlc.suspendStackErrorPolling();

    // To enforce that no other communication takes places on the command pipe
    // while the DSO is active we lock the command pipe here, then create a
    // local MVLCDialog instance which works directly on the underlying
    // low-level MVLCBasicInterface and thus doesn't do any locking itself.
    //
    // Note: any stack errors accumulated in the local MVLCDialog instance are
    // discarded. For total correctness the stack error counters would have to
    // be updated with the locally accumulated error counts.

    auto cmdLock = mvlc.getLocks().lockCmd();
    mvlc::MVLCDialog dlg(mvlc.getImpl());

    // start, then read until we get a sample, stop

    if (auto ec = start_dso(dlg, setup))
        return ec;

    dest.clear();
    bool timed_out = false;

    while (!cancel && dest.size() <= 2 && !timed_out)
    {
        dest.clear();
        auto ec = read_dso(dlg, dest);

        if (is_fatal(ec))
            return ec;

        auto elapsed = std::chrono::steady_clock::now() - tStart;

        if (elapsed >= timeout)
            timed_out = true;
    }

    if (auto ec = stop_dso(dlg))
        return ec;

    // Read and throw away any additional samples (needed to clear the command
    // pipe). Do this even if we got canceled as a sample might have become
    // available in the meantime.
    std::vector<u32> tmpBuffer;

    do
    {
        tmpBuffer.clear();

        auto ec = read_dso(dlg, tmpBuffer);

        if (is_fatal(ec))
            return ec;

    } while (tmpBuffer.size() > 2);

    if (timed_out && dest.size() <= 2)
        return make_error_code(std::errc::timed_out);

    return {};
}

Snapshot fill_snapshot_from_dso_buffer(const std::vector<u32> &buffer)
{
    using namespace std::chrono_literals;

    if (buffer.size() < 2 + 2)
    {
        //qDebug() << __PRETTY_FUNCTION__ << "got a short buffer";
        return {};
    }

    if ((buffer[0] >> 24) != 0xF3
        || (buffer[1] >> 24) != 0xF5)
    {
        //qDebug() << __PRETTY_FUNCTION__ << "invalid frame and block headers";
        return {};
    }

    if (buffer[2] != data_format::Header)
    {
        //qDebug() << __PRETTY_FUNCTION__ << "invalid Header";
        return {};
    }

    if (buffer[buffer.size()-1] != data_format::EoE)
    {
        //qDebug() << __PRETTY_FUNCTION__ << "invalid EoE";
        return {};
    }

    Snapshot result;
    result.reserve(NIM_IO_Count + Level0::IRQ_Inputs_Count);

    for (size_t i=3; i<buffer.size()-1; ++i)
    {
        const u32 word = buffer[i];
        const auto entry = extract_dso_entry(word);

        //qDebug("entry: addr=%u, time=%u, edge=%s",
        //       entry.address, entry.time, to_string(entry.edge));

        if (entry.address >= result.size())
            result.resize(entry.address + 1);

        assert(entry.address < result.size());

        auto &timeline = result[entry.address];
        timeline.push_back({std::chrono::nanoseconds(entry.time), entry.edge});

        // This is the FIFO overflow marker: the first sample of each channel
        // will have the time set to 1 (the first samples time is by definition
        // 0 so no information is lost). The code replaces the 1 with a 0 to
        // make plotting work just like for the non-overflow case.
        // TODO: use the overflow information somewhere? Keep the 1 and handle
        // it in some upper layer?
        /*
        if (timeline.size() == 1)
            if (timeline[0].time == 1ns)
                timeline[0].time = 0ns;
        */
    }

    return result;
}

void extend_traces_to(Snapshot &snapshot, const SampleTime &extendTo)
{
    for (auto &trace: snapshot)
    {
        if (trace.empty())
            continue;

        if (trace.back().time < extendTo)
        {
            if (has_overflow_marker(trace))
            {
                trace.push_back({ trace.back().time, Edge::Unknown });
                trace.push_back({ extendTo, Edge::Unknown });
            }
            else
            {
                trace.push_back({ extendTo, trace.back().edge });
            }
        }
    }
}

void extend_traces_to_post_trigger(Snapshot &snapshot, const DSOSetup &dsoSetup)
{
    SampleTime extendTo(dsoSetup.preTriggerTime + dsoSetup.postTriggerTime);
    extend_traces_to(snapshot, extendTo);
}

/* Jitter elimination:
 * - Look through the traces that are in the set of triggers.
 * - In each trigger trace find the time of the Rising sample that's
 *   closest to the preTriggerTime. Assume that this is the trace that was
 *   the trigger.
 * - From that closest time extract the 3 low bits. This is the jitter
 *   value of the snapshot.
 * - Subtract the jitter value from all samples of all traces in the snapshot.
 */
#if 0
s32 calculate_jitter_value(const Snapshot &snapshot, const DSOSetup &dsoSetup)
{
    auto combinedTriggers = combined_triggers(dsoSetup);

    // Adjusted by dsoSetup.preTriggerTime so a non-jittered trigger should
    // have a value of 0.
    s32 triggerTime = std::numeric_limits<s32>::max();

    for (size_t traceIdx=0;
         traceIdx<snapshot.size() && traceIdx < combinedTriggers.size();
         traceIdx++)
    {
        if (!combinedTriggers.test(traceIdx))
            continue;

        auto &trace = snapshot[traceIdx];

        for (auto &sample: trace)
        {
            if (sample.edge == Edge::Rising)
            {
                s32 tt = sample.time.count() - dsoSetup.preTriggerTime;
                qDebug() << __PRETTY_FUNCTION__
                    << "tt=" << tt
                    << std::abs(triggerTime);
                if (std::abs(tt) < std::abs(triggerTime))
                    triggerTime = tt;
            }
        }
    }

    s32 jitterValue = (std::abs(triggerTime) & 0b111) * mvme::util::sgn(triggerTime);

    qDebug() << __PRETTY_FUNCTION__
        << "triggerTime:" << triggerTime
        << ", jitterValue:" << jitterValue;

    return jitterValue;
}
#else
/* Korrektur des Flankenjitters der Triggerflanke:
 * Von der pretrigger_time werden die untersten 3 Bits nicht verwendet. Sie
 * darf aber schon auf jeden Wert gesetzt werden, es spielt keine Rolle.Also
 * für alle Berechnungen die untersten 3 Bits auf 000 setzen.
 * - nach pretrigger_time [15:3] == edge_time[15:3] suchen. (Es muß eine
 *   steigende Flanke sein, sonst sind die Daten kaputt)
 * - Wert der untersten 3Bits dieser gefundenen trigger edge time von allen
 *   edge times des gleichen trails abzienen. Der trigger edge liegt jetzt auf
 *   0, die anderen Flanken sind korrigiert.
 */
std::pair<unsigned, bool> calculate_jitter_value(const Snapshot &snapshot, const DSOSetup &dsoSetup)
{
    auto combinedTriggers = get_combined_triggers(dsoSetup);

    const unsigned maskedPreTrig = dsoSetup.preTriggerTime & (~0b111);

    for (size_t traceIdx=0;
         traceIdx<snapshot.size() && traceIdx < combinedTriggers.size();
         traceIdx++)
    {
        if (!combinedTriggers.test(traceIdx))
            continue;

        auto &trace = snapshot[traceIdx];

        for (auto &sample: trace)
        {
            if (sample.edge == Edge::Rising)
            {
                unsigned maskedSampleTime = static_cast<unsigned>(sample.time.count()) & (~0b111);

                if (maskedPreTrig == maskedSampleTime)
                {
                    return std::make_pair(static_cast<unsigned>(sample.time.count()) & 0b111, true);
                }
            }
        }
    }

    return std::make_pair(0u, false);
}
#endif

void
pre_process_dso_snapshot(
    Snapshot &snapshot,
    const DSOSetup &dsoSetup,
    SampleTime extendToTime)
{
    // Jitter correction
    auto jitterResult = calculate_jitter_value(snapshot, dsoSetup);
    unsigned jitter = jitterResult.first;

    if (jitter != 0)
    {
        for (auto &trace: snapshot)
        {
            // Never correct the first sample: it is either 0 or 1 (the latter
            // to indicate overflow).
            bool isFirstSample = true;

            for (auto &sample: trace)
            {
                if (!isFirstSample && sample.time != SampleTime::zero())
                    sample.time = SampleTime(sample.time.count() - jitter);

                isFirstSample = false;
            }
        }
    }

    if (extendToTime == SampleTime::zero())
        extendToTime = SampleTime(dsoSetup.preTriggerTime + dsoSetup.postTriggerTime);

    extend_traces_to(snapshot, extendToTime);
}

Edge edge_at(const Trace &trace, const SampleTime &t)
{
    Edge result = Edge::Falling;

    for (const auto &sample: trace)
    {
        if (sample.time <= t)
            result = sample.edge;
        else
            break;
    }

    return result;
}

namespace
{
std::vector<PinAddress> make_trace_index_to_pin_list()
{
    std::vector<PinAddress> result;

    // 14 NIMs
    for (auto i=0u; i<NIM_IO_Count; ++i)
    {
        UnitAddress unit = { 0, i+Level0::NIM_IO_Offset, 0 };
        result.push_back({ unit, PinPosition::Input });
    }

    // 6 IRQs
    for (auto i=0u; i<Level0::IRQ_Inputs_Count; ++i)
    {
        UnitAddress unit = { 0, i+Level0::IRQ_Inputs_Offset, 0 };
        result.push_back({ unit, PinPosition::Input });
    }

    // 16 Utility traces
    for (auto i=0u; i<Level0::UtilityUnitCount; ++i)
    {
        UnitAddress unit = { 0, i, 0 };
        result.push_back({ unit, PinPosition::Input });
    }

    return result;
}
}

const std::vector<PinAddress> &trace_index_to_pin_list()
{
    static const auto result = make_trace_index_to_pin_list();
    return result;
}

int get_trace_index(const PinAddress &pa)
{
    const auto &lst = trace_index_to_pin_list();

    auto it = std::find(std::begin(lst), std::end(lst), pa);

    if (it == std::end(lst))
        return -1;

    return it - std::begin(lst);
}

QString get_trigger_default_name(unsigned combinedTriggerIndex)
{
    const auto &pinList = trace_index_to_pin_list();

    assert(combinedTriggerIndex < pinList.size());

    if (combinedTriggerIndex < pinList.size())
    {
        auto pa = pinList.at(combinedTriggerIndex);

        assert(get_trace_index(pa) == static_cast<int>(combinedTriggerIndex));
        assert(pa.unit[0] == 0);
        assert(pa.unit[1] < Level0::DefaultUnitNames.size());

        if (pa.unit[1] < Level0::DefaultUnitNames.size())
            return Level0::DefaultUnitNames.at(pa.unit[1]);
    }

    return {};
}

} // end namespace trigger_io
} // end namespace mvme_mvlc
} // end namespace mesytec
