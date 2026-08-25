#pragma once

#include "sim-json.hpp"
#include <map>
#include <string>

// Generic factory whcih alloscreation of object of type T from JSON
// files
template<typename T>
class Factory {
private:
    using constructor = T* (*)(const JSON::ref&);
    using mapping     = std::map<std::string, constructor>;
public:
    // Create new curve from JSON value. Factory is dispatched on
    // json["type"]
    static T* make(const JSON& json) {
        return Factory<T>::make(json["type"], json.as_ref());
    }
    // Create new curve from JSON value. Factory is dispatched on
    // json["type"]
    static T* make(const JSON::ref& json) {
        return Factory<T>::make(json["type"], json);
    }
    // Create new curve from JSON value. Factory is dispatched on name
    static T* make(const std::string& name, const JSON& json) {
        return Factory<T>::make(name, json.as_ref());

    }
    // Create new curve from JSON value. Factory is dispatched on name
    static T* make(const std::string& name, const JSON::ref& json) {
        auto it = Factory<T>::m_map.find(name);
        if( it != Factory<T>::m_map.end() ) {
            return it->second(json);
        }
        return nullptr;
    }

    // Register constructor by name
    static void registerInFactory(const std::string& name, constructor fun) {
        Factory<T>::m_map[name] = fun;
    }

    // Helper struct for registration in a factory
    template<typename Q>
    struct Register {
        Register(const std::string& name) {
            Factory<T>::registerInFactory(
                name,
                [](const JSON::ref& json) -> T* {
                    return new Q(json);
                });
        }
    };
private:
    static mapping m_map;
};

template<typename T>
typename Factory<T>::mapping Factory<T>::m_map;
