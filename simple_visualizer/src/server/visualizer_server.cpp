#include "visualizer_server.hpp"
#include "../handler/websocket_handler.hpp"
#include <iostream>
#include <chrono>

VisualizerServer::VisualizerServer() : running_(false) {
    status_reporter_ = std::make_unique<simple_middleware::StatusReporter>("VisualizerNode");
}

VisualizerServer::~VisualizerServer() {
    Stop();
}

bool VisualizerServer::Init(const std::string& port) {
    const char *options[] = {
        "listening_ports", port.c_str(),
        "document_root", document_root_.c_str(),
        "num_threads", "5",
        0
    };

    try {
        civet_server_ = std::make_unique<CivetServer>(options);
        ws_handler_ = std::make_unique<RealtimeWebSocketHandler>(*this);
        civet_server_->addWebSocketHandler("/ws", *ws_handler_.get());

        std::cout << "[Server] Started on port " << port << std::endl;
        StartThreads(); // 启动生产者和消费者
        status_reporter_->Start();
        return true;
    } catch (CivetException &e) {
        std::cerr << "[Error] CivetWeb init failed: " << e.what() << std::endl;
        return false;
    }
}

void VisualizerServer::Stop() {
    running_ = false;
    if (status_reporter_) status_reporter_->Stop();
    
    // 强制唤醒可能正在等待的消费者线程
    // 注意：这里我们通过入队一个空消息来唤醒，更好的做法是在队列类里加 Stop()
    msg_queue_.Push(""); 
    
    if (consumer_thread_.joinable()) consumer_thread_.join();
    
    if (civet_server_) civet_server_->close();
}

void VisualizerServer::WaitForExit() {
    std::cout << "[Server] Press Enter to exit..." << std::endl;
    std::cin.get();
    Stop();
}

void VisualizerServer::AddConnection(struct mg_connection* conn) {
    std::lock_guard<std::mutex> lock(conn_mutex_);
    connections_.insert(conn);
    std::cout << "[Conn] Client connected. Total: " << connections_.size() << std::endl;
}

void VisualizerServer::RemoveConnection(const struct mg_connection* conn) {
    std::lock_guard<std::mutex> lock(conn_mutex_);
    connections_.erase((struct mg_connection*)conn);
    std::cout << "[Conn] Client disconnected. Total: " << connections_.size() << std::endl;
}

void VisualizerServer::BroadcastMessage(const std::string& message) {
    if (message.empty()) return; // 过滤空消息

    std::lock_guard<std::mutex> lock(conn_mutex_);
    for (auto conn : connections_) {
        mg_websocket_write(conn, MG_WEBSOCKET_OPCODE_TEXT, message.c_str(), message.size());
    }
}

void VisualizerServer::StartThreads() {
    running_ = true;
    
    // 订阅中间件消息
    auto& middleware = simple_middleware::PubSubMiddleware::getInstance();
    middleware.subscribe("visualizer/data", [this](const simple_middleware::Message& msg) {
        this->OnMiddlewareMessage(msg);
    });

    consumer_thread_ = std::thread(&VisualizerServer::ConsumeLoop, this);
}

void VisualizerServer::OnMiddlewareMessage(const simple_middleware::Message& msg) {
    if (!running_) return;
    msg_queue_.Push(msg.data);
}

// 消费者线程：负责“消费”数据并发送
void VisualizerServer::ConsumeLoop() {
    std::string data;
    while (running_) {
        // 从队列取出数据 (如果队列空了，这里会阻塞挂起，不占 CPU)
        if (msg_queue_.Pop(data)) {
            if (!running_) break;
            BroadcastMessage(data);
        }
    }
}

// --- JSON Helpers (保持不变) ---
std::string VisualizerServer::parseJsonString(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\":";
    size_t start = json.find(search);
    if (start == std::string::npos) return "";
    start += search.length();
    while (start < json.length() && (json[start] == ' ' || json[start] == '\"')) start++;
    size_t end = start;
    while (end < json.length() && json[end] != '\"' && json[end] != ',' && json[end] != '}') end++;
    return json.substr(start, end - start);
}

double VisualizerServer::parseJsonDouble(const std::string& json, const std::string& key) {
    std::string val = parseJsonString(json, key);
    if (val.empty()) return 0.0;
    try {
        return std::stod(val);
    } catch (...) {
        return 0.0;
    }
}

void VisualizerServer::HandleClientCommand(const std::string& cmd_json) {
    // 转发给 Control 模块 (通过中间件发布指令)
    auto& middleware = simple_middleware::PubSubMiddleware::getInstance();
    middleware.publish("visualizer/control", cmd_json);
}
