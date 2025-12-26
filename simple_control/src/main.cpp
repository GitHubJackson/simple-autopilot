#include "control_component.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <simple_middleware/logger.hpp>

int main(int argc, char* argv[]) {
    // 初始化日志 (写入 logs/control.log)
    simple_middleware::Logger::GetInstance().Init("Control", "logs/control.log");
    
    simple_middleware::Logger::Info("=== Simple Control Module Starting ===");

    ControlComponent control;
    control.Start();
    
    simple_middleware::Logger::Info("Control module running...");
    
    // 保持主线程运行
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    return 0;
}
