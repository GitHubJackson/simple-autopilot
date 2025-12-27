#include "monitor.hpp"
#include <iostream>
#include <string>
#include <csignal>
#include <simple_middleware/logger.hpp>

SystemMonitor* g_monitor = nullptr;

void signal_handler(int sig) {
    if (g_monitor) {
        // SystemMonitor 的析构函数会被调用，但由于 Run 是阻塞的，我们需要一种方式来优雅退出
        // 这里简单退出，依靠 OS 回收资源
        simple_middleware::Logger::Info("Stopping System Monitor...");
        std::exit(0);
    }
}

int main(int argc, char** argv) {
    // 初始化日志 (写入 logs/monitor.log)
    simple_middleware::Logger::GetInstance().Init("Monitor", "logs/monitor.log");
    simple_middleware::Logger::Info("=== System Monitor Starting ===");
    
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    MonitorMode mode = MonitorMode::ALL;

    // 简单的参数解析
    if (argc >= 2) {
        std::string cmd = argv[1];
        if (cmd == "monitor" && argc >= 3 && std::string(argv[2]) == "status") {
            mode = MonitorMode::TOPIC_STATUS;
        } else if (cmd == "daemon" && argc >= 3 && std::string(argv[2]) == "status") {
            mode = MonitorMode::NODE_STATUS;
        }
    }

    SystemMonitor monitor;
    g_monitor = &monitor;
    
    monitor.Init();
    monitor.Run(mode);
    
    return 0;
}

