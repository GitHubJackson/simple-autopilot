#include "perception_component.hpp"
#include <iostream>
#include <csignal>
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
    // 初始化日志 (写入 logs/perception.log)
    // 所有模块都从项目根目录启动，所以使用相对路径即可
    simple_middleware::Logger::GetInstance().Init("Perception", "logs/perception.log");
    
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

