#include "simulation.hpp"
#include "sim-threading.hpp"
#include "sim-util.hpp"

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
#include <deque>

using nlohmann::json;
using std::vector, std::string, std::pair, std::sort, std::map, std::min, std::max;


static void print_clock(string const &mesg, double start, double end) {
    printf("%s %.3lf sec\n", mesg.c_str(), double(end - start));
}

struct trade_data {
    u64 t = 0;          // 0
    money open = 0;    // 1
    money high = 0;    // 2
    money low = 0;     // 3
    money close = 0;   // 4
    money volume = 0;  // 5
    void print() const {
        printf("{ open: %.6Lf, high: %.6Lf low: %.6Lf close: %.6Lf t: %lu, volume: %.6Lf } ",
               this->open, this->high, this->low, this->close, this->t, this->volume);
    }
};

static inline money mabs(money val) noexcept {
    return val >= 0 ? val : -val;
}

class TradeDataArray {
public:
    virtual size_t size() const = 0;
    virtual const trade_data* array() const = 0;
    virtual ~TradeDataArray() = default;
};

class TradeDataVector: public TradeDataArray {
public:
    TradeDataVector(const std::vector<trade_data>& vec) :
        m_vec(vec)
    {}
    TradeDataVector(std::vector<trade_data>&& vec) :
        m_vec(vec)
    {}
    virtual ~TradeDataVector() = default;

    virtual size_t            size()  const { return m_vec.size(); }
    virtual const trade_data* array() const { return &m_vec[0]; }
private:
    std::vector<trade_data> m_vec;
};



// py: returns list of dicts ['t'->u64, 'open'->float, 'high'->float, 'low'->float, 'close'->float, 'volume'->float]
// c++ returns vector of struct datum
vector<trade_data> get_data(std::string const &fname) {
    auto start_time = get_thread_time();
    auto name_to_open = "download/" + fname + ".json";
    printf("parsing %s\n", name_to_open.c_str());
    MMappedFile mf( name_to_open );
    vector<trade_data> ret;
    // FIXME: We may well go past data
    auto p = mf.buffer();
    if (*p == '[') p++; // skip initial '[';
    auto scan_double = [] (const unsigned char *p, long double *d) {
        if (*p == '"') p++;
        *d = atof((char *)p);
        while (*p != '"' && *p!= ' ' && *p != ',' && *p != ']') p++;
        while (*p == ',' || *p == ' ' || *p == '"') p++;
        return p;
    };
    auto scan_u64 = [] (const unsigned char *p, u64 *d) {
        u64 ret = 0;
        while (*p >= '0' && *p <= '9') {
            ret = ret * 10 + *p - '0';
            p++;
        }
        while (*p == ',' || *p == ' ') p++;
        *d = ret;
        return p;
    };
    while (*p != ']') {
        // [1503443580000, "3984.00000000", "3984.00000000", "3984.00000000", "3984.00000000", "0.46619400", 1503443639999, "1857.31689600", 2, "0.00000000", "0.00000000", "11761.90492277"],
        if (*p == '[') {
            trade_data d;
            p++;
            p = scan_u64(p, &d.t);
            if (d.t > 10000000000) {
                d.t /= 1000;
            }
            p = scan_double(p, &d.open);
            p = scan_double(p, &d.high);
            p = scan_double(p, &d.low);
            if (d.high < d.low) {
                auto _high = d.low;
                d.low = d.high;
                d.high = _high;
            }
            p = scan_double(p, &d.close);
            p = scan_double(p, &d.volume);
            ret.push_back(d);
            while (*p != ']') p++;
            p++; // skip ']'
        } else p++;
    }
    auto end_time = get_thread_time();
    printf("%s: load %zu elements\n", name_to_open.c_str(), ret.size());
    print_clock("parsing took", start_time, end_time);
    return ret;
}

Prices get_price_vector(vector<trade_data> const &data) {
    Prices p;
    if( data.empty() ) {
        throw std::runtime_error("Empty data vector");
    }
    p.px = 1.L;
    p.py = data[0].close;
    return p;
}

