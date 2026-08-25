#include "simulation.hpp"
#include "sim-threading.hpp"
#include "sim-data.hpp"
#include "sim-json.hpp"

#include <getopt.h>
#include <iostream>
#include <cassert>
#include <cstdio>
#include <string>
#include <cstdlib>
#include <vector>
#include <algorithm>
#include <stdexcept>
#include <cmath>

using std::vector, std::string, std::min, std::max;


static inline money mabs(money val) noexcept {
    return val >= 0 ? val : -val;
}

TradeDataArray* get_all(const JSON &jin, int last_elems) {
    if( jin["datafile"].size() != 1 ) {
        std::cerr << "Minisim: only 2-coin pools are supported\n";
        exit(1);
    }
    string name = jin["datafile"][0];
    printf("using file '%s'\n", name.c_str());
    vector<OHLC> all_trades = get_data(name);
    TradeDataArray* arr = preprocessOHLC(all_trades, last_elems);
    return arr;
}



struct extra_data {
    money APY = 0;
    money APY_boost = 0;
    money APR_geo_mean = 0;
    money liq_density = 0;
    money slippage = 0;
    money volume = 0;
    money imbalance = 0;
    money imbalance_integral = 0;
};

struct simulation_data {
    int num = 0;
    JSON jconf;
    const TradeDataArray *test_data = nullptr;
    extra_data result;
};


struct Trader {
    Trader(const JSON &jconf, const Prices &p0) :
        fee_model(jconf),
        ext_fee(jconf["ext_fee"]),
        gas_fee(jconf["gas_fee"]),
        curve(Curve::make(jconf["curve"])),
        state0(jconf["D"], p0)
    {
        if( !curve ) {
            throw std::runtime_error("Failed to initialize curve");
        }
        price_oracle.ma_half_time = jconf["ma_half_time"];
        //--
        money D = jconf["D"];
        adjustment_step = jconf["adjustment_step"];
        allowed_extra_profit = jconf["allowed_extra_profit"];

        if (jconf.contains("lp_profit_fraction"))
            this->lp_profit_fraction = jconf["lp_profit_fraction"];
        else
            this->lp_profit_fraction = 0.5L;

        log = jconf["log"];
        this->dx = D * 1e-8L;
        this->xcp_profit = 1.L;
        this->not_adjusted = false;
    }

    void tweak_price_2(const FullAMMState& initial_state,
                       const FullAMMState& oldstate,
                       FullAMMState& state, u64 t, money spot_prev,
                       PriceOracle::State &oracle);

    void simulate(simulation_data *simdata, extra_data *extdata);

    Fee         fee_model;
    PriceOracle price_oracle;
    money dx;
    money xcp_profit;
    money adjustment_step;
    money allowed_extra_profit;
    int log;
    const money ext_fee;
    const money gas_fee;
    money lp_profit_fraction;
    bool not_adjusted;
    std::unique_ptr<Curve> curve;
    AMMState state0;
};

