#include "logger.hpp"
#include <chrono>
#include <iomanip>
#include <sstream>
#include <sys/stat.h>

namespace simple_middleware {

Logger::~Logger() {
    if (log_file_.is_open()) {
        log_file_.close();
    }
}

void Logger::Init(const std::string& module_name, const std::string& log_file_path) {
    std::lock_guard<std::mutex> lock(mutex_);
    module_name_ = module_name;
    
    if (!log_file_path.empty()) {
        // 尝试创建目录 (简单 hack，假设只有一级 logs 目录)
        // mkdir("logs", 0777); 
        
        log_file_.open(log_file_path, std::ios::out | std::ios::app);
        if (!log_file_.is_open()) {
            std::cerr << "[Logger] Failed to open log file: " << log_file_path << std::endl;
        }
    }
    initialized_ = true;
}

void Logger::Log(LogLevel level, const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::stringstream ss;
    ss << "[" << std::put_time(std::localtime(&now), "%H:%M:%S") << "] ";
    
    switch (level) {
        case LogLevel::DEBUG: ss << "[DEBUG] "; break;
        case LogLevel::INFO:  ss << "[INFO]  "; break;
        case LogLevel::WARN:  ss << "[WARN]  "; break;
        case LogLevel::ERROR: ss << "[ERROR] "; break;
    }
    
    if (!module_name_.empty()) {
        ss << "[" << module_name_ << "] ";
    }
    
    ss << message;
    
    std::string final_msg = ss.str();
    
    // Output to console
    std::cout << final_msg << std::endl;
    
    // Output to file
    if (log_file_.is_open()) {
        log_file_ << final_msg << std::endl;
        log_file_.flush(); // 确保实时写入
    }
}

} // namespace simple_middleware