TradeDataArray* get_all(json const &jin, int last_elems, Prices& price_vector) {
    if( jin["datafile"].size() != 1 ) {
        std::cerr << "Minisim: only 2-coin pools are supported\n";
        exit(1);
    }
    vector<trade_data> all_trades;

    string name = jin["datafile"][0];
    all_trades = get_data(name);
    printf("using file '%s'\n", name.c_str());
    
    u64 min_time = 1ull << 63;
    u64 max_time = 0;
    for (auto const &t: all_trades) {
        min_time = min(min_time, t.t);
        max_time = max(max_time, t.t);
    }
    vector<trade_data> out;

    for (auto &trade: all_trades) {
        if (trade.t >= min_time && trade.t <= max_time) {
            trade_data trade_min;
            trade_data trade_max;
            
            // (1, 2) min
            // (0, 2) min
            // (0, 1) min
            // (0, 1) max
            // (0, 2) max
            // (1, 2) max
            trade_min.t = trade.t - 1 * 10 + 5;
            trade_max.t = trade.t + 1 * 10 - 5;
            trade_min.open = trade.open;
            trade_max.close = trade.close;
            // no halving here - volumes are later halved in decision-making
            trade_min.volume = trade.volume;
            trade_max.volume = trade.volume;
            
            if (mabs(trade.open - trade.low) + mabs(trade.close - trade.high) < mabs(trade.open - trade.high) + mabs(trade.close - trade.low)) {
                trade_min.high = trade.low;
                trade_min.low = trade.low;
                trade_min.close = trade.low;
                trade_max.open = trade.high;
                trade_max.high = trade.high;
                trade_max.low = trade.high;
            } else {
                trade_min.high = trade.high;
                trade_min.low = trade.high;
                trade_min.close = trade.high;
                trade_max.open = trade.low;
                trade_max.high = trade.low;
                trade_max.low = trade.low;
            }
            
            out.push_back(trade_min);
            out.push_back(trade_max);
        }
    }
    if (last_elems > 0) {
        printf("Trimming: use last %d elements\n", last_elems);
        out.erase(out.begin(), out.begin() + out.size() - last_elems);
    }
    price_vector = get_price_vector(out);
    return new TradeDataVector(std::move(out));
}

money geometric_mean_2(money const *x) {
    return sqrtl(x[0] * x[1]);
}

static auto reduction_coefficient_2(const TokensXP &x, money gamma) {
    money K = 1.L;
    money S = 0.L;
    for (size_t i = 0; i < 2; i++) S += x[i]; // = sum(x)
    for (size_t i = 0; i < 2; i++)  {
        K *= 2 * x[i] / S;
    }
    if (gamma > 0) {
        K = gamma * K / (gamma * K + 1.L - K);
    }
    return K;
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
    const Prices *price_vector = nullptr;
    const TradeDataArray *test_data = nullptr;
    extra_data result;
};


struct Trader {
    Trader(json const &jconf, const Prices &p0) :
        mid_fee(jconf["mid_fee"]),
        out_fee(jconf["out_fee"]),
        fee_gamma(jconf["fee_gamma"]),
        ext_fee(jconf["ext_fee"]),
        gas_fee(jconf["gas_fee"]),
        curve(jconf["A"], jconf["gamma"]),
        state(jconf["D"], p0)
    {
        money D = jconf["D"];
        adjustment_step = jconf["adjustment_step"];
        allowed_extra_profit = jconf["allowed_extra_profit"];
        ma_half_time = jconf["ma_half_time"];

        this->boost_rate = jconf["boost_rate"];
        this->boost_mul = jconf["boost_mul"];
        this->boost_min = jconf["boost_min"];
        if (jconf.contains("lp_profit_fraction"))
            this->lp_profit_fraction = jconf["lp_profit_fraction"];
        else
            this->lp_profit_fraction = 0.5L;
        this->boost_rate = this->boost_rate / (86400L * 365L);
        this->boost_min = this->boost_min / (86400L * 365L);
        this->boost_integral = 1.L;
        log = jconf["log"];
        this->price_oracle = p0;
        this->last_price   = p0;
        this->dx = D * 1e-8L;
        this->xcp_profit = 1.L;
        this->xcp_profit_real = 1.L;
        this->xcp = curve.get_xcp_2(state);
        this->not_adjusted = false;
        this->heavy_tx = 0;
        this->light_tx = 0;
        this->t = 0;
    }

