#pragma once
// Unitilites for writing results of simulation
#include "simulation.hpp"
#include <memory>


// Interface for generating detailed output
class SimOuput {
public:
    virtual ~SimOuput();
    // Record single simulation point
    virtual void recordPoint(
        u64 t,
        const FullAMMState& initial_state,
        const FullAMMState& state,
        PriceOracle::State& oracle,
        money xcp_profit,
        money local_boost_rate
        ) = 0;
};


// Write output into JSON file
std::unique_ptr<SimOuput> makeOutputJSON(std::string& path);
