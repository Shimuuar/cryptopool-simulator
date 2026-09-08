#pragma once
// APIs for loading of trading data used by simulation.
#include <string>
#include <vector>

#include "simulation.hpp"


// OHLC candlesticks >
struct OHLC {
    u64   t      = 0; // 0
    money open   = 0; // 1
    money high   = 0; // 2
    money low    = 0; // 3
    money close  = 0; // 4
    money volume = 0; // 5
};

// For simulation we need only triple (time,price,volume)
struct price_point {
    u64   t;
    money price;
    money volume;
};


// Interface for accessing trading data
class TradeDataArray {
public:
    // Number of data points
    virtual size_t size() const = 0;
    // Pointer to buffer with data
    virtual const price_point* array() const = 0;
    // Compute initial price scale
    Prices initialPriceScale() const;
    
    virtual ~TradeDataArray() = default;
};


// Load data from JSONs produced by Binance. It uses handrolled parser
// for speed.
std::vector<OHLC> read_binance_data(std::string const &fname);

// Convert OHCL data into time series for simulation
//
// If last_elems is not zero only N last elements are returned
TradeDataArray* preprocessOHLC(const std::vector<OHLC>& data, int last_elems = 0);


// Load data from mmap'd file with already prepared price_point data.
// This is fastest way of reading candlesticks data but most dangerous
// as well.  Memory dump must be prepared by same version of
// minisim-make-mmap utility.
TradeDataArray* read_mmaped_data(const std::string& fname);
