#pragma once

#include "CivetServer.h"
#include <simple_middleware/pub_sub_middleware.hpp>
#include <simple_middleware/status_reporter.hpp>
#include "../common/thread_safe_queue.hpp" // 引入队列
#include <json11.hpp>
#include <memory>
#include <set>
#include <mutex>
#include <thread>
#include <atomic>
#include <string>

#include "../component/visualizer_component.hpp" // New

// 前置声明
class RealtimeWebSocketHandler;

class VisualizerServer {
public:
    VisualizerServer();
    ~VisualizerServer();

    bool Init(const std::string& port);
    void Stop();
    void WaitForExit();

    void AddConnection(struct mg_connection* conn);
    void RemoveConnection(const struct mg_connection* conn);
    void BroadcastMessage(const std::string& message);
    void BroadcastBinaryMessage(const void* data, size_t len); // New
    
    void HandleClientCommand(const std::string& cmd_json);

private:
    // 日志辅助函数
    void Log(const std::string& level, const std::string& msg);

private:
    void StartThreads();
    
    // 消费者：从队列取出数据，推送到 WebSocket
    void ConsumeLoop();
    void RenderLoop(); // New: 定时渲染图片
    
    // 中间件消息回调
    void OnMiddlewareMessage(const simple_middleware::Message& msg);
    void OnSystemStatus(const simple_middleware::Message& msg);
    void OnCameraData(const simple_middleware::Message& msg); // New
    void OnDetectionData(const simple_middleware::Message& msg); // New

private:
    std::unique_ptr<CivetServer> civet_server_;
    std::unique_ptr<RealtimeWebSocketHandler> ws_handler_;
    std::unique_ptr<simple_middleware::StatusReporter> status_reporter_;
    
    // 业务组件
    VisualizerComponent biz_component_; // New

    // 线程安全队列：存储待发送的 JSON 字符串
    // 实际项目中可能存的是 Protobuf 对象或智能指针
    ThreadSafeQueue<std::string> msg_queue_;

    std::set<struct mg_connection*> connections_;
    std::mutex conn_mutex_;
    
    std::thread consumer_thread_;
    std::thread render_thread_; // New
    
    std::atomic<bool> running_;
    
    const std::string document_root_ = "./www";
};
