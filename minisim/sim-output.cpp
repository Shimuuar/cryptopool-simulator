#include "sim-output.hpp"

SimOuput::~SimOuput() {}

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
        int   m_rows;
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

std::unique_ptr<SimOuput> makeOutputJSON(std::string& path) {
    return std::make_unique<SimOuputJSON>(path);
}
