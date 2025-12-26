#include "daemon_server.hpp"
#include <iostream>
#include <signal.h>
#include <simple_middleware/logger.hpp>

DaemonServer* g_server = nullptr;

void signal_handler(int sig) {
    if (g_server) {
        simple_middleware::Logger::Info("Shutting down Daemon...");
        exit(0);
    }
}

int main(int argc, char** argv) {
    simple_middleware::Logger::GetInstance().Init("Daemon", "logs/daemon.log");
    simple_middleware::Logger::Info("=== Simple Daemon Module Starting ===");

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    DaemonServer server;
    g_server = &server;

    server.Init();
    server.Run();

    return 0;
}

