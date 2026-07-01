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

using u64 = unsigned long long;
using money = long double;
static const int MAX_ARRAY = 3;



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
        printf("{ open: %.6Lf, high: %.6Lf low: %.6Lf close: %.6Lf t: %llu, volume: %.6Lf } ",
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

auto get_price_vector(vector<trade_data> const &data) {
    vector<money> p(2);
    if( data.empty() ) {
        throw std::runtime_error("Empty data vector");
    }
    p[0] = 1.L;
    p[1] = data[0].close;
    return p;
}

TradeDataArray* get_all(json const &jin, int last_elems, vector<money> & price_vector) {
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

static auto reduction_coefficient_2(money const *x, money gamma) {
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

auto newton_D_2(money A, money gamma, money const *xx, money D0) {
    // ***
    // This now uses stableswap invariant (because invariants are pluggable)
    // ***
    money S = 0;
    const size_t N = 2;
    money x[2];
    for (size_t i = 0; i < N; i++) {
        S += x[i] = xx[i];
    }
    money D = D0;
    A *= N;  // A is already A * N**(N-1)

    for (int i=0; i < 255; i++) {
        money D_P = D;
        for (auto const &_x: x) {
            D_P = D_P * D / (N * _x);
        }
        money Dprev = D;
        D = (A * S + D_P * N) * D / ((A - 1) * D + (N + 1) * D_P);
        if (mabs(D - Dprev) <= 1e-6) {
            return D;
        }
    }
    return D; // we ignore convergence error in simulation
    throw std::logic_error("Newton_D: did not converge");
}

auto newton_y(money A, money gamma, money const *x, money D, int i) {
    // ***
    // This now uses stableswap invariant (because invariants are pluggable)
    // ***
    constexpr int N = 2;
    A = A * N;
    int other = 1 - i;
    money c = D*D / (x[other] * N);
    c = c * D / (A * N);
    money b = x[other] + D / A - D;
    money y_prev = 0;
    money y = D;
    for (size_t k = 0; k < 255; k++) {
        y_prev = y;
        y = (y*y + c) / (2 * y + b);
        if (mabs(y - y_prev) <= 1e-12L) {
            return y;
        }
    }
    return y;  // XXX
    throw std::logic_error("Did not converge");
}

auto get_p_2(money const *x, money D, money A, money gamma) {
    money ANN = A * 2.;
    money Dr = D / 4.;
    for (size_t idx = 0; idx < 2; ++idx) {
        Dr = Dr * D / x[idx];
    }
    money xp0_A = ANN * x[0];
    return
        (xp0_A + Dr * x[0] / x[1]) / (xp0_A + Dr);
}

money solve_x(money A, money gamma, money const *x, money D, int i) {
    return newton_y(A, gamma, x, D, i);
}

auto solve_D(money A, money gamma, money const *x) {
    auto D0 = 2 * geometric_mean_2(x); //  # <- fuzz to make sure it's ok XXX
    return newton_D_2(A, gamma, x, D0);
}

struct Curve {
    Curve(json const &jconf, vector<money> const &p) {
        this->A = jconf["A"];
        this->gamma = jconf["gamma"];
        money D = jconf["D"];
        if (!p.empty()) {
            this->p = p;
        } else {
            this->p.resize(2, 1.L);
        }
        this->x.resize(2);
        for(size_t i = 0; i < 2; i++) {
            x[i] = D / 2 / p[i];
        }
    }

    auto xp_2(money *ret) const {
        for (int i = 0; i < 2; i++) {
            ret[i] = x[i] * p[i];
            assert(x[i] > 0);
        }
    }

    auto D_2() const {
        money xp[2];
        this->xp_2(xp);
        auto ret = solve_D(A, gamma, xp);
        return ret;
    }

    money y_2(money x, int i, int j) {
        money xp[2];
        this->xp_2(xp);
        xp[i] = x * this->p[i];
        auto yp = solve_x(A, gamma, xp, this->D_2(), j);
        auto ret = yp / this->p[j];
        return ret;
    }

    money p_2() {
        money xp[2];
        this->xp_2(xp);
        auto p = get_p_2(xp, this->D_2(), this->A, this->gamma);
        return p;
    }

    money A;
    money gamma;
    vector<money> p;
    vector<money> x;
};

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
    vector<money> const *price_vector = nullptr;
    const TradeDataArray *test_data = nullptr;
    extra_data result;
    size_t total = 0;
    size_t current = 0;
};


struct Trader {
    Trader(json const &jconf, vector<money> const &p0) :
        curve(jconf, p0)
    {
        money D = jconf["D"];
        mid_fee = jconf["mid_fee"];
        out_fee = jconf["out_fee"];
        fee_gamma = jconf["fee_gamma"];
        adjustment_step = jconf["adjustment_step"];
        allowed_extra_profit = jconf["allowed_extra_profit"];
        ma_half_time = jconf["ma_half_time"];
        this->ext_fee = jconf["ext_fee"];
        this->gas_fee = jconf["gas_fee"];
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
        this->p0 = p0;
        this->price_oracle = this->p0;
        this->last_price = this->p0;
        this->dx = D * 1e-8L;
        this->D0 = this->curve.D_2();
        this->xcp_0 = this->get_xcp_2();
        this->xcp_profit = 1.L;
        this->xcp_profit_real = 1.L;
        this->xcp = this->xcp_0;
        this->total_vol = 0.0;
        this->volume = 0;
        this->not_adjusted = false;
        this->heavy_tx = 0;
        this->light_tx = 0;
        this->is_light = false;
        this->t = 0;

        // Initialize variables
        APY = 0.0;
        APY_boost = 0.0;
        APY_boost_2 = 0.0;
        APR_geo_mean = 0.0;
    }

    auto fee_2() {
        money xp[2];
        curve.xp_2(xp);
        auto f = reduction_coefficient_2(xp, fee_gamma);
        return (mid_fee * f + out_fee * (1.L - f));
    }

    money get_xcp_2() const {
        // First calculate the ideal balance
        //  Then calculate, what the constant-product would be
        auto D = curve.D_2();
        money X[2];
        for (size_t i = 0; i < 2; i++) {
            X[i] = D  / (2 * curve.p[i]);
        }
        return geometric_mean_2(X);
    }

    auto price_2(int i, int j) {
        // auto dx_raw = dx  / curve.p[i];
        // auto curve_res = curve.y_2(curve.x[i] + dx_raw, i, j);
        // auto ret = dx_raw  / (curve.x[j] - curve_res);
        // return ret;
        return curve.p_2() * curve.p[j];
    }

    money step_for_price_2(money p_min, money p_max, money vol, money ext_vol);

    void update_xcp_2(bool only_real=false) {
        auto _xcp = get_xcp_2();
        auto old_xcp_profit_real = xcp_profit_real;
        xcp_profit_real = xcp_profit_real * _xcp / xcp;
        if (not only_real) {
            xcp_profit += xcp_profit_real - old_xcp_profit_real;
        }
        xcp = _xcp;
    }


    inline void static copy_money_2(money *to, money const *from) {
        to[0] = from[0];
        to[1] = from[1];
    }

    money exchange_2(money dx, int i, int j, money max_price=1e100L) {
        //"""
        //Buy y for x
        //"""
        try {
            money x_old[2];
            copy_money_2(x_old, &curve.x[0]);
            auto x = curve.x[i] + dx;
            auto y = curve.y_2(x, i, j);

            curve.x[i] = x;
            curve.x[j] = y;
            auto fee_mul = 1.L - this->fee_2();
            auto dy = x_old[j] - y;

            curve.x[j] = x_old[j] - dy * fee_mul;
            if ((dx / dy) > max_price or dy < 0) {
                copy_money_2(&curve.x[0], x_old);
                return 0;
            }
            update_xcp_2();
            return dy;
        } catch (...) {
            return 0;
        }
    }

    void ma_recorder(u64 t, vector<money> const &price_vector) {
        //  XXX what if every block only has p_b being last
        if (t > this->t) {
            money alpha = powl(0.5, ((money)(t - this->t) / this->ma_half_time));
            alpha = min(alpha, 1.L);
            for (size_t k = 1; k < price_vector.size(); k++) {
                price_oracle[k] = price_vector[k] * (1 - alpha) + price_oracle[k] * alpha;
            }
            this->t = t;
        }
    }

    money tweak_price_2(u64 t, int /*a*/, int /*b*/, money spot_prev);


    void simulate(simulation_data *simdata, extra_data *extdata);

    vector<money> p0;
    vector<money> price_oracle;
    vector<money> last_price;
    u64 t;
    money dx;
    money mid_fee;
    money out_fee;
    money D0;
    money xcp, xcp_0;
    money xcp_profit;
    money xcp_profit_real;
    money adjustment_step;
    money allowed_extra_profit;
    int log;
    money fee_gamma;
    money total_vol;
    int ma_half_time;
    money ext_fee;
    money gas_fee;
    money boost_rate;
    money boost_mul;
    money boost_min;
    money boost_integral;
    money lp_profit_fraction;
    money volume;
    money slippage;
    money imbalance;
    money antislippage;
    money slippage_count;
    long double APY;
    long double APY_boost;
    long double APY_boost_2;
    long double APR_geo_mean;
    bool not_adjusted;
    int  heavy_tx;
    int  light_tx;
    bool is_light;
    Curve curve;
};

money Trader::step_for_price_2(money p_min, money p_max, money vol, money ext_vol) {
    money x0[2];
    copy_money_2(x0, &curve.x[0]);
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
    auto step0 = dx / curve.p[_from];  // step in units of currency being sold
    auto step = step0;
    money gas = gas_fee / curve.p[_from];

    money previous_profit = 0;

    // + (step increases)
    while (true) {
        auto _dx_prev = _dx;
        auto _dy_prev = _dy;

        _dx += step;

        // buy  -> x: first, y: second
        // sell -> x: second, y: first

        x = x0[_from] + _dx;
        y = curve.y_2(x, _from, _to);

        curve.x[_from] = x;
        curve.x[_to] = y;
        auto fee_mul = 1.L - this->fee_2();

        _dy = (x0[_to] - y) * fee_mul;
        curve.x[_to] = x0[_to] - _dy;

        // price in units d_first / d_second
        if (_from == 0) {
            price = _dx / _dy;
        }
        else {
            price = _dy / _dx;
        }
        auto v = vol + _dy * curve.p[_to];

        copy_money_2(&curve.x[0], x0);  // restore the state
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
            y = curve.y_2(x, _from, _to);

            curve.x[_from] = x;
            curve.x[_to] = y;
            auto fee_mul = 1.L - this->fee_2();

            _dy = (x0[_to] - y) * fee_mul;
            curve.x[_to] = x0[_to] - _dy;

            if (_from == 0) {
                price = _dx / _dy;
            }
            else {
                price = _dy / _dx;
            }
            auto v = vol + _dy * curve.p[_to];

            copy_money_2(&curve.x[0], x0);  // restore the state


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
    // printf("*** p_min=%Lf, p_max=%Lf, _dy=%Lf, y=%Lf\n", p_min, p_max, _dy, curve.x[_to]);

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
    size_t N = price_oracle.size();
    u64 start_t = 0;
    long double last_time = 0;
    long double last_time_tweak_price = 0;
    size_t total_elements = simdata->test_data->size();
    simdata->total = total_elements;
    const trade_data* mapped_data = simdata->test_data->array();
    money xcp_profit_real_prev = 1.L;
    money xcp_profit_real_adj = 1.L;
    money slippage = 0;
    money imbalance = 0;
    money antislippage = 0;
    money slippage_count = 0;
    money last_prices = price_2(0, 1);
    money previous_price_scale = curve.p[1];
    money imbalance_integral = 0;
    // Moving 1-month window geometric-mean APY tracking
    const u64 TW_APR_SECONDS = 2 * 30 * 86400;  // time window for APR_geo_mean
    const money TW_APR_PER_YEAR = (365.L * 86400.L) / TW_APR_SECONDS;
    std::deque<std::pair<u64, money>> xcp_history;
    money sum_log_tw_apr = 0;
    money tw_apr = 0;
    long long n_monthly_samples = 0;
    // Track TVL growth in coin0 units and HODL baseline
    // TVL in coin0 units: sum_i x[i] * p[i] (p[0] == 1)
    vector<money> x_start = curve.x; // initial LP balances by coin

    FILE *out_file = nullptr;
    if (log) {
        out_file = fopen("detailed-output.json", "w");
        fprintf(out_file, "[");
    }
    constexpr int a = 0;
    constexpr int b = 1;
    money last = price_oracle[b] / price_oracle[a];
    // Accumulator: sum of dt where relative deviation exceeds threshold
    for (size_t i = 0; i < total_elements; i++) {
        trade_data d = mapped_data[i];
        money _slippage = 0;

        simdata->current = i;

        if (i == 0) {
            start_t = d.t;
            last_time_tweak_price = d.t;
            this->t = d.t;
        }
        if (last_time > 0) {
            last_time = d.t - last_time;
        }

        money vol     = 0.0L;
        money ext_vol = money(d.volume * price_oracle[b]); //  <- now all is in USD
        auto _high = last;
        auto _low  = last;

        const money max_price = d.high * (1 - ext_fee);
        const money min_price = d.low  * (1 + ext_fee);
        money p_before = price_2(a, b);
        money p_after  = 0;
        bool trade_happened = false;
        auto apply_tweak_trade = [&]() {
            money ps_before = curve.p[1];
            money cur_get_p = curve.p_2();
            tweak_price_2(d.t, a, b, last_prices);
            last_prices = cur_get_p * ps_before;
            last_time_tweak_price = d.t;
        };

        {
            int   ctr = 0;
            money _dx = 0;
            if ((max_price != 0) & (max_price > p_before)) {
                auto step = step_for_price_2(0, max_price, vol, ext_vol);
                if (step > 0) {
                    auto dy = exchange_2(step, a, b);
                    vol += step * price_oracle[a];
                    _dx += dy;
                    last = price_2(a, b);
                    ctr += 1;
                }
            }
            
            p_after = price_2(a, b);
            
            if (p_before != p_after) {
                auto v = _dx / (curve.x[b] + curve.x[a] / p_after) * N / 2;
                _slippage = (_dx * (p_before + p_after)) / (2.L * (mabs(p_before - p_after)) * curve.x[b]);
                volume += v;
            }
            if (_slippage > 1e-10) {
                slippage_count += last_time;
                antislippage += last_time * _slippage;
                slippage += last_time / _slippage;
                imbalance += mabs(logl((_high + _low) / (2.L * curve.p[1]))) * curve.A * last_time;
            }
            _high = last;
            
            if (ctr > 0) {
                if (_low == 0) _low = last;
                apply_tweak_trade();
            }
        }

        {
            int   ctr = 0;
            money _dx = 0;
            p_before = p_after;

            if ((min_price != 0) && (min_price < p_before)) {
                auto step = step_for_price_2(min_price, 0, vol, ext_vol);
                if (step > 0) {
                    auto dy = exchange_2(step, b, a);
                    vol += dy * price_oracle[a];
                    _dx += step;
                    last = price_2(a, b);
                    ctr += 1;
                }
            }

            p_after = price_2(a, b);

            if (p_before != p_after) {
                auto v = _dx / (curve.x[b] + curve.x[a] / p_after) * N / 2;
                _slippage = (_dx * (p_before + p_after)) / (2.L * (mabs(p_before - p_after)) * curve.x[b]);
                volume += v;
            }
            if (_slippage > 1e-10) {
                slippage_count += last_time;
                antislippage += last_time * _slippage;
                slippage += last_time / _slippage;
                imbalance += logl(mabs((_high + _low) / (2.L * curve.p[1]))) * curve.A * last_time;
            }
            
            _low = last;
            if (ctr > 0) {
                if (_high == 0) _high = last;
                apply_tweak_trade();
            }
        }


        auto local_boost_rate = this->boost_rate;
        if (mid_fee < out_fee)
            local_boost_rate *= 1 + (fee_2() - mid_fee) / (out_fee - mid_fee) * (this->boost_mul - 1);

        // Boost with special donations to the pool
        if (this->boost_rate > 0) {
            auto _boost = (1.L + last_time * local_boost_rate);
            curve.x[0] = curve.x[0] * _boost;
            curve.x[1] = curve.x[1] * _boost;
            xcp_profit_real *= _boost;
            xcp *= _boost;
            this->boost_integral *= _boost;
        }

        long double norm = 0;
        // only tweak_price every N seconds or on trade
        if (d.t - last_time_tweak_price >= 3600 || trade_happened) {
            previous_price_scale = curve.p[1];
            money cur_get_p = curve.p_2();
            norm = tweak_price_2(d.t, a, b, last_prices);
            // spot_prev = price_2(0, 1) * ps_pre / curve.p[1];
            last_prices = cur_get_p * previous_price_scale;
            last_time_tweak_price = d.t;
            // XXX: Suppress unused
            (void)norm;
        }

        money _xp[2];
        curve.xp_2(_xp);
        money bal_mul = (_xp[0] + _xp[1]);
        money ideal_vp = xcp_profit * lp_profit_fraction + (1.L - lp_profit_fraction);
        bal_mul = 4 * _xp[0] * _xp[1] / (bal_mul * bal_mul);
        // xcp_profit_real_adj *= (ideal_vp / xcp_profit_real_prev - 1.L) * bal_mul * bal_mul + 1.L;
        xcp_profit_real_adj *= (ideal_vp / xcp_profit_real_prev);
        xcp_profit_real_prev = ideal_vp;

        total_vol += vol;
        imbalance_integral += (1.L - bal_mul) * last_time;  // last_time is dt here
        last_time = d.t;
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
                printf("t=%llu %.1Lf%%\ttrades: %d\tAMM: %.5Lf\tTarget: %.5Lf\tVol: %.4Lf\tPR:%.2Lf\txCP-growth: {%.10Lf}\tAPY:%.1Lf%%\ttw_apr:%.1Lf%%\tfee:%.3Lf%% %c\n",
                       d.t,
                       100.L * i / total_elements,
                       0, // FIXME: kept for keeping golden tests
                       last,
                       curve.p[1],
                       total_vol,
                       (xcp_profit_real - 1.) / (xcp_profit - 1.L),
                       xcp_profit_real,
                       APY * 100.L,
                       tw_apr * 100.L,
                       fee_2() * 100.L,
                       is_light ? '*' : '.');
            } catch (std::exception const &e) {
                printf("caught '%s'\n", e.what());
            }
        }

        if (log) {
            fprintf(out_file, "{\"t\": %llu, \"token0\": %.6Le, \"token1\": %.6Le, \"price_oracle\": %.6Le, \"price_scale\": %.6Le, \"profit\": %.6Le, \"xcp\": %.6Le, \"open\": %.6Le, \"high\": %.6Le, \"low\": %.6Le, \"close\": %.6Le, \"boost_rate\": %.6Le}",
                    d.t,
                    curve.x[0],
                    curve.x[1],
                    price_oracle[b] / price_oracle[a],
                    curve.p[1],
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

money Trader::tweak_price_2(u64 t, int /*a*/, int /*b*/, money spot_prev) {
    const int N = 2;

    // --- Feed the EMA with the pool's own spot (pre-fee marginal price),
    //     coin0 per coin1, computed at the current state.
    money amm_p01 = spot_prev;             // dx/dy (coin0 per coin1)
    // money amm_p01 = price_2(0, 1);
    // Optional: cap like the real pool (avoid extreme oracle jumps)
    money capped_p01 = std::min(amm_p01, 2.L * curve.p[1]);

    std::vector<money> spot = {1.L, capped_p01};
    ma_recorder(t, spot);


    // # price_oracle looks like [1, p1, p2, ...] normalized to 1e18
    money S = 0;
    for (size_t i = 0; i < N; i++) {
        auto t = price_oracle[i] / curve.p[i] - 1.L;
        S += t*t;
    }
    auto norm = S;
    norm = sqrt(norm); // .root_to();
    auto _adjustment_step = min(adjustment_step, norm / 5);
    if (norm <= _adjustment_step) {
        // Already close to the target price
        is_light = true;
        light_tx += 1;
        return norm;
    }
    if (not not_adjusted and (xcp_profit_real > xcp_profit * lp_profit_fraction + (1.L - lp_profit_fraction) + allowed_extra_profit)) {
        not_adjusted = true;
    }
    if (not not_adjusted) {
        light_tx += 1;
        is_light = true;
        return norm;
    }
    heavy_tx += 1;
    is_light = false;

    money p_new[MAX_ARRAY];
    p_new[0] = 1.L;
    for (size_t i = 1; i < price_oracle.size(); i++) {
        auto p_target = curve.p[i];
        auto p_real = price_oracle[i];
        p_new[i] = p_target + _adjustment_step * (p_real - p_target) / norm;
    }
    money old_p[MAX_ARRAY];
    copy_money_2(old_p, &curve.p[0]);

    auto old_profit = xcp_profit_real;
    auto old_xcp = xcp;

    copy_money_2(&curve.p[0],p_new);
    update_xcp_2(true);

    if (xcp_profit_real <= xcp_profit * lp_profit_fraction + (1.L - lp_profit_fraction)) {
        //  If real profit is less than equilibrium - revert params back
        copy_money_2(&curve.p[0], old_p);
        xcp_profit_real = old_profit;
        xcp = old_xcp;
        not_adjusted = false;
        // auto val = ((xcp_profit_real - 1.L - (xcp_profit - 1.L) / 2.L));
        // printf("%.10Lf\n", val);
    }
    return norm;
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
    vector<money> price_vector;
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
        cd.current = 0;
        cd.total = 0;
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
