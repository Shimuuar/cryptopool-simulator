#include "sim-json.hpp"
#include <iostream>

int main() {
    JSON js;
    js.load_file("test/conf.json");
    std::cout << (double)js["debug"] << std::endl;
    std::cout << (double)js["configuration"][0]["D"] << std::endl;

    JSON jjs = js;
    // jjs["foo"] = 12.3;
    // jjs["configuration"][0]["XXX"]=123;
    js.save_file("/dev/stdout");
    jjs.save_file("/dev/stdout");
    return 0;
}
