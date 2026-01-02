/*
 * @Desc: SenseAuto Visualizer Demo - Refactored (Stage 4)
 * @Author: Lucas & AI Assistant
 * @Date: 2025/12/17
 */

#include "server/visualizer_server.hpp"
#include <signal.h>
#include <simple_middleware/logger.hpp> // Add logger include

#define PORT "8080"

int main(int argc, char *argv[]) {
    // 初始化日志 (写入 logs/visualizer.log)
    simple_middleware::Logger::GetInstance().Init("Visualizer", "logs/visualizer.log");
    simple_middleware::Logger::Info("=== Visualizer Server Starting ===");

    // 忽略 SIGPIPE 信号，防止向断开的连接写数据导致进程退出
    signal(SIGPIPE, SIG_IGN);

    VisualizerServer viz_server;
    
    if (viz_server.Init(PORT)) {
        viz_server.WaitForExit();
    }

    return 0;
}
