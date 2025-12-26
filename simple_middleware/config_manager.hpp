#pragma once

#include "json11.hpp"
#include <string>
#include <mutex>
#include <map>
#include <iostream>

namespace simple_middleware {

class ConfigManager {
public:
    static ConfigManager& GetInstance();
    
    // 加载指定模块的配置文件 (例如 "control" -> 加载 config/control.json)
    // base_path 可以指定配置文件所在的目录，默认为 "../config" (相对于 build/bin)
    bool Load(const std::string& module_name, const std::string& config_file_path);
    
    // 获取整个配置对象
    json11::Json GetConfig(const std::string& module_name);
    
    // 泛型获取值 (基本类型特化)
    template<typename T>
    T Get(const std::string& module_name, const std::string& key, const T& default_val) {
        auto json = GetConfig(module_name);
        if (json.is_null()) return default_val;
        
        // 简单递归查找不是这里的重点，我们假设 key 是一级 key
        // 实际上 json11 不支持 "a.b" 这样的路径查找，这里先做简单封装
        if (json[key].is_null()) return default_val;

        return GetValue<T>(json[key], default_val);
    }

private:
    ConfigManager() = default;
    
    template<typename T>
    T GetValue(const json11::Json& json, const T& default_val);

    std::map<std::string, json11::Json> configs_;
    std::mutex mutex_;
};

// 特化实现
template<>
inline double ConfigManager::GetValue(const json11::Json& json, const double& default_val) {
    if (json.is_number()) return json.number_value();
    return default_val;
}

template<>
inline int ConfigManager::GetValue(const json11::Json& json, const int& default_val) {
    if (json.is_number()) return json.int_value();
    return default_val;
}

template<>
inline bool ConfigManager::GetValue(const json11::Json& json, const bool& default_val) {
    if (json.is_bool()) return json.bool_value();
    return default_val;
}

template<>
inline std::string ConfigManager::GetValue(const json11::Json& json, const std::string& default_val) {
    if (json.is_string()) return json.string_value();
    return default_val;
}

} // namespace simple_middleware

