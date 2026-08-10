#include "simulation.hpp"
#include "sim-threading.hpp"
#include "sim-data.hpp"

#include <iostream>
#include <cassert>
#include <cstdio>
#include <map>
#include <string>
#include <cstdlib>
#include <vector>
#include <algorithm>
#include <utility>
#include <stdexcept>
#include <cmath>
#include <fstream>
#include <iomanip>
#include "json.hpp"

using nlohmann::json;
using std::vector, std::string, std::pair, std::sort, std::map, std::min, std::max;


static inline money mabs(money val) noexcept {
    return val >= 0 ? val : -val;
}

TradeDataArray* get_all(json const &jin, int last_elems) {
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
    money APY_boost_2 = 0;
    money APR_geo_mean = 0;
    money liq_density = 0;
    money slippage = 0;
    money volume = 0;
    money imbalance = 0;
    money imbalance_integral = 0;
};

struct simulation_data {
    int num = 0;
    json const *jconf = nullptr;
    const TradeDataArray *test_data = nullptr;
    extra_data result;
};


struct Trader {
    Trader(json const &jconf, const Prices &p0) :
        ext_fee(jconf["ext_fee"]),
        gas_fee(jconf["gas_fee"]),
        curve(jconf["A"], jconf["gamma"]),
        state0(jconf["D"], p0)
    {
        price_oracle.ma_half_time = jconf["ma_half_time"];
        //--
        fee_model.mid_fee   = jconf["mid_fee"];
        fee_model.out_fee   = jconf["out_fee"];
        fee_model.fee_gamma = jconf["fee_gamma"];
        fee_model.boost_mul = jconf["boost_mul"];
        fee_model.boost_rate = jconf["boost_rate"];
        fee_model.boost_rate = fee_model.boost_rate / (86400L * 365L);
        //-
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

    money step_for_price_2(const AMMState& state, money p_min, money p_max, money vol, money ext_vol);

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
    const Curve curve;
    AMMState state0;
};

money Trader::step_for_price_2(const AMMState& state0, money p_min, money p_max, money vol, money ext_vol) {
    AMMState state = state0;
    Tokens x0 = state.xs;
    money _dx = 0;
    money _dy = 0;
    money x = 0;
    money y = 0;
    money price = 0;
    int _from = 0;
    int _to   = 1;
    if (p_min > 0) {
        _from = 1;
        _to = 0;
    }
    auto step0 = dx / state.price[_from];  // step in units of currency being sold
    auto step = step0;
    money gas = gas_fee / state.price[_from];

    money previous_profit = 0;

    // + (step increases)
    while (true) {
        auto _dx_prev = _dx;
        auto _dy_prev = _dy;

        _dx += step;

        // buy  -> x: first, y: second
        // sell -> x: second, y: first

        x = x0[_from] + _dx;
        y = curve.y_2(state, x, _from, _to);

        state.xs[_from] = x;
        state.xs[_to] = y;
        auto fee_mul = 1.L - this->fee_model.computeFee(state);

        _dy = (x0[_to] - y) * fee_mul;
        state.xs[_to] = x0[_to] - _dy;

        // price in units d_first / d_second
        if (_from == 0) {
            price = _dx / _dy;
        } else {
            price = _dy / _dx;
        }
        auto v = vol + _dy * state.price[_to];

        state.xs = x0;  // restore the state

        // _from == p.first - buy
        // _from != p.first - sell
        money new_profit;
        if (_from == 0)
            new_profit = (_dx / price - _dx / p_max) * p_max;
        else
            new_profit = (price - p_min) * _dx;

        // printf("*** price=%Lf, min=%Lf, max=%Lf, _dx=%Le, new_p=%Lf, pr_p=%Lf\n", price, p_min, p_max, _dx, new_profit, previous_profit);

        if (new_profit > previous_profit and v <= ext_vol / 2.L) {
            previous_profit = new_profit;
        } else {
            _dx = _dx_prev;
            _dy = _dy_prev;
            break;
        }
        step += step;
    }

    // - (step decreases)
    while (true) {
        auto _dx_prev = _dx;
        auto _dy_prev = _dy;
        if (step < 0) step = -step;
        step /= 2;

        if (step < step0) {
            break;
        }

        for (int ctr=0;ctr<2;ctr++) {
            step = -step;
            _dx = _dx_prev + step;

            x = x0[_from] + _dx;
            y = curve.y_2(state, x, _from, _to);

            state.xs[_from] = x;
            state.xs[_to] = y;
            auto fee_mul = 1.L - this->fee_model.computeFee(state);

            _dy = (x0[_to] - y) * fee_mul;
            state.xs[_to] = x0[_to] - _dy;

            if (_from == 0) {
                price = _dx / _dy;
            }
            else {
                price = _dy / _dx;
            }
            auto v = vol + _dy * state.price[_to];
            state.xs = x0;  // restore the state

            // _from == p.first - buy
            // _from != p.first - sell
            money new_profit;
            if (_from == 0)
                new_profit = (_dx / price - _dx / p_max) * p_max;
            else
                new_profit = (price - p_min) * _dx;

            if (new_profit > previous_profit and v <= ext_vol / 2.L) {
                previous_profit = new_profit;
                break;
            } else {
                _dx = _dx_prev;
                _dy = _dy_prev;
            }
        }
    }

    if (_from == 0) {
        price = (_dx + gas) / _dy;  // need to buy higher than without gas
        previous_profit = (_dx / price - _dx / p_max) * p_max;
    }
    else {
        price = _dy / (_dx + gas); // need to sell lower than without gas
        previous_profit = (price - p_min) * _dx;
    }

    if (previous_profit <= 0) _dx = 0;
    return _dx;
}


void Trader::simulate(simulation_data *simdata, extra_data *extdata) {
    const size_t total_elements = simdata->test_data->size();
    const price_point* mapped_data = simdata->test_data->array();
    money xcp_profit_real_prev = 1.L;
    money xcp_profit_real_adj = 1.L;
    money slippage = 0;
    money imbalance = 0;
    money antislippage = 0;
    money slippage_count = 0;
    money volume = 0;
    money total_vol = 0;
    FullAMMState state(state0, curve);
    const FullAMMState initial_state = state;
    money last_prices = curve.price_2(state.amm);
    money imbalance_integral = 0;
    money APY = 0.0;
    money APY_boost = 0.0;
    money APY_boost_2 = 0.0;
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
            money cur_get_p = curve.p_2(st.amm);
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
                auto step = step_for_price_2(state.amm, 0, max_price, 0, ext_vol);
                if (step > 0) {
                    trade_happened = true;
                    trade = Trade(Trade::BUY, step, a, b, state.amm, curve);
                }
            } else if((min_price != 0) && (min_price < state.price)) {
                // External Y price is lower. AMM will buy Y from and
                // sell X to arbitrageurs
                auto step = step_for_price_2(state.amm, min_price, 0, 0, ext_vol);
                if (step > 0) {
                    trade_happened = true;
                    trade = Trade(Trade::BUY, step, b, a, state.amm, curve);
                }
            }
            if( trade_happened ) {
                // Apply fee and make trade
                Trade        trade_fee   = trade.applyFee(fee_model.computeFee(state.amm, trade));
                FullAMMState state_trade = FullAMMState(state, trade_fee, curve);
                xcp_profit += (state_trade.xcp - state.xcp) / initial_state.xcp;
                // Update trade volumes
                total_vol += trade.amountFor(a) * oracle.price[a];
                const money trade_dy = trade.amountFor(b);
                const money p_before = state.price;
                const money p_after  = state_trade.price;
                volume += trade_dy
                        / (state_trade.amm.xs[b] + state_trade.amm.xs[a] / p_after);
                const money _slippage = (trade_dy * (p_before + p_after))
                                      / (2.L * (mabs(p_before - p_after)) * state_trade.amm.xs[b]);
                // Slippage
                if (_slippage > 1e-10) {
                    slippage_count += last_time;
                    antislippage   += last_time * _slippage;
                    slippage       += last_time / _slippage;
                    imbalance      += mabs(logl(last / state_trade.amm.price[1])) * curve.A * last_time;
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
            state.compute(curve);
            boost_integral *= _boost;
        }

        // only tweak_price every N seconds or on trade
        if (d.t - last_time_tweak_price >= 3600) {
            FullAMMState state_ = state; // FIXME: Work duplication!
            apply_tweak_trade(state_, state);
        }

        TokensXP _xp;
        state.amm.getXP(_xp);
        {
            money bal_mul = (_xp[0] + _xp[1]);
            bal_mul = 4 * _xp[0] * _xp[1] / (bal_mul * bal_mul);
            imbalance_integral += (1.L - bal_mul) * last_time;  // last_time is dt here
        }

        money ideal_vp = xcp_profit * lp_profit_fraction + (1.L - lp_profit_fraction);
        xcp_profit_real_adj *= (ideal_vp / xcp_profit_real_prev);
        xcp_profit_real_prev = ideal_vp;

        long double ARU_x = ideal_vp;
        long double ARU_y = (86400.L * 365.L / (d.t - start_t + 1.L));
        APY         = powl(ARU_x, ARU_y) - 1.L;
        APY_boost   = powl(ideal_vp            / boost_integral, ARU_y) - 1.L;
        APY_boost_2 = powl(xcp_profit_real_adj / boost_integral, ARU_y) - 1.L;
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
    extdata->APY_boost_2 = APY_boost_2;
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
    state.compute(curve);

    {
        money xcp_profit_real = state.xcp / initial_state.xcp;
        if (xcp_profit_real <= xcp_profit * lp_profit_fraction + (1.L - lp_profit_fraction)) {
            //  If real profit is less than equilibrium - revert params back
            state = old_state;
            not_adjusted = false;
        }
    }
}


static bool json_load(string const &name, json &j) {
    try {
        std::ifstream ifl(name);
        if (!ifl) throw std::logic_error("can't open file " + name);
        ifl >> j;
    } catch (std::exception const &ex) {
        printf("json_load: %s\n", ex.what());
        return false;
    }
    return true;
}

static bool json_save(string const &name, json const &j) {
    try {
        std::ofstream ofl(name);
        if (!ofl) throw std::logic_error("can't create file " + name);
        ofl << std::setw(4) << j << "\n";
    } catch (std::exception const &ex) {
        printf("json_load: %s\n", ex.what());
        return false;
    }
    return true;
}


class SimulationTask : public Workload {
public:
    SimulationTask(simulation_data _simdata, json *_result) :
        simdata(_simdata),
        result(_result)
    {}
    simulation_data simdata;
    json *result;

