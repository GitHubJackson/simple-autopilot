#include "sensor_component.hpp"
#include <iostream>
#include <signal.h>
#include <simple_middleware/logger.hpp>

SensorComponent* g_component = nullptr;

void signal_handler(int sig) {
    if (g_component) {
        g_component->Stop();
    }
    simple_middleware::Logger::Info("Stopping Sensor Module...");
    exit(0);
}

int main(int argc, char** argv) {
    simple_middleware::Logger::GetInstance().Init("Sensor", "logs/sensor.log");
    simple_middleware::Logger::Info("=== Simple Sensor Module Starting ===");

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    SensorComponent component;
    g_component = &component;
    
    component.Start();

    // 保持主线程运行
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    return 0;
}

