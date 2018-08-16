#ifndef __MVME_A2_H__
#define __MVME_A2_H__

#include <boost/dynamic_bitset.hpp>
#include <cassert>
#include <cpp11-on-multicore/common/rwlock.h>
#include <pcg_random.hpp>

#ifdef liba2_shared_EXPORTS
#include "a2_export.h"
#endif

#include "a2_exprtk.h"
#include "a2_param.h"
#include "listfilter.h"
#include "memory.h"
#include "multiword_datafilter.h"
#include "rate_sampler.h"
#include "util/typed_block.h"

namespace a2
{
using ParamVec = TypedBlock<double, s32>;

void print_param_vector(ParamVec pv);

inline void fill(ParamVec pv, double value)
{
    for (s32 i = 0; i < pv.size; ++i)
    {
        pv.data[i] = value;
    }
}

inline void invalidate_all(ParamVec pv)
{
    fill(pv, invalid_param());
}

inline void invalidate_all(double *params, s32 count)
{
    invalidate_all({params, count});
}

ParamVec push_param_vector(memory::Arena *arena, s32 size);
ParamVec push_param_vector(memory::Arena *arena, s32 size, double value);

struct Thresholds
{
    double min;
    double max;
};

inline bool in_range(Thresholds t, double v)
{
    return (t.min <= v && v < t.max);
}

struct PipeVectors
{
    ParamVec data;
    ParamVec lowerLimits;
    ParamVec upperLimits;
};

/* ===============================================
 * Data Sources
 * =============================================== */
struct DataSource
{
    PipeVectors output;
    ParamVec hitCounts;
    void *d;
    u8 moduleIndex;
    u8 type;
};

enum DataSourceType
{
    DataSource_Extractor,
    DataSource_ListFilterExtractor,
};

struct DataSourceOptions
{
    using opt_t = u8;
    static const opt_t NoOption                             = 0u;

    /* Do not add a random value in [0.0, 1.0) to the extracted data value. */
    static const opt_t NoAddedRandom                        = 1u << 0;

    /* Make the repetition value of ListFilters contribute to the low bits of
     * the final address value. By default the repetition number contributes to
     * the high address bits. */
    static const opt_t RepetitionContributesLowAddressBits  = 1u << 1;
};

struct Extractor
{
    data_filter::MultiWordFilter filter;
    pcg32_fast rng;
    u32 requiredCompletions;
    u32 currentCompletions;
    DataSourceOptions::opt_t options;
};

size_t get_address_count(Extractor *ex);

struct ListFilterExtractor
{
    data_filter::ListFilter listFilter;
    pcg32_fast rng;
    u8 repetitions;
    DataSourceOptions::opt_t options;
};

size_t get_base_address_bits(ListFilterExtractor *ex);
size_t get_repetition_address_bits(ListFilterExtractor *ex);
size_t get_address_bits(ListFilterExtractor *ex);
size_t get_address_count(ListFilterExtractor *ex);

Extractor make_extractor(
    data_filter::MultiWordFilter filter,
    u32 requiredCompletions,
    u64 rngSeed,
    DataSourceOptions::opt_t options = 0);

DataSource make_datasource_extractor(
    memory::Arena *arena,
    data_filter::MultiWordFilter filter,
    u32 requiredCompletions,
    u64 rngSeed,
    int moduleIndex,
    DataSourceOptions::opt_t options = 0);

ListFilterExtractor make_listfilter_extractor(
    data_filter::ListFilter listFilter,
    u8 repetitions,
    u64 rngSeed,
    DataSourceOptions::opt_t options = 0);

DataSource make_datasource_listfilter_extractor(
    memory::Arena *arena,
    data_filter::ListFilter listFilter,
    u8 repetitions,
    u64 rngSeed,
    u8 moduleIndex,
    DataSourceOptions::opt_t options = 0);

size_t get_address_count(DataSource *ds);

void extractor_begin_event(DataSource *ex);
void extractor_process_module_data(DataSource *ex, u32 *data, u32 size);
void listfilter_extractor_begin_event(DataSource *ex);
u32 *listfilter_extractor_process_module_data(DataSource *ex, u32 *data, u32 dataSize);


/* ===============================================
 * Operators
 * =============================================== */
struct Operator
{
    using count_type = u8;

    static const auto MaxInputCount  = std::numeric_limits<count_type>::max();
    static const auto MaxOutputCount = std::numeric_limits<count_type>::max();