    auto fee_2() {
        TokensXP xp;
        state.getXP(xp);
        auto f = reduction_coefficient_2(xp, fee_gamma);
        return (mid_fee * f + out_fee * (1.L - f));
    }

    money step_for_price_2(money p_min, money p_max, money vol, money ext_vol);

    void update_xcp_2(bool only_real=false) {
        auto _xcp = curve.get_xcp_2(state);
        auto old_xcp_profit_real = xcp_profit_real;
        xcp_profit_real = xcp_profit_real * _xcp / xcp;
        if (not only_real) {
            xcp_profit += xcp_profit_real - old_xcp_profit_real;
        }
        xcp = _xcp;
    }

    money exchange_2(money dx, int i, int j, money max_price=1e100L) {
        //"""
        //Buy y for x
        //"""
        Tokens x_old = state.xs;
        auto x = state.xs[i] + dx;
        auto y = curve.y_2(state, x, i, j);
        
        state.xs[i] = x;
        state.xs[j] = y;
        auto fee_mul = 1.L - this->fee_2();
        auto dy = x_old[j] - y;
        
        state.xs[j] = x_old[j] - dy * fee_mul;
        if ((dx / dy) > max_price or dy < 0) {
            state.xs = x_old;
            return 0;
        }
        update_xcp_2();
        return dy;
    }

    void ma_recorder(u64 t, vector<money> const &price_vector) {
        //  XXX what if every block only has p_b being last
        if (t > this->t) {
            money alpha = powl(0.5, ((money)(t - this->t) / this->ma_half_time));
            alpha = min(alpha, 1.L);
            const size_t k = 1;
            price_oracle.py = price_vector[k] * (1 - alpha) + price_oracle.py * alpha;
            this->t = t;
        }
    }

    void tweak_price_2(u64 t, money spot_prev);


    void simulate(simulation_data *simdata, extra_data *extdata);

    Prices price_oracle;
    Prices last_price;
    u64 t;
    money dx;
    money xcp;
    money xcp_profit;
    money xcp_profit_real;
    money adjustment_step;
    money allowed_extra_profit;
    int log;
    int ma_half_time;
    const money mid_fee;
    const money out_fee;
    const money fee_gamma;
    const money ext_fee;
    const money gas_fee;
    money boost_rate;
    money boost_mul;
    money boost_min;
    money boost_integral;
    money lp_profit_fraction;
    bool not_adjusted;
    int  heavy_tx;
    int  light_tx;
    const Curve curve;
    AMMState state;
};

