#include "logger.hpp"
#include <chrono>
#include <iomanip>
#include <sstream>
#include <sys/stat.h>
#include <filesystem>
#include <cstring>
#include <unistd.h>
#include <cerrno>

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
        // 自动创建日志目录（如果不存在）
        try {
            std::filesystem::path log_path(log_file_path);
            std::filesystem::path log_dir = log_path.parent_path();
            
            if (!log_dir.empty()) {
                if (!std::filesystem::exists(log_dir)) {
                    std::filesystem::create_directories(log_dir);
                }
            }
        } catch (const std::exception& e) {
            // 如果 filesystem 不可用，尝试使用 mkdir
            std::string log_dir = log_file_path.substr(0, log_file_path.find_last_of("/\\"));
            if (!log_dir.empty()) {
                // 递归创建目录（如果父目录不存在）
                size_t pos = 0;
                while ((pos = log_dir.find('/', pos + 1)) != std::string::npos) {
                    std::string sub_dir = log_dir.substr(0, pos);
                    if (!sub_dir.empty()) {
                        mkdir(sub_dir.c_str(), 0777);
                    }
                }
                mkdir(log_dir.c_str(), 0777);
            }
        }
        
        log_file_.open(log_file_path, std::ios::out | std::ios::app);
        if (!log_file_.is_open()) {
            char cwd[1024];
            std::string cwd_str = (getcwd(cwd, sizeof(cwd)) != nullptr) ? cwd : "unknown";
            std::cerr << "[Logger] ERROR: Failed to open log file: " << log_file_path 
                      << " (current working directory: " << cwd_str << ")" << std::endl;
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