void Trader::simulate(simulation_data *simdata, extra_data *extdata) {
    const size_t total_elements = simdata->test_data->size();
    const price_point* mapped_data = simdata->test_data->array();
    money slippage = 0;
    money imbalance = 0;
    money antislippage = 0;
    money slippage_count = 0;
    money volume = 0;
    money total_vol = 0;
    FullAMMState state(state0, *curve);
    const FullAMMState initial_state = state;
    money last_prices = state.price;
    money imbalance_integral = 0;
    money APY = 0.0;
    money APY_boost = 0.0;
    money boost_integral = 1.0;

    FILE *out_file = nullptr;
    if (log) {
        out_file = fopen("detailed-output.json", "w");
        fprintf(out_file, "[");
    }
    assert(total_elements > 0 );
    //
    constexpr int a = 0;
    constexpr int b = 1;
    const u64   start_t               = mapped_data[0].t;
    long double last_time_tweak_price = mapped_data[0].t;
    PriceOracle::State oracle = price_oracle.init(start_t, state0.price);
    money last                = oracle.price[b] / oracle.price[a];
    //
    for (size_t i = 0; i < total_elements; i++) {
        long double last_time = 0;
        price_point d = mapped_data[i];
        if( i > 0 ) {
            last_time = d.t - mapped_data[i-1].t;
        }

        auto apply_tweak_trade = [&](const FullAMMState& oldst, FullAMMState& st) {
            money ps_before = st.amm.price[1];
            money cur_get_p = curve->computeP(st.amm);
            tweak_price_2(initial_state, oldst, st, d.t, last_prices, oracle);
            last_prices = cur_get_p * ps_before;
            last_time_tweak_price = d.t;
        };

        // Attempt to make arbitrage trade
        {
            Trade trade;                  // On-curve trade
            bool  trade_happened = false; // Flag to check that trade should happen

            // Check whether trade in either direction is possible.
            const money ext_vol = money(d.volume * oracle.price[b]); //  <- now all is in USD
            const money max_price = d.price * (1 - ext_fee);
            const money min_price = d.price * (1 + ext_fee);
            if ((max_price != 0) & (max_price > state.price)) {
                // External Y price is higher. AMM will buy X from and
                // sell Y to arbitrageurs
                auto step = step_for_price_2(state.amm, 0, max_price, 0, ext_vol, *curve, fee_model, gas_fee, dx);
                if (step > 0) {
                    trade_happened = true;
                    trade = Trade(Trade::BUY, step, a, b, state.amm, *curve);
                }
            } else if((min_price != 0) && (min_price < state.price)) {
                // External Y price is lower. AMM will buy Y from and
                // sell X to arbitrageurs
                auto step = step_for_price_2(state.amm, min_price, 0, 0, ext_vol, *curve, fee_model, gas_fee, dx);
                if (step > 0) {
                    trade_happened = true;
                    trade = Trade(Trade::BUY, step, b, a, state.amm, *curve);
                }
            }
            if( trade_happened ) {
                // Apply fee and make trade
                Trade        trade_fee   = trade.applyFee(fee_model.computeFee(state.amm, trade));
                FullAMMState state_trade = FullAMMState(state, trade_fee, *curve);
                xcp_profit += (state_trade.xcp - state.xcp) / initial_state.xcp;
                // Update trade volumes
                const money trade_dx = trade.amountFor(a);
                const money trade_dy = trade.amountFor(b);
                const money p_before = state.price;
                const money p_after  = state_trade.price;
                total_vol += trade_dx * oracle.price[a];
                volume    += trade_dy
                           / (state_trade.amm.xs[b] + state_trade.amm.xs[a] / p_after);
                const money _slippage = (trade_dy * (p_before + p_after))
                                      / (2.L * (mabs(p_before - p_after)) * state_trade.amm.xs[b]);
                // Slippage
                if (_slippage > 1e-10) {
                    slippage_count += last_time;
                    antislippage   += last_time * _slippage;
                    slippage       += last_time / _slippage;
                    // FIXME: cannot compute imbalance
                    // imbalance      += mabs(logl(last / state_trade.amm.price[1])) * curve->A * last_time;
                }
                // Apply correction to a price scale
                FullAMMState state_price = state_trade;
                apply_tweak_trade(state_trade, state_price);
                last  = state_trade.price;
                state = state_price;
            }
        }


        // Boost with special donations to the pool
        money local_boost_rate = fee_model.localBoostRate(state.amm);
        if (local_boost_rate > 0) {
            money _boost = (1.L + last_time * local_boost_rate);
            state.amm.xs[0] = state.amm.xs[0] * _boost;
            state.amm.xs[1] = state.amm.xs[1] * _boost;
            state.compute(*curve);
            boost_integral *= _boost;
        }

        // only tweak_price every N seconds or on trade
        if (d.t - last_time_tweak_price >= 3600) {
            FullAMMState state_ = state; // FIXME: Work duplication!
            apply_tweak_trade(state_, state);
        }

        {
            TokensXP _xp(state.amm);
            money bal_mul = (_xp[0] + _xp[1]);
            bal_mul = 4 * _xp[0] * _xp[1] / (bal_mul * bal_mul);
            imbalance_integral += (1.L - bal_mul) * last_time;  // last_time is dt here
        }

        money ideal_vp = 1 + (xcp_profit - 1) * lp_profit_fraction;
        money ARU_y    = (86400.L * 365.L / (d.t - start_t + 1.L));
        APY            = powl(ideal_vp,                  ARU_y) - 1.L;
        APY_boost      = powl(ideal_vp / boost_integral, ARU_y) - 1.L;

        if (i % 1024 == 0 && log) {
            money xcp_profit_real = state.xcp / initial_state.xcp;
            printf("t=%lu %.1Lf%%\ttrades: 0\tAMM: %.5Lf\tTarget: %.5Lf\tVol: %.4Lf\tPR:%.2Lf\txCP-growth: {%.10Lf}\tAPY:%.1Lf%%\ttw_apr:0.0%%\tfee:%.3Lf%% .\n",
                   d.t,
                   100.L * i / total_elements,
                   last,
                   state.amm.price.p[1],
                   total_vol,
                   (xcp_profit_real - 1.) / (xcp_profit - 1.L),
                   xcp_profit_real,
                   APY * 100.L,
                   fee_model.computeFee(state.amm) * 100.L);
        }

        if (log) {
            money xcp_profit_real = state.xcp / initial_state.xcp;
            fprintf(out_file, "{\"t\": %lu, \"token0\": %.6Le, \"token1\": %.6Le, \"price_oracle\": %.6Le, \"price_scale\": %.6Le, \"profit\": %.6Le, \"xcp\": %.6Le, \"boost_rate\": %.6Le}",
                    d.t,
                    state.amm.xs[0],
                    state.amm.xs[1],
                    oracle.price[b] / oracle.price[a],
                    state.amm.price[1],
                    xcp_profit_real - 1.0,
                    xcp_profit,
                    local_boost_rate);
            if (i < total_elements - 1) {
                fprintf(out_file, ",\n");
            }
        }

        if (slippage > 1e20 and slippage_count > 0) {
            printf("*** Slippage is too high %.5Lf\n", slippage);
        }
    }
    extdata->imbalance_integral = imbalance_integral / (oracle.time - start_t + 1.L);
    extdata->slippage = slippage / slippage_count / 2.L;
    extdata->imbalance = imbalance / slippage_count / 2.L;
    extdata->liq_density = 2.L * antislippage / slippage_count;
    extdata->APY = APY;
    extdata->volume = volume;
    extdata->APY_boost = APY_boost;
    extdata->APR_geo_mean = 0;

    if (log) {
        fprintf(out_file, "]");
    }
}

