#include "map_component.hpp"
#include <iostream>
#include <csignal>
#include <simple_middleware/logger.hpp>

std::unique_ptr<MapComponent> map_node;

void signal_handler(int signal) {
    if (map_node) {
        map_node->Stop();
    }
    simple_middleware::Logger::Info("Stopping Map Module...");
    exit(0);
}

int main() {
    // 初始化日志 (写入 logs/map.log)
    simple_middleware::Logger::GetInstance().Init("Map", "logs/map.log");
    simple_middleware::Logger::Info("=== Simple Map Module Starting ===");
    
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    map_node = std::make_unique<MapComponent>();
    map_node->Start();

    // 保持主线程运行
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    return 0;
}
