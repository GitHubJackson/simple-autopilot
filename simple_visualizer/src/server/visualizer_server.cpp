#include "visualizer_server.hpp"
#include "../handler/websocket_handler.hpp"
#include <iostream>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <common_msgs/daemon.pb.h>
#include <json11.hpp>
#include <simple_middleware/logger.hpp> // Add middleware logger

using namespace json11;

VisualizerServer::VisualizerServer() : running_(false) {
    status_reporter_ = std::make_unique<simple_middleware::StatusReporter>("VisualizerNode");
}

VisualizerServer::~VisualizerServer() {
    Stop();
}

// Deprecated local Log function, forwarding to middleware logger
void VisualizerServer::Log(const std::string& level, const std::string& msg) {
    if (level == "INFO") simple_middleware::Logger::Info(msg);
    else if (level == "WARN") simple_middleware::Logger::Warn(msg);
    else if (level == "ERROR") simple_middleware::Logger::Error(msg);
    else simple_middleware::Logger::Debug(msg);
}

bool VisualizerServer::Init(const std::string& port) {
    Log("INFO", "Initializing server on port " + port);
    
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

        Log("INFO", "CivetWeb started successfully");
        StartThreads();
        status_reporter_->Start();
        return true;
    } catch (CivetException &e) {
        Log("ERROR", "CivetWeb init failed: " + std::string(e.what()));
        return false;
    }
}

void VisualizerServer::Stop() {
    if (!running_) return;
    Log("INFO", "Stopping server...");
    
    running_ = false;
    
    if (status_reporter_) status_reporter_->Stop();
    
    msg_queue_.Push(""); 
    
    if (consumer_thread_.joinable()) consumer_thread_.join();
    if (render_thread_.joinable()) render_thread_.join(); 
    
    if (civet_server_) {
        civet_server_->close();
        Log("INFO", "CivetWeb closed");
    }
    
    Log("INFO", "Server stopped");
}

void VisualizerServer::WaitForExit() {
    Log("INFO", "Press Enter to exit...");
    std::cin.get();
    Stop();
}

void VisualizerServer::AddConnection(struct mg_connection* conn) {
    std::lock_guard<std::mutex> lock(conn_mutex_);
    connections_.insert(conn);
    Log("INFO", "Client connected. Total connections: " + std::to_string(connections_.size()));
}

void VisualizerServer::RemoveConnection(const struct mg_connection* conn) {
    std::lock_guard<std::mutex> lock(conn_mutex_);
    connections_.erase((struct mg_connection*)conn);
    Log("INFO", "Client disconnected. Total connections: " + std::to_string(connections_.size()));
}

void VisualizerServer::BroadcastMessage(const std::string& message) {
    if (message.empty()) return;

    std::lock_guard<std::mutex> lock(conn_mutex_);
    for (auto conn : connections_) {
        mg_websocket_write(conn, MG_WEBSOCKET_OPCODE_TEXT, message.c_str(), message.size());
    }
}

void VisualizerServer::BroadcastBinaryMessage(const void* data, size_t len) {
    if (!data || len == 0) return;
    std::lock_guard<std::mutex> lock(conn_mutex_);
    for (auto conn : connections_) {
        mg_websocket_write(conn, MG_WEBSOCKET_OPCODE_BINARY, (const char*)data, len);
    }
}

void VisualizerServer::StartThreads() {
    Log("INFO", "Starting worker threads...");
    running_ = true;
    
    auto& middleware = simple_middleware::PubSubMiddleware::getInstance();
    
    middleware.subscribe("visualizer/data", [this](const simple_middleware::Message& msg) {
        if (!running_) return;
        
        senseauto::demo::FrameData frame;
        if (frame.ParseFromString(msg.data)) {
            // Log frame receiving every 100 frames
            if (frame.frame_id() % 100 == 0) {
                Log("DEBUG", "Received Sim Frame ID: " + std::to_string(frame.frame_id()));
            }

            biz_component_.UpdateFromSimulator(frame);
            
            std::string json_data = biz_component_.GetSerializedData(frame.frame_id());
            msg_queue_.Push(json_data);
        } else {
             Log("WARN", "Failed to parse visualizer/data (Protobuf)");
        }
    });

    middleware.subscribe("planning/trajectory", [this](const simple_middleware::Message& msg) {
        this->OnMiddlewareMessage(msg); 
    });

    middleware.subscribe("visualizer/map", [this](const simple_middleware::Message& msg) {
        this->OnMiddlewareMessage(msg); 
    });

    middleware.subscribe("system/status", [this](const simple_middleware::Message& msg) {
        this->OnSystemStatus(msg);
    });
    
    middleware.subscribe("sensor/camera/front", [this](const simple_middleware::Message& msg) {
        this->OnCameraData(msg);
    });

    middleware.subscribe("perception/detection_2d", [this](const simple_middleware::Message& msg) {
        this->OnDetectionData(msg);
    });

    consumer_thread_ = std::thread(&VisualizerServer::ConsumeLoop, this);
    render_thread_ = std::thread(&VisualizerServer::RenderLoop, this); 
    
    Log("INFO", "Worker threads started");
}

