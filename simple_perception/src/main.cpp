#include "perception_component.hpp"
#include <iostream>
#include <csignal>
#include <unistd.h>
#include <mach-o/dyld.h>  // for _NSGetExecutablePath on macOS
#include <simple_middleware/logger.hpp>

std::unique_ptr<PerceptionComponent> perception;

void signal_handler(int signal) {
    if (perception) {
        perception->Stop();
    }
    simple_middleware::Logger::Info("Stopping Perception Module...");
    exit(0);
}

int main() {
    // 获取可执行文件所在目录，构建日志路径
    // 这样可以确保日志文件在 simple_perception/logs/ 目录下，无论从哪个目录启动
    char exe_path[1024] = {0};
    uint32_t size = sizeof(exe_path);
    
    // macOS 使用 _NSGetExecutablePath 获取可执行文件路径
    #ifdef __APPLE__
        if (_NSGetExecutablePath(exe_path, &size) == 0) {
            // 获取可执行文件所在目录
            std::string exe_dir = std::string(exe_path);
            size_t last_slash = exe_dir.find_last_of("/");
            if (last_slash != std::string::npos) {
                exe_dir = exe_dir.substr(0, last_slash);
                // 向上两级到 simple_perception 目录（从 build/perception_node -> build -> simple_perception）
                size_t pos = exe_dir.find_last_of("/");
                if (pos != std::string::npos) {
                    exe_dir = exe_dir.substr(0, pos);
                    std::string log_path = exe_dir + "/logs/perception.log";
                    simple_middleware::Logger::GetInstance().Init("Perception", log_path);
                    simple_middleware::Logger::Info("Perception: Log file path: " + log_path);
                } else {
                    // 回退到相对路径
                    simple_middleware::Logger::GetInstance().Init("Perception", "logs/perception.log");
                }
            } else {
                simple_middleware::Logger::GetInstance().Init("Perception", "logs/perception.log");
            }
        } else {
            simple_middleware::Logger::GetInstance().Init("Perception", "logs/perception.log");
        }
    #else
        simple_middleware::Logger::GetInstance().Init("Perception", "logs/perception.log");
    #endif
    
    simple_middleware::Logger::Info("=== Simple Perception Module Starting ===");

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    perception = std::make_unique<PerceptionComponent>();
    perception->Start();

    // 保持主线程运行
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    return 0;
}