    virtual void work();
    virtual void fini();

    virtual ~SimulationTask() = default;
private:
};

void SimulationTask::work() {
    int tid = 0;
    printf("[%d]: pick up configuration %d\n", tid, simdata.num);
    Trader trader(*(simdata.jconf), simdata.test_data->initialPriceScale());
    auto start_simulation = get_thread_time();
    printf("Configuration %d: begin simulation\n", simdata.num);
    extra_data extdata;
    trader.simulate(&simdata, &extdata);
    simdata.result = extdata;
    printf("Liquidity density vs that of xyz=k: %Lf\n", extdata.liq_density);
    printf("APY-boost: %Lf%%\n", extdata.APY_boost * 100.L);
    printf("APY-boost-2: %Lf%%\n", extdata.APY_boost_2 * 100.L);
    printf("APR-geo-mean: %Lf%%\n", extdata.APR_geo_mean * 100.L);
    printf("APY: %Lf%%\n", extdata.APY * 100.L);
    auto end = get_thread_time();
    print_clock("Total simulation time", start_simulation, end);
}

void SimulationTask::fini() {
    json& dst = (*result)["configuration"][simdata.num]["Result"];
    dst["APY"]                = simdata.result.APY;
    dst["liq_density"]        = simdata.result.liq_density;
    dst["slippage"]           = simdata.result.slippage;
    dst["imbalance"]          = simdata.result.imbalance;
    dst["volume"]             = simdata.result.volume;
    dst["APY_boost"]          = simdata.result.APY_boost;
    dst["APY_boost_2"]        = simdata.result.APY_boost_2;
    dst["APR_geo_mean"]       = simdata.result.APR_geo_mean;
    dst["imbalance_integral"] = simdata.result.imbalance_integral;
}

int main(int argc, char **argv) {
    if (argc == 1) {
        printf("Usage: %s [trim] [threads=#] [in-json-file] [out-json-file]\n", argv[0]);
        return 0;
    }
    int LAST_ELEMS = 0;
    if (argc > 1 && std::string(argv[1]).find("trim") != std::string::npos) {
        if (argv[1][4] == 0) LAST_ELEMS = 1000000;
        else                 LAST_ELEMS = atoi(argv[1]+4);
        argc--; argv++;
    }
    int THREADS = 1;
    if (argc > 1 && std::string(argv[1]).find("threads=") != std::string::npos) {
        THREADS = atoi(argv[1]+8);
        argc--; argv++;
    }
    string in_json_name = argc > 1 ? argv[1] : "sample_in.json";
    string out_json_name = argc > 2 ? argv[2] : "sample_out.json";
    double real_time_start = get_total_time();
    json jin;
    if (!json_load(in_json_name, jin)) {
        return 0;
    }
    int configurations = jin["configuration"].size();
    if (configurations <= 0) {
        printf("No configurations found\n");
        return 0;
    }

    printf("Total %d configurations will be processed in %d threads\n", configurations, THREADS);

    std::unique_ptr<TradeDataArray> test_data(
        get_all(jin, LAST_ELEMS));
    double time_start      = get_total_time();
    double wall_time_start = get_wall_time();

    WorkQueue work_queue(THREADS);
    json result = jin;
    for (int i = 0; i < configurations; i++) {
        simulation_data cd;
        cd.num = i;
        cd.test_data = &*test_data;
        cd.jconf = &jin["configuration"][i];
        work_queue.enqueue(new SimulationTask(cd, &result));
    }
    work_queue.start();
    work_queue.join();

    json_save(out_json_name, result);
    double time_end = get_total_time();
    double wall_time_end = get_wall_time();
    print_clock("Data reading and preprocessing time", real_time_start, time_start);
    print_clock("Total simulation wall time", wall_time_start, wall_time_end);
    print_clock("Total simulation processor time", time_start, time_end);
    printf("Parallelizm ratio %.5lf\n", (time_end - time_start) / (wall_time_end - wall_time_start));
}