money Trader::step_for_price_2(money p_min, money p_max, money vol, money ext_vol) {
    Tokens x0;
    x0 = state.xs;
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
        auto fee_mul = 1.L - this->fee_2();

        _dy = (x0[_to] - y) * fee_mul;
        state.xs[_to] = x0[_to] - _dy;

        // price in units d_first / d_second
        if (_from == 0) {
            price = _dx / _dy;
        }
        else {
            price = _dy / _dx;
        }
        auto v = vol + _dy * state.price[_to];

        state.xs = x0;  // restore the state
        // printf("::: %Lf %Lf %Lf %Lf\n", price, inst_price, p_min, p_max);

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
            auto fee_mul = 1.L - this->fee_2();

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
    // printf("*** p_min=%Lf, p_max=%Lf, _dy=%Lf, y=%Lf\n", p_min, p_max, _dy, state.xs[_to]);

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
    size_t N = 2;
    long double last_time_tweak_price = 0;
    const size_t total_elements = simdata->test_data->size();
    const trade_data* mapped_data = simdata->test_data->array();
    money xcp_profit_real_prev = 1.L;
    money xcp_profit_real_adj = 1.L;
    money slippage = 0;
    money imbalance = 0;
    money antislippage = 0;
    money slippage_count = 0;
    money volume = 0;
    money total_vol = 0;
    money last_prices = curve.price_2(state);
    money imbalance_integral = 0;
    money APY = 0.0;
    money APY_boost = 0.0;
    money APY_boost_2 = 0.0;
    money APR_geo_mean = 0.0;
    // Moving 1-month window geometric-mean APY tracking
    constexpr u64 TW_APR_SECONDS = 2 * 30 * 86400;  // time window for APR_geo_mean
    constexpr money TW_APR_PER_YEAR = (365.L * 86400.L) / TW_APR_SECONDS;
    std::deque<std::pair<u64, money>> xcp_history;
    money sum_log_tw_apr = 0;
    money tw_apr = 0;
    long long n_monthly_samples = 0;

    FILE *out_file = nullptr;
    if (log) {
        out_file = fopen("detailed-output.json", "w");
        fprintf(out_file, "[");
    }
    constexpr int a = 0;
    constexpr int b = 1;
    money last = price_oracle[b] / price_oracle[a];
    // Accumulator: sum of dt where relative deviation exceeds threshold

    assert(total_elements > 0 );
    const u64 start_t = mapped_data[0].t;

    for (size_t i = 0; i < total_elements; i++) {
        long double last_time = 0;
        trade_data d = mapped_data[i];

        if (i == 0) {
            last_time_tweak_price = d.t;
            this->t = d.t;
        }
        if( i > 0 ) {
            last_time = d.t - mapped_data[i-1].t;
        }

        money vol     = 0.0L;
        const money ext_vol = money(d.volume * price_oracle[b]); //  <- now all is in USD
        auto _high = last;
        auto _low  = last;

        money p_before = curve.price_2(state);
        money p_after  = p_before;
        auto apply_tweak_trade = [&]() {
            money ps_before = state.price[1];
            money cur_get_p = curve.p_2(state);
            tweak_price_2(d.t, last_prices);
            last_prices = cur_get_p * ps_before;
            last_time_tweak_price = d.t;
        };

        {
            const money max_price = d.high * (1 - ext_fee);
            if ((max_price != 0) & (max_price > p_before)) {
                auto step = step_for_price_2(0, max_price, vol, ext_vol);
                if (step > 0) {
                    const money dy = exchange_2(step, a, b);
                    vol += step * price_oracle[a];
                    const money _dx = dy;
                    last    = curve.price_2(state);
                    p_after = last;
                    volume += _dx / (state.xs[b] + state.xs[a] / p_after) * N / 2;
                    const money _slippage = (_dx * (p_before + p_after)) / (2.L * (mabs(p_before - p_after)) * state.xs[b]);
                    if (_slippage > 1e-10) {
                        slippage_count += last_time;
                        antislippage   += last_time * _slippage;
                        slippage       += last_time / _slippage;
                        imbalance      += mabs(logl((_high + _low) / (2.L * state.price[1]))) * curve.A * last_time;
                    }
                    _high = last;
                    apply_tweak_trade();
                }
            }
        }

        {
            const money min_price = d.low  * (1 + ext_fee);
            p_before = p_after;

            if ((min_price != 0) && (min_price < p_before)) {
                auto step = step_for_price_2(min_price, 0, vol, ext_vol);
                if (step > 0) {
                    const money dy = exchange_2(step, b, a);
                    vol += dy * price_oracle[a];
                    const money _dx = step;
                    last    = curve.price_2(state);
                    p_after = last;
                    volume += _dx / (state.xs[b] + state.xs[a] / p_after) * N / 2;
                    const money _slippage = (_dx * (p_before + p_after)) / (2.L * (mabs(p_before - p_after)) * state.xs[b]);
                    if (_slippage > 1e-10) {
                        slippage_count += last_time;
                        antislippage += last_time * _slippage;
                        slippage += last_time / _slippage;
                        imbalance += logl(mabs((_high + _low) / (2.L * state.price[1]))) * curve.A * last_time;
                    }
                    apply_tweak_trade();
                }
            }
        }


        auto local_boost_rate = this->boost_rate;
        if (mid_fee < out_fee)
            local_boost_rate *= 1 + (fee_2() - mid_fee) / (out_fee - mid_fee) * (this->boost_mul - 1);

        // Boost with special donations to the pool
        if (this->boost_rate > 0) {
            auto _boost = (1.L + last_time * local_boost_rate);
            state.xs[0] = state.xs[0] * _boost;
            state.xs[1] = state.xs[1] * _boost;
            xcp_profit_real *= _boost;
            xcp *= _boost;
            this->boost_integral *= _boost;
        }

        // only tweak_price every N seconds or on trade
        //
        // FIXME: Contrary to comment we only tweak price on trade
        if (d.t - last_time_tweak_price >= 3600) {
            money previous_price_scale = state.price[1];
            money cur_get_p = curve.p_2(state);
            tweak_price_2(d.t, last_prices);
            last_prices = cur_get_p * previous_price_scale;
            last_time_tweak_price = d.t;
        }

        TokensXP _xp;
        state.getXP(_xp);
        money bal_mul = (_xp[0] + _xp[1]);
        money ideal_vp = xcp_profit * lp_profit_fraction + (1.L - lp_profit_fraction);
        bal_mul = 4 * _xp[0] * _xp[1] / (bal_mul * bal_mul);
        // xcp_profit_real_adj *= (ideal_vp / xcp_profit_real_prev - 1.L) * bal_mul * bal_mul + 1.L;
        xcp_profit_real_adj *= (ideal_vp / xcp_profit_real_prev);
        xcp_profit_real_prev = ideal_vp;

        total_vol += vol;
        imbalance_integral += (1.L - bal_mul) * last_time;  // last_time is dt here
        long double ARU_x = ideal_vp;
        long double ARU_y = (86400.L * 365.L / (d.t - start_t + 1.L));
        APY = powl(ARU_x, ARU_y) - 1.L;
        APY_boost = powl(ideal_vp / this->boost_integral, ARU_y) - 1.L;
        APY_boost_2 = powl(xcp_profit_real_adj / this->boost_integral, ARU_y) - 1.L;
        // Moving 1-month window geometric-mean APR
        xcp_history.push_back({d.t, xcp_profit_real_adj / this->boost_integral});
        // Advance front to the closest entry at or before (d.t - TW_APR_SECONDS)
        while (xcp_history.size() > 1 &&
               xcp_history[1].first <= d.t - TW_APR_SECONDS) {
            xcp_history.pop_front();
        }
        if (d.t - xcp_history.front().first >= TW_APR_SECONDS) {
            money tw_growth = (xcp_profit_real_adj / this->boost_integral) / xcp_history.front().second;
            tw_apr = max((tw_growth - 1.L) * TW_APR_PER_YEAR, 1e-20L);
            sum_log_tw_apr += logl(tw_apr);
            n_monthly_samples++;
            APR_geo_mean = expl(sum_log_tw_apr / n_monthly_samples);
        }
        if (i % 1024 == 0 && log) {
            try {
                printf("t=%lu %.1Lf%%\ttrades: %d\tAMM: %.5Lf\tTarget: %.5Lf\tVol: %.4Lf\tPR:%.2Lf\txCP-growth: {%.10Lf}\tAPY:%.1Lf%%\ttw_apr:%.1Lf%%\tfee:%.3Lf%% .\n",
                       d.t,
                       100.L * i / total_elements,
                       0, // FIXME: kept for keeping golden tests
                       last,
                       state.price.py,
                       total_vol,
                       (xcp_profit_real - 1.) / (xcp_profit - 1.L),
                       xcp_profit_real,
                       APY * 100.L,
                       tw_apr * 100.L,
                       fee_2() * 100.L);
            } catch (std::exception const &e) {
                printf("caught '%s'\n", e.what());
            }
        }

        if (log) {
            fprintf(out_file, "{\"t\": %lu, \"token0\": %.6Le, \"token1\": %.6Le, \"price_oracle\": %.6Le, \"price_scale\": %.6Le, \"profit\": %.6Le, \"xcp\": %.6Le, \"open\": %.6Le, \"high\": %.6Le, \"low\": %.6Le, \"close\": %.6Le, \"boost_rate\": %.6Le}",
                    d.t,
                    state.xs.x,
                    state.xs.y,
                    price_oracle[b] / price_oracle[a],
                    state.price[1],
                    xcp_profit_real - 1.0,
                    xcp_profit,
                    d.open, d.high, d.low, d.close,
                    local_boost_rate);
            if (i < total_elements - 1) {
                fprintf(out_file, ",\n");
            }
        }

        if (slippage > 1e20 and slippage_count > 0) {
            printf("*** Slippage is too high %.5Lf\n", slippage);
        }
    }
    extdata->imbalance_integral = imbalance_integral / (this->t - start_t + 1.L);
    extdata->slippage = slippage / slippage_count / 2.L;
    extdata->imbalance = imbalance / slippage_count / 2.L;
    extdata->liq_density = 2.L * antislippage / slippage_count;
    extdata->APY = APY;
    extdata->volume = volume;
    extdata->APY_boost = APY_boost;
    extdata->APY_boost_2 = APY_boost_2;
    extdata->APR_geo_mean = APR_geo_mean;

    if (log) {
        fprintf(out_file, "]");
    }
}