    ParamVec *inputs;
    ParamVec *inputLowerLimits;
    ParamVec *inputUpperLimits;
    ParamVec *outputs;
    ParamVec *outputLowerLimits;
    ParamVec *outputUpperLimits;
    void *d;
    s16 conditionIndex;
    u8 inputCount;
    u8 outputCount;
    u8 type;
};

void assign_input(Operator *op, PipeVectors input, s32 inputIndex);

Operator make_calibration(
    memory::Arena *arena,
    PipeVectors input,
    double unitMin, double unitMax);

Operator make_calibration(
    memory::Arena *arena,
    PipeVectors input,
    ParamVec calibMinimums,
    ParamVec calibMaximums);

Operator make_calibration_idx(
    memory::Arena *arena,
    PipeVectors input,
    s32 inputInfo,
    double unitMin, double unitMax);

Operator make_keep_previous(
    memory::Arena *arena,
    PipeVectors input,
    bool keepValid);

Operator make_keep_previous_idx(
    memory::Arena *arena,
    PipeVectors input,
    s32 inputIndex,
    bool keepValid);

Operator make_difference(
    memory::Arena *arena,
    PipeVectors inPipeA,
    PipeVectors inPipeB);

Operator make_difference_idx(
    memory::Arena *arena,
    PipeVectors inPipeA,
    PipeVectors inPipeB,
    s32 indexA,
    s32 indexB);

struct ArrayMapData
{
    struct Mapping
    {
        u8 inputIndex;
        s32 paramIndex;
    };

    TypedBlock<Mapping, s32> mappings;
};

Operator make_array_map(
    memory::Arena *arena,
    TypedBlock<PipeVectors, s32> inputs,
    TypedBlock<ArrayMapData::Mapping, s32> mappings);

Operator make_binary_equation(
    memory::Arena *arena,
    PipeVectors inputA,
    PipeVectors inputB,
    u32 equationIndex, // stored right inside the d pointer so it can be at least u32 in size
    double outputLowerLimit,
    double outputUpperLimit);

Operator make_binary_equation_idx(
    memory::Arena *arena,
    PipeVectors inputA,
    PipeVectors inputB,
    s32 inputIndexA,
    s32 inputIndexB,
    u32 equationIndex,
    double outputLowerLimit,
    double outputUpperLimit);

Operator make_range_filter(
    memory::Arena *arena,
    PipeVectors input,
    Thresholds thresholds,
    bool invert);

Operator make_range_filter_idx(
    memory::Arena *arena,
    PipeVectors input,
    s32 inputIndex,
    Thresholds thresholds,
    bool invert);


enum class RectFilterOperation
{
    And,
    Or
};

Operator make_rect_filter(
    memory::Arena *arena,
    PipeVectors xInput,
    PipeVectors yInput,
    s32 xIndex,
    s32 yIndex,
    Thresholds xThresholds,
    Thresholds yThresholds,
    RectFilterOperation filterOp);

Operator make_condition_filter(
    memory::Arena *arena,
    PipeVectors dataInput,
    PipeVectors condInput,
    bool inverted,
    s32 dataIndex = -1,
    s32 condIndex = -1);

/* ===============================================
 * AggregateOps
 * =============================================== */

/* Thresholds to check each input parameter against. If the parameter is not
 * inside [min_threshold, max_threshold] it is not considered for the result.
 */

Operator make_aggregate_sum(
    memory::Arena *arena,
    PipeVectors input,
    Thresholds thresholds);

Operator make_aggregate_multiplicity(
    memory::Arena *arena,
    PipeVectors input,
    Thresholds thresholds);

Operator make_aggregate_min(
    memory::Arena *arena,
    PipeVectors input,
    Thresholds thresholds);

Operator make_aggregate_max(
    memory::Arena *arena,
    PipeVectors input,
    Thresholds thresholds);

Operator make_aggregate_sigma(
    memory::Arena *arena,
    PipeVectors input,
    Thresholds thresholds);

Operator make_aggregate_mean(
    memory::Arena *arena,
    PipeVectors input,
    Thresholds thresholds);

Operator make_aggregate_minx(
    memory::Arena *arena,
    PipeVectors input,
    Thresholds thresholds);

Operator make_aggregate_maxx(
    memory::Arena *arena,
    PipeVectors input,
    Thresholds thresholds);

Operator make_aggregate_meanx(
    memory::Arena *arena,
    PipeVectors input,
    Thresholds thresholds);

Operator make_aggregate_sigmax(
    memory::Arena *arena,
    PipeVectors input,
    Thresholds thresholds);

/* ===============================================
 * Expression Operator
 * =============================================== */
struct ExpressionOperatorError: public std::runtime_error
{
    using std::runtime_error::runtime_error;
};

/* Thrown if the return value of the begin expression is malformed or contains
 * unexpected data types. */
struct ExpressionOperatorSemanticError: public ExpressionOperatorError
{
    std::string message;