void Trader::tweak_price_2(const FullAMMState& initial_state,
                           const FullAMMState& oldstate,
                           FullAMMState& state,
                           u64 t,
                           money spot_prev,
                           PriceOracle::State &oracle
    )
{
    const int N = 2;

    // --- Feed the EMA with the pool's own spot (pre-fee marginal price),
    //     coin0 per coin1, computed at the current state.
    const money amm_p01 = spot_prev;             // dx/dy (coin0 per coin1)
    // money amm_p01 = price_2(0, 1);
    // Optional: cap like the real pool (avoid extreme oracle jumps)
    const money capped_p01 = std::min(amm_p01, 2.L * state.amm.price[1]);

    const Prices spot = {1.L, capped_p01};    
    oracle.record(t, spot);


    // # price_oracle looks like [1, p1, p2, ...] normalized to 1e18
    money S = 0;
    for (size_t i = 0; i < N; i++) {
        auto t = oracle.price[i] / state.amm.price[i] - 1.L;
        S += t*t;
    }
    const money norm = sqrt(S);
    const money _adjustment_step = min(adjustment_step, norm / 5);
    if (norm <= _adjustment_step) {
        // Already close to the target price
        return;
    }

    {
        money xcp_profit_real = state.xcp / initial_state.xcp;
        if (not not_adjusted and (xcp_profit_real > xcp_profit * lp_profit_fraction + (1.L - lp_profit_fraction) + allowed_extra_profit)) {
            not_adjusted = true;
        }
        if (not not_adjusted) {
            return;
        }
    }

    FullAMMState old_state = state;
    {
        auto p_target = state.amm.price.p[1];
        auto p_real   = oracle.price[1];
        state.amm.price.p[1] = p_target + _adjustment_step * (p_real - p_target) / norm;
    }
    state.compute(*curve);

    {
        money xcp_profit_real = state.xcp / initial_state.xcp;
        if (xcp_profit_real <= xcp_profit * lp_profit_fraction + (1.L - lp_profit_fraction)) {
            //  If real profit is less than equilibrium - revert params back
            state = old_state;
            not_adjusted = false;
        }
    }
}


class SimulationTask : public Workload {
public:
    SimulationTask(simulation_data _simdata, JSON *_result) :
        simdata(_simdata),
        result(_result)
    {}
    simulation_data simdata;
    JSON *result;

    virtual void work();
    virtual void fini();

    virtual ~SimulationTask() = default;
private:
};

