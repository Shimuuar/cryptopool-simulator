#include "sim-output.hpp"

#ifdef SUPPORT_PARQUET
#include <arrow/api.h>
#include <arrow/io/file.h>
#include <parquet/arrow/writer.h>
#include <parquet/properties.h>
#endif

#include <stdexcept>
#include <string>
#include <vector>

SimOuput::~SimOuput() {}

// ----------------------------------------------------------------
// -- JSON

namespace {
    class SimOuputJSON : public SimOuput {
    public:
        SimOuputJSON(const std::string& name);
        virtual ~SimOuputJSON();
        void recordPoint(
            u64 t,
            const FullAMMState& initial_state,
            const FullAMMState& state,
            PriceOracle::State& oracle,
            money xcp_profit,
            money local_boost_rate
        ) override;
    private:
        FILE* m_file;
        int   m_rows = 0;
    };       
}

SimOuputJSON::SimOuputJSON(const std::string& name) {
    m_file = fopen(name.c_str(), "w");
    if( !m_file ) {
        throw std::runtime_error("Cannot open file for detailed output");
    }
    fprintf(m_file, "[\n");
}

SimOuputJSON::~SimOuputJSON() {
    fprintf(m_file, "\n]\n");
    fclose(m_file);
}

void SimOuputJSON::recordPoint(
    u64 t,
    const FullAMMState& initial_state,
    const FullAMMState& state,
    PriceOracle::State& oracle,
    money xcp_profit,
    money local_boost_rate
    )
{
    if( m_rows > 0 ) {
        fprintf(m_file, ",\n");
    }
    m_rows++;
    money xcp_profit_real = state.xcp / initial_state.xcp;
    // NOTE: 2-coin specific
    const int a = 0;
    const int b = 1;
    fprintf(m_file, "{\"t\": %lu, \"token0\": %.6Le, \"token1\": %.6Le, \"price_oracle\": %.6Le, \"price_scale\": %.6Le, \"profit\": %.6Le, \"xcp\": %.6Le, \"boost_rate\": %.6Le}",
            t,
            state.amm.xs[0],
            state.amm.xs[1],
            oracle.price[b] / oracle.price[a],
            state.amm.price[1],
            xcp_profit_real - 1.0,
            xcp_profit,
            local_boost_rate);
}

std::unique_ptr<SimOuput> makeOutputJSON(const std::string& path) {
    return std::make_unique<SimOuputJSON>(path);
}


// ----------------------------------------------------------------
// -- Parquet

#ifdef SUPPORT_PARQUET

static void throwOnError(const arrow::Status& status, const char* what) {
    if( !status.ok() ) {
        throw std::runtime_error(std::string(what) + ": " + status.ToString());
    }
}

namespace {
    class SimOuputParquet : public SimOuput {
    public:
        SimOuputParquet(const std::string& name);
        virtual ~SimOuputParquet();
        void recordPoint(
            u64 t,
            const FullAMMState& initial_state,
            const FullAMMState& state,
            PriceOracle::State& oracle,
            money xcp_profit,
            money local_boost_rate
        ) override;
    private:
        // Maximum number of points kept in memory before being flushed
        static constexpr int64_t BATCH_SIZE = 1 << 16;

        void flush();

        std::shared_ptr<arrow::Schema>               m_schema;
        std::shared_ptr<arrow::io::FileOutputStream> m_sink;
        std::unique_ptr<parquet::arrow::FileWriter>  m_writer;
        std::unique_ptr<arrow::UInt64Builder>        m_t;
        std::unique_ptr<arrow::DoubleBuilder>        m_token0;
        std::unique_ptr<arrow::DoubleBuilder>        m_token1;
        std::unique_ptr<arrow::DoubleBuilder>        m_price_oracle;
        std::unique_ptr<arrow::DoubleBuilder>        m_price_scale;
        std::unique_ptr<arrow::DoubleBuilder>        m_profit;
        std::unique_ptr<arrow::DoubleBuilder>        m_xcp;
        std::unique_ptr<arrow::DoubleBuilder>        m_boost_rate;
        int64_t                                      m_rows = 0;
    };
}

