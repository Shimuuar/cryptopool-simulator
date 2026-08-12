#include "sim-json.hpp"
#include "json.hpp"

#include <variant>
#include <functional>
#include <fstream>
#include <iomanip>

using nlohmann::json;

struct JSON::Impl {
    // std::variant<
    //   std::reference_wrapper<json>,
    //   std::unique_ptr<json>
    //   > payload;

    Impl() :
        m_payload()
    {}

    nlohmann::json m_payload;
};


JSON::~JSON() {}


JSON::JSON() :
    m_json(new Impl())
{}

JSON::JSON(JSON&& other) :
    m_json(std::move(other.m_json))
{}

void JSON::load_file(const std::string& path) {
    std::ifstream ifl(path);
    if( !ifl ){
        throw std::logic_error("can't open file " + path);
    }
    ifl >> m_json->m_payload;
}

void JSON::save_file(const std::string &name) {
    std::ofstream ofl(name);
    if (!ofl) {
        throw std::logic_error("can't create file " + name);
    }
    ofl << std::setw(4) << m_json->m_payload << "\n";
}


JSON::ref JSON::as_ref() {
    json* ref = &m_json->m_payload;
    return JSON::ref(static_cast<void*>(ref));
}

const JSON::ref JSON::as_ref() const {
    const json* ref = &m_json->m_payload;
    return JSON::ref((void*)(ref));
}


JSON::ref       JSON::operator[](const char* k)       { return as_ref()[k]; }
const JSON::ref JSON::operator[](const char* k) const { return as_ref()[k]; }
JSON::ref       JSON::operator[](int k)               { return as_ref()[k]; }
const JSON::ref JSON::operator[](int k)         const { return as_ref()[k]; }


// ================================================================
// Ref implementation

void JSON::ref::operator=(int i) {
    json *js = static_cast<json*>(m_ptr);
    *js = i;
}

void JSON::ref::operator=(double x) {
    json *js = static_cast<json*>(m_ptr);
    *js = x;
}

JSON::ref JSON::ref::operator[](const char* key) {
    json *js    = static_cast<json*>(m_ptr);
    json &child = (*js)[key];
    return JSON::ref(static_cast<void*>(&child));
}
const JSON::ref JSON::ref::operator[](const char* key) const {
    const json *js    = static_cast<const json*>(m_ptr);
    const json &child = (*js)[key];
    return JSON::ref((void*)(&child));
}


JSON::ref JSON::ref::operator[](int key) {
    json *js    = static_cast<json*>(m_ptr);
    json &child = (*js)[key];
    return JSON::ref(static_cast<void*>(&child));
}
const JSON::ref JSON::ref::operator[](int key) const {
    const json *js    = static_cast<json*>(m_ptr);
    const json &child = (*js)[key];
    return JSON::ref((void*)(&child));
}


JSON::ref::operator int() const {
    const json *js = static_cast<json*>(m_ptr);
    return *js;
}
JSON::ref::operator double() const {
    const json *js = static_cast<json*>(m_ptr);
    return *js;
}
