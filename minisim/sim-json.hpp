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
        void operator=(long double);

        int size() const;
        bool contains(const char*) const;
        bool contains(const std::string&) const;

        void save_file(const std::string& path) const;

        ref       operator[](const char*);
        const ref operator[](const char*) const;
        ref       operator[](const std::string&);
        const ref operator[](const std::string&) const;
        ref       operator[](int);
        const ref operator[](int) const;

        operator int()         const;
        operator double()      const;
        operator long double() const;
        operator std::string() const;
    private:
        ref(void* ptr) : m_ptr(ptr) {}
        void* m_ptr;
        friend class JSON;
    };


    void load_file(const std::string& path);
    void save_file(const std::string& path) const;

    // Default, same as nlohmann::json()
    JSON();
    // Copy data
    JSON(const JSON&      other);
    JSON(const JSON::ref& other);
    // Move data from other
    JSON(JSON&& other);
    ~JSON();

    int size() const;
    bool contains(const char*) const;
    bool contains(const std::string&) const;

    void operator=(const JSON&);
    void operator=(const JSON::ref&);
    void operator=(int);
    void operator=(double);
    void operator=(long double);

    ref       operator[](const char*);
    const ref operator[](const char*) const;
    ref       operator[](const std::string&);
    const ref operator[](const std::string&) const;
    ref       operator[](int);
    const ref operator[](int) const;

    operator int()         const;
    operator double()      const;
    operator long double() const;
    operator std::string() const;

    ref       as_ref();
    const ref as_ref() const;
private:
    struct Impl;
    std::unique_ptr<Impl> m_json;
};
