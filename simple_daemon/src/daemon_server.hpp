#pragma once

#include <simple_middleware/pub_sub_middleware.hpp>
#include <common_msgs/daemon.pb.h>
#include <map>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <memory>
#include <sys/types.h>

class DaemonServer {
public:
    DaemonServer();
    ~DaemonServer();

    void Init();
    void Run();

private:
    void OnCommand(const simple_middleware::Message& msg);
    void StartNode(const std::string& node_name, const std::string& request_id);
    void StopNode(const std::string& node_name, const std::string& request_id);
    void MonitorLoop();

    // 辅助函数：根据名字获取可执行文件路径
    std::string GetExecutablePath(const std::string& node_name);

    void SendResponse(const std::string& request_id, bool success, const std::string& message);

private:
    // 进程管理
    struct ProcessInfo {
        pid_t pid;
        std::string name;
        bool is_running;
    };
    std::map<std::string, ProcessInfo> processes_;
    std::mutex processes_mutex_;

    std::thread monitor_thread_;
    std::atomic<bool> running_;
};
