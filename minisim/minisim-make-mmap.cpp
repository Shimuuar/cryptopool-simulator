#include "sim-data.hpp"
#include <memory>
#include <fstream>

static void usage(const char* progname, std::ostream& out) {
    out << "Usage: "<<progname<<" CANDLESTICKS MMAP\n"
        << "  CANDLESTICKS      Input file with Binance candleticks data\n"
        << "  MMAP              Output file which could be mmaped by minisim\n";
    out << "\n";
    out <<
        "This program produces memory dump from Binance candleticks data\n"
        "It's fastetest way to read data for simulator but data format\n"
        "is unstable.\n";
        ;
}

int main(int argc, char **argv) {
    if( argc != 3 ) {
        usage(argv[0], std::cerr);
        exit(1);
    }
    const char* input  = argv[1];
    const char* output = argv[2];
    //
    try {
        // Load data
        std::vector<OHLC> candles = get_data(input);
        std::unique_ptr<TradeDataArray> data(preprocessOHLC(candles));
        // Wrire data for
        std::ofstream f_out;
        f_out.exceptions(std::ofstream::failbit | std::ofstream::badbit);
        f_out.open(output);
        f_out.write(reinterpret_cast<const char*>(data->array()),
                    sizeof(price_point) * data->size());
    }
    catch ( const std::exception &e ) {
        std::cerr << "Error:   " << e.what()         << std::endl;
        std::cerr << "Of type: " << typeid(e).name() << std::endl;
        return 1;
    }
    return 0;
    
}
