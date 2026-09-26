#include "sim-data.hpp"
#include "sim-threading.hpp"

#include <sys/fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdexcept>
#include <charconv>
#include <simdjson.h>

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
        size_t size() const { return m_size; }
        // Underlying buffer
        const char* buffer() const { return m_ptr; }
    private:
        int    m_fd;
        char  *m_ptr;
        size_t m_size;
    };

    MMappedFile::MMappedFile(const char* name) {
        m_fd = open(name, O_RDONLY);
        if( m_fd < 0 ) {
            throw std::runtime_error("Cannot open file for reading");
        }
        lseek(m_fd, 0, SEEK_END);
        m_size = lseek(m_fd, 0, SEEK_CUR);
        m_ptr  = (char*)::mmap(
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


// Parse a price/volume field, which may be a decimal string
// (e.g. "3984.00000000") or a raw JSON number.
static money parse_money(simdjson::dom::element field, const char* what) {
    simdjson::error_code err;
    switch( field.type() ) {
    case simdjson::dom::element_type::STRING: {
        std::string_view s;
        err = field.get_string().get(s);
        if( err ) {
            throw std::runtime_error(std::string("Invalid string field ") + what);
        }
        double d = 0;
        auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), d);
        if( ec != std::errc() || ptr != s.data() + s.size() ) {
            throw std::runtime_error(
                "Invalid number '"+std::string(s)+"' for field "+what);
        }
        return d;
    }
    case simdjson::dom::element_type::DOUBLE: // Fallthough
    case simdjson::dom::element_type::UINT64: // Fallthough
    case simdjson::dom::element_type::INT64: {
        double v = 0;
        err = field.get_double().get(v);
        if( err ) {
            throw std::runtime_error(std::string("Invalid number field ") + what);
        }
        return v;
    }
    default:
        throw std::runtime_error(std::string("Field ") + what + " is not a number");
    }
}

// Parse timestamp in JSON
static uint64_t parse_time(simdjson::dom::element field) {
    uint64_t t = 0;
    simdjson::error_code err = field.get_uint64().get(t);
    if( err ) {
        throw std::runtime_error(
            std::string(std::string("Invalid time field: ") + simdjson::error_message(err)));
    }
    if( t > 10000000000ull ) {
        t /= 1000; // ms -> s
    }
    return t;
}

std::vector<OHLC> read_binance_data(std::string const &fname) {
    auto start_time = get_thread_time();
    printf("Parsing Binance data: %s\n", fname.c_str());
    MMappedFile mf( fname );

    simdjson::dom::parser parser;
    simdjson::dom::element root;
    simdjson::error_code err =
        parser.parse(mf.buffer(), mf.size()).get(root);
    if( err ) {
        throw std::runtime_error("Failed to parse JSON in '"+fname+"': " +simdjson::error_message(err));
    }
    // Top level JSON is an array of candlestick records.
    simdjson::dom::array records;
    err = root.get_array().get(records);
    if( err ) {
        throw std::runtime_error(
            "JSON in '" + fname + "' is not an array of records: " +
            simdjson::error_message(err));
    }

    std::vector<OHLC> ret;
    ret.reserve(records.size()); // exact count while it is below 2^24

    // Prices/volumes are stored as decimal strings (e.g. "3984.00000000").
    // parse_money() parses them at double precision (like the historical
    // atof()-based parser did) and widens to money; it also handles raw
    // JSON numbers.

    // Each record: [time, open, high, low, close, volume, ...]
    for( simdjson::dom::element record : records ) {
        simdjson::dom::array fields;
        err = record.get_array().get(fields);
        if( err ) {
            throw std::runtime_error(
                std::string("Record in '") + fname + "' is not an array: " +
                simdjson::error_message(err));
        }

        OHLC d;
        int idx = 0;
        for( simdjson::dom::element field : fields ) {
            switch( idx ) {
            case 0: d.t      = parse_time(field);            break;
            case 1: d.open   = parse_money(field, "open");   break;
            case 2: d.high   = parse_money(field, "high");   break;
            case 3: d.low    = parse_money(field, "low");    break;
            case 4: d.close  = parse_money(field, "close");  break;
            case 5: d.volume = parse_money(field, "volume"); break;
            default: break; // rest of the record is irrelevant
            }
            ++idx;
        }
        if( idx < 6 ) {
            throw std::runtime_error(
                std::string("Short record in '") + fname + "': expected >= 6 fields, got " +
                std::to_string(idx));
        }
        if( d.high < d.low ) {
            std::swap(d.high, d.low);
        }
        ret.push_back(d);
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

std::unique_ptr<TradeDataArray> make_binance_data(const std::string& fname, int last_elem) {
    std::vector<OHLC> all_trades = read_binance_data(fname);
    return std::unique_ptr<TradeDataArray>( preprocessOHLC(all_trades, last_elem) );
}

std::unique_ptr<TradeDataArray> make_time_series(const std::string& fname) {
    std::vector<price_point> dat;
    //
    auto start_time = get_thread_time();
    printf("Parsing time series data: %s\n", fname.c_str());
    MMappedFile mf( fname );

    simdjson::dom::parser parser;
    simdjson::dom::element root;
    simdjson::error_code err =
        parser.parse(mf.buffer(), mf.size()).get(root);
    if( err ) {
        throw std::runtime_error("Failed to parse JSON in '"+fname+"': " +simdjson::error_message(err));
    }
    //
    simdjson::dom::array records;
    err = root.get_array().get(records);
    if( err ) {
        throw std::runtime_error(
            "JSON in '" + fname + "' is not an array of records: " +
            simdjson::error_message(err));
    }
    dat.reserve(records.size()); // exact count while it is below 2^24
    //
    for( simdjson::dom::element record : records ) {
        simdjson::dom::array fields;
        err = record.get_array().get(fields);
        if( err ) {
            throw std::runtime_error(
                std::string("Record in '") + fname + "' is not an array: " +
                simdjson::error_message(err));
        }

        price_point d;
        int idx = 0;
        for( simdjson::dom::element field : fields ) {
            switch( idx ) {
            case 0: d.t      = parse_time(field);            break;
            case 1: d.price  = parse_money(field, "price");  break;
            case 2: d.volume = parse_money(field, "volume"); break;
            default: break; // rest of the record is irrelevant
            }
            ++idx;
        }
        if( idx < 2 ) {
            throw std::runtime_error(
                std::string("Short record in '") + fname + "': expected 3 fields, got " +
                std::to_string(idx));
        }
        dat.push_back(d);
    }
    //
    auto end_time = get_thread_time();
    printf("%s: load %zu elements\n", fname.c_str(), dat.size());
    print_clock("parsing took", start_time, end_time);
    return std::unique_ptr<TradeDataArray>(new TradeDataVector(std::move(dat)));
}

// ----------------------------------------------------------------
// MMAP'ed data

namespace {
    class TradeDataMmap: public TradeDataArray {
    public:
        TradeDataMmap(const std::string& file):
            m_mmap(file)
        {
            if( m_mmap.size() % sizeof(price_point) != 0 ) {
                throw std::runtime_error("mmap'ed file has incorrect length");
            }
        }
        virtual ~TradeDataMmap() = default;

        virtual size_t size()  const {
            return m_mmap.size() / sizeof(price_point);
        }
        virtual const price_point* array() const {
            return reinterpret_cast<const price_point*>(m_mmap.buffer());
        }
    private:
        MMappedFile m_mmap;
    };
}


std::unique_ptr<TradeDataArray> make_mmaped_data(const std::string& fname) {
    return std::make_unique<TradeDataMmap>(fname);
}