void SimulationTask::work() {
    int tid = 0;
    printf("[%d]: pick up configuration %d\n", tid, simdata.num);
    Trader trader(simdata.jconf, simdata.test_data->initialPriceScale());
    auto start_simulation = get_thread_time();
    printf("Configuration %d: begin simulation\n", simdata.num);
    extra_data extdata;
    trader.simulate(&simdata, &extdata);
    simdata.result = extdata;
    printf("Liquidity density vs that of xyz=k: %Lf\n", extdata.liq_density);
    printf("APY-boost: %Lf%%\n", extdata.APY_boost * 100.L);
    printf("APR-geo-mean: %Lf%%\n", extdata.APR_geo_mean * 100.L);
    printf("APY: %Lf%%\n", extdata.APY * 100.L);
    auto end = get_thread_time();
    print_clock("Total simulation time", start_simulation, end);
}

void SimulationTask::fini() {
    JSON::ref dst = (*result)["configuration"][simdata.num]["Result"];
    dst["APY"]                = simdata.result.APY;
    dst["liq_density"]        = simdata.result.liq_density;
    dst["slippage"]           = simdata.result.slippage;
    dst["imbalance"]          = simdata.result.imbalance;
    dst["volume"]             = simdata.result.volume;
    dst["APY_boost"]          = simdata.result.APY_boost;
    dst["APR_geo_mean"]       = simdata.result.APR_geo_mean;
    dst["imbalance_integral"] = simdata.result.imbalance_integral;
}


static void usage(std::ostream& out) {
    out << "Usage: foo [options] <job.json>\n"
        << "  --threads N         run using number of threads (default: 1)\n"
        << "  --trim N            use only N last elements in time series\n"
        << "  -r, --result FILE   write summary results to FILE\n";
}

int main(int argc, char **argv) {
    int param_threads = 1;
    int param_trim    = 0;
    std::string job_file;
    std::string result_file;
    // Parse command line arguments using getopt
    {
        static const option long_opts[] = {
            {"threads", required_argument, nullptr, 't'},
            {"trim",    required_argument, nullptr, 1001},
            {"result",  required_argument, nullptr, 'r'},
            {"help",    no_argument,       nullptr, 'h'},
            {nullptr, 0, nullptr, 0}
        };
        while(true) {
            int opt = getopt_long(argc, argv, "t:r:h", long_opts, nullptr);
            if( opt == -1 ) { break; }
            switch (opt) {
            case 't':
                param_threads = std::stoi(optarg);
                break;
            case 1001:
                param_trim = std::stoi(optarg);
                break;
            case 'r':
                result_file = optarg;
                break;
            case 'h':
                usage(std::cout);
                return 0;
            default:
                return 1;
            }
        }
        if (optind >= argc) {
            std::cerr << "Error: missing mandatory positional argument\n";
            return 1;
        } else if (optind + 1 < argc) {
            std::cerr << "Error: too many positional arguments\n";
            return 1;
        }
        job_file = argv[optind];
    }
    // Run simultion 
    try {
        double real_time_start = get_total_time();
        JSON jin;
        jin.load_file(job_file);
        int configurations = jin["configuration"].size();
        if (configurations <= 0) {
            printf("No configurations found\n");
            return 0;
        }
        printf("Total %d configurations will be processed in %d threads\n", configurations, param_threads);
        std::unique_ptr<TradeDataArray> test_data(get_all(jin, param_trim));

        double time_start      = get_total_time();
        double wall_time_start = get_wall_time();
        WorkQueue work_queue(param_threads);
        JSON result = jin;
        for (int i = 0; i < configurations; i++) {
            simulation_data cd;
            cd.num = i;
            cd.test_data = &*test_data;
            cd.jconf = jin["configuration"][i];
            work_queue.enqueue(new SimulationTask(cd, &result));
        }
        work_queue.start();
        work_queue.join();

        result.save_file(result_file);
        double time_end = get_total_time();
        double wall_time_end = get_wall_time();
        print_clock("Data reading and preprocessing time", real_time_start, time_start);
        print_clock("Total simulation wall time", wall_time_start, wall_time_end);
        print_clock("Total simulation processor time", time_start, time_end);
        printf("Parallelizm ratio %.5lf\n", (time_end - time_start) / (wall_time_end - wall_time_start));
    }
    catch ( const std::exception &e ) {
        std::cerr << "Error:   " << e.what()         << std::endl;
        std::cerr << "Of type: " << typeid(e).name() << std::endl;
        return 1;
    }
    return 0;
}
