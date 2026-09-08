#include "sim-data.hpp"
#include "sim-threading.hpp"

#include <sys/fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdexcept>

#ifndef MAP_NOCACHE
#define MAP_NOCACHE 0
#endif


// ----------------------------------------------------------------
// Data loading

namespace {
    // MMapped file opened in read-only mode.
    class MMappedFile {
    public:
        explicit MMappedFile(const char* fname);
        explicit MMappedFile(const std::string& fname) :
            MMappedFile(fname.c_str())
        {}
        ~MMappedFile();

        // Size of file
        size_t      size()   { return m_size; }
        // Underlying buffer
        const unsigned char* buffer() { return m_ptr; }
    private:
        int            m_fd;
        unsigned char *m_ptr;
        size_t         m_size;
    };

    MMappedFile::MMappedFile(const char* name) {
        m_fd = open(name, O_RDONLY);
        if( m_fd < 0 ) {
            throw std::runtime_error("Cannot open file for reading");
        }
        lseek(m_fd, 0, SEEK_END);
        m_size = lseek(m_fd, 0, SEEK_CUR);
        m_ptr  = (unsigned char*)::mmap(
            nullptr, m_size, PROT_READ, MAP_NOCACHE|MAP_FILE|MAP_SHARED, m_fd, 0);
        if( m_ptr == MAP_FAILED) {
            throw std::runtime_error("mmap failed");
        }
    }

    MMappedFile::~MMappedFile() {
        ::munmap(m_ptr, m_size);
        close(m_fd);
    }
}


std::vector<OHLC> read_binance_data(std::string const &fname) {
    auto start_time = get_thread_time();
    printf("parsing %s\n", fname.c_str());
    MMappedFile mf( fname );
    std::vector<OHLC> ret;
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
            OHLC d;
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
    printf("%s: load %zu elements\n", fname.c_str(), ret.size());
    print_clock("parsing took", start_time, end_time);
    return ret;
}


// ----------------------------------------------------------------
// Data preprocessing

Prices TradeDataArray::initialPriceScale() const {
    if( size() == 0 ) {
        throw std::runtime_error("Empty data vector");
    }
    Prices p;
    p.p[0] = 1.L;
    p.p[1] = array()[0].price;
    return p;
}


namespace {
    class TradeDataVector: public TradeDataArray {
    public:
        TradeDataVector(const std::vector<price_point>& vec) :
            m_vec(vec)
        {}
        TradeDataVector(std::vector<price_point>&& vec) :
            m_vec(vec)
        {}
        virtual ~TradeDataVector() = default;

        virtual size_t             size()  const { return m_vec.size(); }
        virtual const price_point* array() const { return &m_vec[0]; }
    private:
        std::vector<price_point> m_vec;
    };
}


TradeDataArray* preprocessOHLC(const std::vector<OHLC>& all_trades, int last_elems) {
    u64 min_time = 1ull << 63;
    u64 max_time = 0;
    for (auto const &t: all_trades) {
        min_time = std::min(min_time, t.t);
        max_time = std::max(max_time, t.t);
    }
    std::vector<price_point> out;

    for (auto &trade: all_trades) {
        if (trade.t >= min_time && trade.t <= max_time) {
            price_point trade_min;
            price_point trade_max;

            // (1, 2) min
            // (0, 2) min
            // (0, 1) min
            // (0, 1) max
            // (0, 2) max
            // (1, 2) max
            trade_min.t = trade.t - 1 * 10 + 5;
            trade_max.t = trade.t + 1 * 10 - 5;
            // no halving here - volumes are later halved in decision-making
            trade_min.volume = trade.volume;
            trade_max.volume = trade.volume;

            if (std::abs(trade.open - trade.low) + std::abs(trade.close - trade.high) < std::abs(trade.open - trade.high) + std::abs(trade.close - trade.low)) {
                trade_min.price = trade.low;
                trade_max.price = trade.high;
            } else {
                trade_min.price = trade.high;
                trade_max.price = trade.low;
            }

            out.push_back(trade_min);
            out.push_back(trade_max);
        }
    }
    if (last_elems > 0) {
        printf("Trimming: use last %d elements\n", last_elems);
        out.erase(out.begin(), out.begin() + out.size() - last_elems);
    }
    return new TradeDataVector(std::move(out));
}

