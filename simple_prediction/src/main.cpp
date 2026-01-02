#include "prediction_component.hpp"
#include <iostream>
#include <csignal>
#include <simple_middleware/logger.hpp>

std::unique_ptr<PredictionComponent> prediction;

void signal_handler(int signal) {
    if (prediction) {
        prediction->Stop();
    }
    simple_middleware::Logger::Info("Stopping Prediction Module...");
    exit(0);
}

int main() {
    // 初始化日志
    simple_middleware::Logger::GetInstance().Init("Prediction", "logs/prediction.log");
    
    simple_middleware::Logger::Info("=== Simple Prediction Module Starting ===");

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    prediction = std::make_unique<PredictionComponent>();
    prediction->Start();

    // 保持主线程运行
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    return 0;
}

