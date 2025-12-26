#include "config_manager.hpp"
#include <fstream>
#include <sstream>

namespace simple_middleware {

ConfigManager& ConfigManager::GetInstance() {
    static ConfigManager instance;
    return instance;
}

bool ConfigManager::Load(const std::string& module_name, const std::string& config_file_path) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::ifstream file(config_file_path);
    if (!file.is_open()) {
        std::cerr << "[ConfigManager] Failed to open config file: " << config_file_path << std::endl;
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string err;
    
    json11::Json json = json11::Json::parse(buffer.str(), err);
    if (!err.empty()) {
        std::cerr << "[ConfigManager] Failed to parse JSON in " << config_file_path << ": " << err << std::endl;
        return false;
    }

    configs_[module_name] = json;
    std::cout << "[ConfigManager] Loaded config for module: " << module_name << std::endl;
    return true;
}

json11::Json ConfigManager::GetConfig(const std::string& module_name) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (configs_.find(module_name) != configs_.end()) {
        return configs_[module_name];
    }
    return json11::Json();
}

} // namespace simple_middleware

