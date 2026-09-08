#include "sim-json.hpp"
#include "json.hpp"

#include <fstream>
#include <iomanip>

using nlohmann::json;

struct JSON::Impl {
    Impl() :
        m_payload(nullptr)
    {}

    nlohmann::json m_payload;
};


JSON::~JSON() {}


JSON::JSON() :
    m_json(new Impl())
{}

JSON::JSON(const JSON& other) :
    JSON(other.as_ref())
{}

JSON::JSON(const JSON::ref& other) :
    m_json(new Impl())
{
    const json* js = static_cast<const json*>(other.m_ptr);
    m_json->m_payload = *js;
}

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

void JSON::save_file(const std::string &name) const {
    as_ref().save_file(name);
}

JSON::ref JSON::as_ref() {
    json* ref = &m_json->m_payload;
    return JSON::ref(static_cast<void*>(ref));
}

const JSON::ref JSON::as_ref() const {
    const json* ref = &m_json->m_payload;
    return JSON::ref((void*)(ref));
}

void JSON::operator=(int         i) { as_ref() = i; }
void JSON::operator=(double      x) { as_ref() = x; }
void JSON::operator=(long double x) { as_ref() = x; }
void JSON::operator=(const JSON& js) { *this = js.as_ref(); }
void JSON::operator=(const JSON::ref& js) {
    const json* js_ptr = static_cast<const json*>(js.m_ptr);
    m_json->m_payload = *js_ptr;
}

JSON::ref       JSON::operator[](const char* k)              { return as_ref()[k]; }
const JSON::ref JSON::operator[](const char* k)        const { return as_ref()[k]; }
JSON::ref       JSON::operator[](const std::string& k)       { return as_ref()[k]; }
const JSON::ref JSON::operator[](const std::string& k) const { return as_ref()[k]; }
JSON::ref       JSON::operator[](int k)                      { return as_ref()[k]; }
const JSON::ref JSON::operator[](int k)                const { return as_ref()[k]; }

JSON::operator int()         const { return as_ref(); }
JSON::operator double()      const { return as_ref(); }
JSON::operator long double() const { return as_ref(); }
JSON::operator std::string() const { return as_ref(); }

int JSON::size() const { return as_ref().size(); }
bool JSON::contains(const char*        k) const { return as_ref().contains(k); }
bool JSON::contains(const std::string& k) const { return as_ref().contains(k); }

bool JSON::is_string() const { return as_ref().is_string(); }
bool JSON::is_array()  const { return as_ref().is_array();  }
bool JSON::is_object() const { return as_ref().is_object(); }

// ================================================================
// Ref implementation

void JSON::ref::operator=(int         i) { *static_cast<json*>(m_ptr) = i; }
void JSON::ref::operator=(double      x) { *static_cast<json*>(m_ptr) = x; }
void JSON::ref::operator=(long double x) { *static_cast<json*>(m_ptr) = x; }

void JSON::ref::save_file(const std::string &name) const {
    std::ofstream ofl(name);
    if (!ofl) {
        throw std::logic_error("can't create file " + name);
    }
    const json *js = static_cast<const json*>(m_ptr);
    ofl << std::setw(4) << *js << "\n";
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

JSON::ref JSON::ref::operator[](const std::string& key) {
    json *js    = static_cast<json*>(m_ptr);
    json &child = (*js)[key];
    return JSON::ref(static_cast<void*>(&child));
}

const JSON::ref JSON::ref::operator[](const std::string& key) const {
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

JSON::ref::operator int()         const { return *static_cast<const json*>(m_ptr); }
JSON::ref::operator double()      const { return *static_cast<const json*>(m_ptr); }
JSON::ref::operator long double() const { return *static_cast<const json*>(m_ptr); }
JSON::ref::operator std::string() const { return *static_cast<const json*>(m_ptr); }

int JSON::ref::size() const {
    return static_cast<json*>(m_ptr)->size();
}
bool JSON::ref::contains(const char* k) const {
    return static_cast<json*>(m_ptr)->contains(k);
}
bool JSON::ref::contains(const std::string& k) const {
    return static_cast<json*>(m_ptr)->contains(k);
}

bool JSON::ref::is_string() const {
    return static_cast<json*>(m_ptr)->is_string();
}

bool JSON::ref::is_array() const {
    return static_cast<json*>(m_ptr)->is_array();
}

bool JSON::ref::is_object() const {
    return static_cast<json*>(m_ptr)->is_object();
}