void Trader::tweak_price_2(u64 t, money spot_prev) {
    const int N = 2;

    // --- Feed the EMA with the pool's own spot (pre-fee marginal price),
    //     coin0 per coin1, computed at the current state.
    const money amm_p01 = spot_prev;             // dx/dy (coin0 per coin1)
    // money amm_p01 = price_2(0, 1);
    // Optional: cap like the real pool (avoid extreme oracle jumps)
    const money capped_p01 = std::min(amm_p01, 2.L * state.price[1]);

    std::vector<money> spot = {1.L, capped_p01};
    ma_recorder(t, spot);


    // # price_oracle looks like [1, p1, p2, ...] normalized to 1e18
    money S = 0;
    for (size_t i = 0; i < N; i++) {
        auto t = price_oracle[i] / state.price[i] - 1.L;
        S += t*t;
    }
    const money norm = sqrt(S);
    const money _adjustment_step = min(adjustment_step, norm / 5);
    if (norm <= _adjustment_step) {
        // Already close to the target price
        light_tx += 1;
        return;
    }
    if (not not_adjusted and (xcp_profit_real > xcp_profit * lp_profit_fraction + (1.L - lp_profit_fraction) + allowed_extra_profit)) {
        not_adjusted = true;
    }
    if (not not_adjusted) {
        light_tx += 1;
        return;
    }
    heavy_tx += 1;

    Prices p_new;
    p_new.px = 1.L;
    {
        auto p_target = state.price.py;
        auto p_real = price_oracle[1];
        p_new.py = p_target + _adjustment_step * (p_real - p_target) / norm;
    }
    Prices old_p;
    old_p = state.price;

    auto old_profit = xcp_profit_real;
    auto old_xcp = xcp;

    state.price = p_new;
    update_xcp_2(true);

    if (xcp_profit_real <= xcp_profit * lp_profit_fraction + (1.L - lp_profit_fraction)) {
        //  If real profit is less than equilibrium - revert params back
        state.price = old_p;
        xcp_profit_real = old_profit;
        xcp = old_xcp;
        not_adjusted = false;
        // auto val = ((xcp_profit_real - 1.L - (xcp_profit - 1.L) / 2.L));
        // printf("%.10Lf\n", val);
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


bool simulation(simulation_data *data) {
    Trader trader(*(data->jconf), *(data->price_vector));
    auto start_simulation = get_thread_time();
    printf("Configuration %d: begin simulation\n", data->num);
    extra_data extdata;
    trader.simulate(data, &extdata);
    data->result = extdata;
    //money liq_density = jout["liq_density"];
    //money APY = jout["APY"];
    printf("Liquidity density vs that of xyz=k: %Lf\n", extdata.liq_density);
    printf("APY-boost: %Lf%%\n", extdata.APY_boost * 100.L);
    printf("APY-boost-2: %Lf%%\n", extdata.APY_boost_2 * 100.L);
    printf("APR-geo-mean: %Lf%%\n", extdata.APR_geo_mean * 100.L);
    printf("APY: %Lf%%\n", extdata.APY * 100.L);
    auto end = get_thread_time();
    print_clock("Total simulation time", start_simulation, end);
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
    simulation(&simdata);
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
    Prices price_vector;
    std::unique_ptr<TradeDataArray> test_data(
        get_all(jin, LAST_ELEMS, price_vector));
    double time_start = get_total_time();
    double wall_time_start = get_wall_time();

    WorkQueue work_queue(THREADS);
    json result = jin;
    for (int i = 0; i < configurations; i++) {
        simulation_data cd;
        cd.num = i;
        cd.test_data = &*test_data;
        cd.price_vector = &price_vector;
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
