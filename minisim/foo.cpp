#include "sim-json.hpp"
#include <iostream>

int main() {
    JSON js;
    js.load_file("test/conf.json");
    std::cout << (double)js["debug"] << std::endl;
    std::cout << (double)js["configuration"][0]["D"] << std::endl;

    js["foo"] = 12.3;
    js["configuration"][0]["XXX"]=123;
    js.save_file("/dev/stdout");
    return 0;
}
