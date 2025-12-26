#include "planning_component.hpp"
#include <iostream>
#include <csignal>
#include <simple_middleware/logger.hpp>

std::unique_ptr<PlanningComponent> planning_node;

void signal_handler(int signal) {
    if (planning_node) {
        planning_node->Stop();
    }
    simple_middleware::Logger::Info("Stopping Planning Module...");
    exit(0);
}

int main() {
    simple_middleware::Logger::GetInstance().Init("Planning", "logs/planning.log");
    simple_middleware::Logger::Info("=== Simple Planning Module Starting ===");

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    planning_node = std::make_unique<PlanningComponent>();
    planning_node->Start();

    // 保持主线程运行
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    return 0;
}