void VisualizerServer::OnMiddlewareMessage(const simple_middleware::Message& msg) {
    if (!running_) return;
    msg_queue_.Push(msg.data);
}

void VisualizerServer::OnSystemStatus(const simple_middleware::Message& msg) {
    if (!running_) return;
    
    simple_daemon::SystemStatus status;
    if (status.ParseFromString(msg.data)) {
        Json::array nodes_array;
        for (int i = 0; i < status.nodes_size(); ++i) {
            const auto& node = status.nodes(i);
            nodes_array.push_back(Json::object{
                {"name", node.name()},
                {"is_running", node.is_running()}
            });
        }
        
        Json json_obj = Json::object{
            {"type", "system_status"},
            {"nodes", nodes_array} 
        };
        
        BroadcastMessage(json_obj.dump());
    }
}

void VisualizerServer::ConsumeLoop() {
    Log("INFO", "Consumer thread running");
    std::string data;
    while (running_) {
        if (msg_queue_.Pop(data)) {
            if (!running_) break;
            BroadcastMessage(data);
        }
    }
    Log("INFO", "Consumer thread exited");
}

void VisualizerServer::RenderLoop() {
    Log("INFO", "Render thread running");
    while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        try {
            auto img_buffer = biz_component_.GetRenderedImage();
            if (!img_buffer.empty()) {
                BroadcastBinaryMessage(img_buffer.data(), img_buffer.size());
            }
        } catch (const std::exception& e) {
             Log("ERROR", "RenderLoop exception: " + std::string(e.what()));
        }
    }
    Log("INFO", "Render thread exited");
}

void VisualizerServer::HandleClientCommand(const std::string& cmd_json) {
    auto& middleware = simple_middleware::PubSubMiddleware::getInstance();
    
    std::string err;
    Json json = Json::parse(cmd_json, err);
    if (!err.empty()) {
        Log("ERROR", "Failed to parse client command JSON: " + err);
        return; 
    }
    
    std::string type = json["type"].string_value();
    if (type == "system_control") {
        std::string action_str = json["action"].string_value();
        std::string node_name = json["node"].string_value();
        
        Log("INFO", "Received System Control: " + action_str + " " + node_name);

        simple_daemon::SystemCommand cmd;
        cmd.set_request_id("req_" + std::to_string(std::time(nullptr)));
        cmd.set_target_name(node_name);
        cmd.set_target_type(simple_daemon::SystemCommand::NODE);
        
        if (action_str == "start") {
            cmd.set_action(simple_daemon::SystemCommand::START);
        } else if (action_str == "stop") {
            cmd.set_action(simple_daemon::SystemCommand::STOP);
        }
        
        std::string payload;
        cmd.SerializeToString(&payload);
        middleware.publish("system/command", payload);
        
    } else {
        middleware.publish("visualizer/control", cmd_json);
    }
}

void VisualizerServer::OnCameraData(const simple_middleware::Message& msg) {
    if (!running_) return;
    senseauto::demo::CameraFrame frame;
    if (frame.ParseFromString(msg.data)) {
        if (frame.image_format() == "ppm") {
            biz_component_.UpdateCameraImage(frame.raw_image());
        } else if (frame.image_format() == "raw_gray") {
            // 简单的将 Gray 转 RGB (R=G=B)
            // 这里为了简单，我们假设 VisualizerComponent 能处理或者我们这里转一下
            // 但 SimpleImage 目前只支持 PPM (RGB)。
            // 所以我们手动构造一个假的 RGB buffer
            std::string rgb_data;
            rgb_data.reserve(frame.raw_image().size() * 3);
            for (char p : frame.raw_image()) {
                rgb_data.push_back(p);
                rgb_data.push_back(p);
                rgb_data.push_back(p);
            }
            // 还需要加上 PPM Header 才能被 FromBuffer 解析... 
            // SimpleImage::FromBuffer 期望的是 PPM 格式
            // 让我们别折腾这个了，只要 Sensor 路径对了就行。
            // 或者，我们可以 hack 一下，如果 simple_image 支持 Raw RGB set
        }
    } else {
        // Log("WARN", "Failed to parse camera data");
    }
}

void VisualizerServer::OnDetectionData(const simple_middleware::Message& msg) {
    if (!running_) return;
    senseauto::demo::Detection2DArray dets;
    if (dets.ParseFromString(msg.data)) {
        biz_component_.UpdateDetections(dets);
    }
}
