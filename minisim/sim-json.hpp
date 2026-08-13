#pragma once
// nlohmann::json is pretty common JSON implementation for C++.  It
// however comes with very serious flaw: it's single 22k header of
// complicated C++ and it takes a long to compile (several seconds).
// This price is paid for every cpp that includes it.
//
// In order to avoid this we provide small facade over json which
// hides giant header.

#include <memory>
#include <string>
#include <functional>



/// Facade for nlohmann::json. This class exists in order to reduce
/// compile time of *.cpp files which need to interact with JSON.
class JSON {
public:
    // Reference to JSON value.
    class ref {
    public:
        void operator=(int);
        void operator=(double);

        ref       operator[](const char*);
        const ref operator[](const char*) const;
        ref       operator[](const std::string&);
        const ref operator[](const std::string&) const;
        ref       operator[](int);
        const ref operator[](int) const;

        operator int()         const;
        operator double()      const;
        operator long double() const;
    private:
        ref(void* ptr) : m_ptr(ptr) {}
        void* m_ptr;
        friend class JSON;
    };


    void load_file(const std::string& path);
    void save_file(const std::string& path);

    // Default, same as nlohmann::json()
    JSON();
    // Move data from other
    JSON(JSON&& other);
    ~JSON();

    ref       operator[](const char*);
    const ref operator[](const char*) const;
    ref       operator[](const std::string&);
    const ref operator[](const std::string&) const;
    ref       operator[](int);
    const ref operator[](int) const;

    operator int()         const;
    operator double()      const;
    operator long double() const;

    ref       as_ref();
    const ref as_ref() const;
private:
    struct Impl;
    std::unique_ptr<Impl> m_json;
};