    ExpressionOperatorSemanticError(const std::string &msg)
        : ExpressionOperatorError("SemanticError")
        , message(msg)
    {}
};

/* Runtime library containing basic analysis related functions.
 *
 * An instance of the library will automatically be registered for expressions
 * used in the expression operator.
 *
 * Contains the following functions:
 * is_valid(p), is_invalid(p), make_invalid(), is_nan(d)
 */
a2_exprtk::SymbolTable make_expression_operator_runtime_library();

struct ExpressionOperatorData
{
    a2_exprtk::SymbolTable symtab_begin;
    a2_exprtk::SymbolTable symtab_step;
    a2_exprtk::Expression expr_begin;
    a2_exprtk::Expression expr_step;

    std::vector<std::string> output_names;
    std::vector<std::string> output_units;
};

enum class ExpressionOperatorBuildOptions: u8
{
    /* Compiles and evaluates the begin expression and uses the result to build
     * the operator outputs, populate the symbol table for the step expression
     * and the ExpressionOperatorData output_names and output_units. */
    InitOnly,

    /* Performs the InitOnly steps and then compiles the step expression. */
    FullBuild,
};

static const s32 NoParamIndex = -1;

Operator make_expression_operator(
    memory::Arena *arena,
    const std::vector<PipeVectors> &inputs,
    const std::vector<s32> &input_param_indexes,
    const std::vector<std::string> &input_prefixes,
    const std::vector<std::string> &input_units,
    const std::string &expr_begin_str,
    const std::string &expr_step_str,
    ExpressionOperatorBuildOptions options = ExpressionOperatorBuildOptions::FullBuild);

/* Can be used after calling make_expression_operator() with the InitOnly
 * option to complete building the operator. */
void expression_operator_compile_step_expression(Operator *op);

void expression_operator_step(Operator *op);

/* ===============================================
 * Histograms
 * =============================================== */
struct Binning
{
    static const s8 Underflow = -1;
    static const s8 Overflow = -2;
    double min;
    double range;
};

struct H1D: public ParamVec
{
    Binning binning;
    // binningFactor = binCount / binning.range
    double binningFactor;
    double entryCount;
    double underflow;
    double overflow;
};

Operator make_h1d_sink(
    memory::Arena *arena,
    PipeVectors inPipe,
    TypedBlock<H1D, s32> histos);

Operator make_h1d_sink_idx(
    memory::Arena *arena,
    PipeVectors inPipe,
    TypedBlock<H1D, s32> histos,
    s32 inputIndex);

struct H1DSinkData
{
    TypedBlock<H1D, s32> histos;
};

struct H1DSinkData_idx: public H1DSinkData
{
    s32 inputIndex;
};

struct H2D: public ParamVec
{
    enum Axis
    {
        XAxis,
        YAxis,
        AxisCount
    };

    s32 binCounts[AxisCount];
    Binning binnings[AxisCount];
    double binningFactors[AxisCount];
    double entryCount;
    double underflow;
    double overflow;
};

struct H2DSinkData
{
    H2D histo;
    s32 xIndex;
    s32 yIndex;
};

Operator make_h2d_sink(
    memory::Arena *arena,
    PipeVectors xInput,
    PipeVectors yInput,
    s32 xIndex,
    s32 yIndex,
    H2D histo);

//
// RateMonitor
//

enum class RateMonitorType
{
    /* Input values are rates and simply need to be accumulated. */
    PrecalculatedRate,

    /* Input values are counter values. The rate has be calculated from
     * the current and the previous value. */
    CounterDifference,

    /* The rate of hits for an analysis pipe. Basically the rate of a
     * source or the flow through an operator.
     *
     * The event and pipe to be monitored are required. At the end of
     * an event, after operators have been processed, a hitcount value
     * has to be incremented for each input value if the value is
     * valid.
     *
     * Event: the event this sink is in.
     * Pipe:  this operators input pipe.
     * Two step processing: in a2_end_event() increment the count values based on the input.
     * In a new a2_timetick() function run through all RateMonitorSinks
     * and sample from the hitcounts data.
     *
     * Sampling and recording of the resulting rate happens
     * "asynchronously" based on analysis timeticks (system generated
     * during DAQ / from the listfile during replay).
     */
    FlowRate,
};

Operator make_rate_monitor(
    memory::Arena *arena,
    TypedBlock<PipeVectors, s32> inputs,
    TypedBlock<s32, s32> input_param_indexes,
    TypedBlock<RateSampler *, s32> samplers,
    RateMonitorType type);

//
// ExportSink
//

enum class ExportSinkFormat
{
    /* Writes whole arrays with a size prefix. Use if all channels respond for
     * every event. In this case the output data will be smaller than the
     * indexed format. */
    Full,