SimOuputParquet::SimOuputParquet(const std::string& name) {
    auto sink = arrow::io::FileOutputStream::Open(name);
    throwOnError(sink.status(), "Cannot open file for parquet output");
    m_sink   = *sink;
    m_schema = arrow::schema({
        arrow::field("t",            arrow::uint64()),
        arrow::field("token0",       arrow::float64()),
        arrow::field("token1",       arrow::float64()),
        arrow::field("price_oracle", arrow::float64()),
        arrow::field("price_scale",  arrow::float64()),
        arrow::field("profit",       arrow::float64()),
        arrow::field("xcp",          arrow::float64()),
        arrow::field("boost_rate",   arrow::float64()),
    });

    auto properties      = parquet::WriterProperties::Builder().build();
    auto arrow_properties = parquet::ArrowWriterProperties::Builder()
                                .set_use_threads(false)
                                ->build();

    auto writer = parquet::arrow::FileWriter::Open(
        *m_schema, arrow::default_memory_pool(), m_sink,
        properties, arrow_properties);
    throwOnError(writer.status(), "Cannot create parquet writer");
    m_writer = std::move(*writer);

    m_t            = std::make_unique<arrow::UInt64Builder>();
    m_token0       = std::make_unique<arrow::DoubleBuilder>();
    m_token1       = std::make_unique<arrow::DoubleBuilder>();
    m_price_oracle = std::make_unique<arrow::DoubleBuilder>();
    m_price_scale  = std::make_unique<arrow::DoubleBuilder>();
    m_profit       = std::make_unique<arrow::DoubleBuilder>();
    m_xcp          = std::make_unique<arrow::DoubleBuilder>();
    m_boost_rate   = std::make_unique<arrow::DoubleBuilder>();
}

SimOuputParquet::~SimOuputParquet() {
    // Destructors must not throw, but still make a best effort to write
    // out the buffered points and close the file.
    try {
        flush();
        if( m_writer ) {
            (void)m_writer->Close();
        }
        if( m_sink ) {
            (void)m_sink->Close();
        }
    } catch( ... ) {
    }
}

void SimOuputParquet::recordPoint(
    u64 t,
    const FullAMMState& initial_state,
    const FullAMMState& state,
    PriceOracle::State& oracle,
    money xcp_profit,
    money local_boost_rate
    )
{
    money xcp_profit_real = state.xcp / initial_state.xcp;
    // NOTE: 2-coin specific
    const int a = 0;
    const int b = 1;

    throwOnError(m_t->Append(t), "parquet append t");
    throwOnError(m_token0->Append(static_cast<double>(state.amm.xs[0])), "parquet append token0");
    throwOnError(m_token1->Append(static_cast<double>(state.amm.xs[1])), "parquet append token1");
    throwOnError(m_price_oracle->Append(static_cast<double>(oracle.price[b] / oracle.price[a])), "parquet append price_oracle");
    throwOnError(m_price_scale->Append(static_cast<double>(state.amm.price[1])), "parquet append price_scale");
    throwOnError(m_profit->Append(static_cast<double>(xcp_profit_real - 1.0L)), "parquet append profit");
    throwOnError(m_xcp->Append(static_cast<double>(xcp_profit)), "parquet append xcp");
    throwOnError(m_boost_rate->Append(static_cast<double>(local_boost_rate)), "parquet append boost_rate");

    if( ++m_rows >= BATCH_SIZE ) {
        flush();
    }
}

void SimOuputParquet::flush() {
    if( m_rows == 0 ) {
        return;
    }
    std::shared_ptr<arrow::Array> t;
    std::shared_ptr<arrow::Array> token0;
    std::shared_ptr<arrow::Array> token1;
    std::shared_ptr<arrow::Array> price_oracle;
    std::shared_ptr<arrow::Array> price_scale;
    std::shared_ptr<arrow::Array> profit;
    std::shared_ptr<arrow::Array> xcp;
    std::shared_ptr<arrow::Array> boost_rate;
    throwOnError(m_t->Finish(&t),                       "parquet finish t");
    throwOnError(m_token0->Finish(&token0),             "parquet finish token0");
    throwOnError(m_token1->Finish(&token1),             "parquet finish token1");
    throwOnError(m_price_oracle->Finish(&price_oracle), "parquet finish price_oracle");
    throwOnError(m_price_scale->Finish(&price_scale),   "parquet finish price_scale");
    throwOnError(m_profit->Finish(&profit),             "parquet finish profit");
    throwOnError(m_xcp->Finish(&xcp),                   "parquet finish xcp");
    throwOnError(m_boost_rate->Finish(&boost_rate),     "parquet finish boost_rate");

    auto batch = arrow::RecordBatch::Make(
        m_schema, m_rows,
        {t, token0, token1, price_oracle, price_scale, profit, xcp, boost_rate});
    throwOnError(m_writer->WriteRecordBatch(*batch), "parquet write batch");
    m_rows = 0;
}

#endif

std::unique_ptr<SimOuput> makeOutputParquet(const std::string& path) {
#ifdef SUPPORT_PARQUET
    return std::make_unique<SimOuputParquet>(path);
#else
    throw std::runtime_error("minisim is built without parquet support");
#endif
}
