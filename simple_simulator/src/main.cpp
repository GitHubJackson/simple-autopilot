#include "simulator_core.hpp"
#include <iostream>
#include <csignal>
#include <memory>
#include <simple_middleware/logger.hpp>

std::unique_ptr<SimulatorCore> simulator;

void signal_handler(int signal) {
    if (simulator) {
        simulator->Stop();
    }
    simple_middleware::Logger::Info("Stopping Simulator Module...");
    exit(0);
}

int main() {
    simple_middleware::Logger::GetInstance().Init("Simulator", "logs/simulator.log");
    simple_middleware::Logger::Info("=== Simple Simulator Module Starting ===");

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    simulator = std::make_unique<SimulatorCore>();
    simulator->Start();

    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    return 0;
}