    /* Indexed/Sparse format:
     * Writes a size prefix and two arrays, the first containing the parameter
     * indices, the second the coressponding values. Only valid values are
     * written out.
     * Use this if only a couple of channels respond per event. In this case it
     * will produce much smaller data than the Full format. */
    Sparse,
};

struct ExportSinkData
{
    // Output filename. May include a path. Is relative to the application
    // working directory which is the workspace directory.
    std::string filename;

    //  0:  turn of compression; makes this operator write directly to the output file
    // -1:  Z_DEFAULT_COMPRESSION
    //  1:  Z_BEST_SPEED
    //  9:  Z_BEST_COMPRESSION
    int compressionLevel;

    // The lowest level output stream. Right now always a std::ofstream
    // working on this operators output filename.
    std::unique_ptr<std::ostream> ostream;

    // ostream used when compression is enabled.
    std::unique_ptr<std::ostream> z_ostream;

    // Condition input index. If negative the condition input will be unused.
    s32 condIndex = -1;

    // runtime state
    u64 eventsWritten = 0;
    u64 bytesWritten  = 0;
    std::string lastError;

    mutable NonRecursiveRWLock lastErrorLock;
    using WriteGuard = WriteLockGuard<NonRecursiveRWLock>;
    using ReadGuard  = ReadLockGuard<NonRecursiveRWLock>;

    std::string getLastError() const
    {
        ReadGuard guard(lastErrorLock);
        return lastError;
    }

    void setLastError(const std::string &msg)
    {
        WriteGuard guard(lastErrorLock);
        lastError = msg;
    }
};

// No condition input. All data will be written to the output file.
Operator make_export_sink(
    memory::Arena *arena,
    const std::string &output_filename,
    int compressionLevel,
    ExportSinkFormat format,
    TypedBlock<PipeVectors, s32> dataInputs
    );

// With condition input. This can dramatically reduce the output data size.
Operator make_export_sink(
    memory::Arena *arena,
    const std::string &output_filename,
    int compressionLevel,
    ExportSinkFormat format,
    TypedBlock<PipeVectors, s32> dataInputs,
    PipeVectors condInput,
    s32 condIndex
    );

//
// A2 structure and entry points
//

static const int MaxVMEEvents  = 12;
static const int MaxVMEModules = 20;

struct A2
{
    std::array<u8, MaxVMEEvents> dataSourceCounts;
    std::array<DataSource *, MaxVMEEvents> dataSources;

    std::array<u8, MaxVMEEvents> operatorCounts;
    std::array<Operator *, MaxVMEEvents> operators;
    std::array<u8 *, MaxVMEEvents> operatorRanks;

    boost::dynamic_bitset<> conditions;

    A2(): conditions()
    {
        dataSourceCounts.fill(0);
        dataSources.fill(nullptr);
        operatorCounts.fill(0);
        operators.fill(nullptr);
        operatorRanks.fill(0);
    }
};

using Logger = std::function<void (const std::string &msg)>;

void a2_begin_run(A2 *a2, Logger logger);
void a2_begin_event(A2 *a2, int eventIndex);
void a2_process_module_data(A2 *a2, int eventIndex, int moduleIndex, u32 *data, u32 dataSize);
void a2_end_event(A2 *a2, int eventIndex);
void a2_timetick(A2 *a2);
void a2_end_run(A2 *a2);

//
// Stuff used for debugging and tests
//

template<typename Out, typename T>
void write_value(Out &out, T value)
{
    out.write(reinterpret_cast<const char *>(&value), sizeof(T));
}

template<typename Out, typename T>
void write_array(Out &out, T *data, size_t size)
{
    out.write(reinterpret_cast<const char *>(data), size * sizeof(T));
}
template<typename Out>
void write_histo(Out &out, H1D histo)
{
    // s32 size
    // double binning.min
    // double binning.range
    // double underflow
    // double overflow
    // double data[size]

    write_value(out, histo.size);
    write_value(out, histo.binning.min);
    write_value(out, histo.binning.range);
    write_value(out, histo.underflow);
    write_value(out, histo.overflow);
    write_array(out, histo.data, histo.size);
}

template<typename Out>
void write_histo_list(Out &out, TypedBlock<H1D, s32> histos)
{
    // s32 histoCount
    write_value(out, histos.size);

    for (s32 i = 0; i < histos.size; i++)
    {
        write_histo(out, histos[i]);
    }
}

} // namespace a2

#endif /* __MVME_A2_H__ */
