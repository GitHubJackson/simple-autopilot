#include "daemon_server.hpp"
#include <iostream>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <vector>
#include <filesystem>
#include <fcntl.h> // for open
#include <cstdio>  // for popen

namespace fs = std::filesystem;

DaemonServer::DaemonServer() : running_(false) {}

DaemonServer::~DaemonServer() {
    running_ = false;
    if (monitor_thread_.joinable()) {
        monitor_thread_.join();
    }
}

// 辅助函数：获取指定 PID 的 CPU 和 Memory 使用率
// 使用 ps 命令以兼顾 macOS 和 Linux
static void GetProcessStats(pid_t pid, float& cpu, float& mem) {
    cpu = 0.0f;
    mem = 0.0f;
    if (pid <= 0) return;

    // 命令：ps -p <pid> -o %cpu,rss
    // -o %cpu: cpu percentage
    // -o rss: resident set size (in KB)
    // 注意：macOS 和 Linux 的 ps 参数略有不同，但 -o %cpu,rss 是 POSIX 标准的一部分，通常都支持。
    // 但是输出表头可能不一样，为了简单，我们可以使用无表头模式（如果支持）或者跳过第一行。
    // Linux/macOS 通用: ps -p <pid> -o %cpu= -o rss=  (等号表示不显示 header)
    
    std::string cmd = "ps -p " + std::to_string(pid) + " -o %cpu= -o rss=";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return;

    char buffer[128];
    if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        // 输出格式: "  0.0  1234"
        try {
            float c = 0.0f;
            float m = 0.0f; // in KB
            if (sscanf(buffer, "%f %f", &c, &m) == 2) {
                cpu = c;
                mem = m / 1024.0f; // 转换为 MB
            }
        } catch (...) {}
    }
    pclose(pipe);
}

void DaemonServer::Init() {
    auto& middleware = simple_middleware::PubSubMiddleware::getInstance();
    
    // 订阅控制指令
    middleware.subscribe("system/command", 
        [this](const simple_middleware::Message& msg) {
            this->OnCommand(msg);
        });

    std::cout << "[Daemon] Initialized. Listening on system/command..." << std::endl;
}

void DaemonServer::Run() {
    running_ = true;
    monitor_thread_ = std::thread(&DaemonServer::MonitorLoop, this);
    
    // 主线程阻塞等待
    while (running_) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

void DaemonServer::OnCommand(const simple_middleware::Message& msg) {
    simple_daemon::SystemCommand cmd;
    if (!cmd.ParseFromString(msg.data)) { // Use msg.data, not msg.payload
        std::cerr << "[Daemon] Failed to parse command." << std::endl;
        return;
    }

    std::cout << "[Daemon] Received command: " << cmd.DebugString() << std::endl;

    if (cmd.target_type() == simple_daemon::SystemCommand::NODE) {
        if (cmd.action() == simple_daemon::SystemCommand::START) {
            StartNode(cmd.target_name(), cmd.request_id());
        } else if (cmd.action() == simple_daemon::SystemCommand::STOP) {
            StopNode(cmd.target_name(), cmd.request_id());
        }
    }
}

std::string DaemonServer::GetExecutablePath(const std::string& node_name) {
    // 简单的路径映射，假设从 demos 根目录运行
    if (node_name == "simple_planning") return "./simple_planning/build/planning_node";
    if (node_name == "simple_control") return "./simple_control/build/control_server";
    if (node_name == "simple_perception") return "./simple_perception/build/perception_node";
    if (node_name == "simple_visualizer") return "./simple_visualizer/build/server";
    
    return "";
}

void DaemonServer::StartNode(const std::string& node_name, const std::string& request_id) {
    std::lock_guard<std::mutex> lock(processes_mutex_);

    if (processes_.count(node_name) && processes_[node_name].is_running) {
        SendResponse(request_id, false, "Node is already running.");
        return;
    }

    std::string path = GetExecutablePath(node_name);
    if (path.empty()) {
        SendResponse(request_id, false, "Unknown node name.");
        return;
    }
    
    // 检查文件是否存在
    if (!fs::exists(path)) {
        SendResponse(request_id, false, "Executable not found at: " + path);
        return;
    }

    pid_t pid = fork();
    if (pid == 0) {
        // 子进程
        // 1. 创建日志目录
        std::string log_dir = "./logs";
        if (!fs::exists(log_dir)) {
            fs::create_directory(log_dir);
        }

        // 2. 重定向标准输出和错误到日志文件
        std::string log_file = log_dir + "/" + node_name + ".log";
        int fd = open(log_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd != -1) {
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            close(fd);
        }

        // 3. 执行程序
        execl(path.c_str(), path.c_str(), (char*)NULL);
        perror("execl failed");
        exit(1);
    } else if (pid > 0) {
        ProcessInfo info;
        info.pid = pid;
        info.name = node_name;
        info.is_running = true;
        processes_[node_name] = info;
        
        std::cout << "[Daemon] Started " << node_name << " with PID " << pid << std::endl;
        SendResponse(request_id, true, "Started " + node_name);
    } else {
        SendResponse(request_id, false, "Fork failed.");
    }
}

void DaemonServer::StopNode(const std::string& node_name, const std::string& request_id) {
    std::lock_guard<std::mutex> lock(processes_mutex_);

    auto it = processes_.find(node_name);
    if (it == processes_.end() || !it->second.is_running) {
        SendResponse(request_id, false, "Node is not running.");
        return;
    }

    if (kill(it->second.pid, SIGTERM) == 0) {
        std::cout << "[Daemon] Sent SIGTERM to " << node_name << " (PID " << it->second.pid << ")" << std::endl;
        SendResponse(request_id, true, "Stop signal sent to " + node_name);
    } else {
        SendResponse(request_id, false, "Failed to send kill signal.");
    }
}

void DaemonServer::SendResponse(const std::string& request_id, bool success, const std::string& message) {
    if (request_id.empty()) return;

    simple_daemon::CommandResponse resp;
    resp.set_request_id(request_id);
    resp.set_success(success);
    resp.set_message(message);

    std::string payload;
    resp.SerializeToString(&payload);
    
    auto& middleware = simple_middleware::PubSubMiddleware::getInstance();
    middleware.publish("system/response", payload);
}

void DaemonServer::MonitorLoop() {
    auto& middleware = simple_middleware::PubSubMiddleware::getInstance();
    while (running_) {
        {
            std::lock_guard<std::mutex> lock(processes_mutex_);
            int status;
            pid_t pid;
            while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
                for (auto& pair : processes_) {
                    if (pair.second.pid == pid) {
                        pair.second.is_running = false;
                        std::cout << "[Daemon] Process " << pair.first << " (PID " << pid << ") exited." << std::endl;
                        break;
                    }
                }
            }
        }

        simple_daemon::SystemStatus status_msg;
        {
            std::lock_guard<std::mutex> lock(processes_mutex_);
            for (const auto& pair : processes_) {
                auto* node = status_msg.add_nodes();
                node->set_name(pair.first);
                node->set_is_running(pair.second.is_running);
                node->set_pid(pair.second.pid);
                
                // 采集资源使用情况
                if (pair.second.is_running) {
                    float cpu = 0.0f, mem = 0.0f;
                    GetProcessStats(pair.second.pid, cpu, mem);
                    node->set_cpu_usage(cpu);
                    node->set_memory_usage(mem);
                }
            }
        }
        
        std::string payload;
        status_msg.SerializeToString(&payload);
        middleware.publish("system/status", payload);

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}
