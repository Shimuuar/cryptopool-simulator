#pragma once
// Very weird class. It wraps nlohmann::json since including 22k lines
// everywhere is bad idea and really hurts compilation time.

#include <memory>
#include <string>
#include <functional>




class JSON {
public:
    class ref {
    public:
        void operator=(int);
        void operator=(double);
        
        ref       operator[](const char*);
        const ref operator[](const char*) const;
        ref       operator[](int);
        const ref operator[](int) const;
        
        operator int()    const;
        operator double() const;
    private:
        ref(void* ptr) : m_ptr(ptr) {}
        void* m_ptr;
        friend class JSON;
    };

    
    void load_file(const std::string& path);
    void save_file(const std::string& path);

    JSON();
    JSON(JSON&& other);
    ~JSON();

    
    ref       operator[](const char*);
    const ref operator[](const char*) const;
    ref       operator[](int);
    const ref operator[](int) const;

    ref       as_ref();
    const ref as_ref() const;
private:
    struct Impl;
    std::unique_ptr<Impl> m_json;
};
