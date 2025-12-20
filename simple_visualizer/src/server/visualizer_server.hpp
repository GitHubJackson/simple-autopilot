#pragma once

#include "CivetServer.h"
#include <simple_middleware/pub_sub_middleware.hpp>
#include <simple_middleware/status_reporter.hpp>
#include "../common/thread_safe_queue.hpp" // 引入队列
#include <memory>
#include <set>
#include <mutex>
#include <thread>
#include <atomic>
#include <string>

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
    
    void HandleClientCommand(const std::string& cmd_json);

private:
    void StartThreads();
    
    // 消费者：从队列取出数据，推送到 WebSocket
    void ConsumeLoop();
    
    // 中间件消息回调
    void OnMiddlewareMessage(const simple_middleware::Message& msg);
    
    std::string parseJsonString(const std::string& json, const std::string& key);
    double parseJsonDouble(const std::string& json, const std::string& key);

private:
    std::unique_ptr<CivetServer> civet_server_;
    std::unique_ptr<RealtimeWebSocketHandler> ws_handler_;
    std::unique_ptr<simple_middleware::StatusReporter> status_reporter_;
    
    // 线程安全队列：存储待发送的 JSON 字符串
    // 实际项目中可能存的是 Protobuf 对象或智能指针
    ThreadSafeQueue<std::string> msg_queue_;

    std::set<struct mg_connection*> connections_;
    std::mutex conn_mutex_;
    
    std::thread consumer_thread_;
    
    std::atomic<bool> running_;
    
    const std::string document_root_ = "./www";
};
