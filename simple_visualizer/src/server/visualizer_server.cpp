#include "visualizer_server.hpp"
#include "../handler/websocket_handler.hpp"
#include <iostream>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <arpa/inet.h> // for ntohl
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
    
    static int broadcast_counter = 0;
    if (connections_.empty()) {
        if (broadcast_counter++ % 100 == 0) { // 每 10 秒输出一次
            Log("DEBUG", "No WebSocket connections, skipping image broadcast");
        }
        return;
    }
    
    for (auto conn : connections_) {
        mg_websocket_write(conn, MG_WEBSOCKET_OPCODE_BINARY, (const char*)data, len);
    }
    
    if (broadcast_counter++ % 30 == 0) { // 每 3 秒输出一次
        Log("DEBUG", "Broadcasted binary message to " + std::to_string(connections_.size()) 
            + " connections, size=" + std::to_string(len) + " bytes");
    }
}

void VisualizerServer::StartThreads() {
    Log("INFO", "Starting worker threads...");
    running_ = true;
    
    auto& middleware = simple_middleware::PubSubMiddleware::getInstance();
    
    int64_t data_sub_id = middleware.subscribe("visualizer/data", [this](const simple_middleware::Message& msg) {
        if (!running_) return;
        
        senseauto::demo::FrameData frame;
        if (frame.ParseFromString(msg.data)) {
            static int recv_count = 0;
            if (recv_count++ % 30 == 0 || recv_count == 1) { // 每 30 帧或第一次打印
                Log("DEBUG", "Received Sim Frame ID: " + std::to_string(frame.frame_id())
                    + ", car_state: x=" + std::to_string(frame.car_state().position().x())
                    + ", y=" + std::to_string(frame.car_state().position().y())
                    + ", speed=" + std::to_string(frame.car_state().speed()));
            }

            biz_component_.UpdateFromSimulator(frame);
            
            std::string json_data = biz_component_.GetSerializedData(frame.frame_id());
            msg_queue_.Push(json_data);
        } else {
             static int parse_fail_count = 0;
             if (parse_fail_count++ % 10 == 0) {
                 Log("WARN", "Failed to parse visualizer/data (Protobuf), message size=" 
                     + std::to_string(msg.data.size()));
             }
        }
    });
    if (data_sub_id >= 0) {
        Log("INFO", "Subscribed to visualizer/data (ID: " + std::to_string(data_sub_id) + ")");
    } else {
        Log("ERROR", "Failed to subscribe to visualizer/data");
    }

    middleware.subscribe("planning/trajectory", [this](const simple_middleware::Message& msg) {
        this->OnMiddlewareMessage(msg); 
    });
    
    // 订阅规划轨迹分片
    middleware.subscribe("planning/trajectory/chunk", [this](const simple_middleware::Message& msg) {
        this->OnTrajectoryChunk(msg);
    });

    middleware.subscribe("visualizer/map", [this](const simple_middleware::Message& msg) {
        this->OnMiddlewareMessage(msg); 
    });

    middleware.subscribe("system/status", [this](const simple_middleware::Message& msg) {
        this->OnSystemStatus(msg);
    });
    
    middleware.subscribe("sensor/camera/front", [this](const simple_middleware::Message& msg) {
        static int recv_count = 0;
        if (recv_count++ % 30 == 0) {
            Log("DEBUG", "Visualizer: Received sensor/camera/front message, size=" + std::to_string(msg.data.size()));
        }
        this->OnCameraData(msg);
    });

    int64_t chunk_sub_id = middleware.subscribe("sensor/camera/front/chunk", [this](const simple_middleware::Message& msg) {
        this->OnCameraChunk(msg);
    });
    if (chunk_sub_id >= 0) {
        Log("INFO", "Subscribed to sensor/camera/front/chunk (ID: " + std::to_string(chunk_sub_id) + ")");
    } else {
        Log("ERROR", "Failed to subscribe to sensor/camera/front/chunk");
    }

    int64_t det_sub_id = middleware.subscribe("perception/detection_2d", [this](const simple_middleware::Message& msg) {
        Log("INFO", "Perception/detection_2d callback triggered! message size=" + std::to_string(msg.data.size()));
        this->OnDetectionData(msg);
    });
    if (det_sub_id >= 0) {
        Log("INFO", "Subscribed to perception/detection_2d (ID: " + std::to_string(det_sub_id) + ")");
    } else {
        Log("ERROR", "Failed to subscribe to perception/detection_2d");
    }

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
    static int render_counter = 0;
    static int empty_counter = 0;
    
    while (running_) {
        // 1Hz 频率（每秒1帧），与 Sensor 同步
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        
        try {
            auto img_buffer = biz_component_.GetRenderedImage();
            if (!img_buffer.empty()) {
                BroadcastBinaryMessage(img_buffer.data(), img_buffer.size());
                render_counter++;
                if (render_counter % 5 == 0 || render_counter <= 5) { // 每 5 秒或前 5 次打印
                    Log("DEBUG", "Broadcasted image: size=" + std::to_string(img_buffer.size()) 
                        + " bytes, total sent=" + std::to_string(render_counter));
                }
            } else {
                empty_counter++;
                if (empty_counter % 5 == 0 || empty_counter <= 5) { // 每 5 秒或前 5 次打印
                    Log("DEBUG", "GetRenderedImage returned empty buffer (count=" + std::to_string(empty_counter) + ")");
                }
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
        static int frame_count = 0;
        frame_count++;
        if (frame_count % 5 == 0 || frame_count <= 5) {
            Log("DEBUG", "Received camera frame #" + std::to_string(frame_count) 
                + ": format=" + frame.image_format() 
                + ", size=" + std::to_string(frame.raw_image().size())
                + ", width=" + std::to_string(frame.image_width())
                + ", height=" + std::to_string(frame.image_height()));
        }
        
        if (frame.image_format() == "ppm") {
            // 检查是否是纯 RGB 数据（不含 header）
            // 如果数据大小正好是 width*height*3，说明是纯 RGB 数据
            size_t expected_rgb_size = frame.image_width() * frame.image_height() * 3;
            bool is_pure_rgb = (frame.raw_image().size() == expected_rgb_size);
            
            bool success = false;
            if (is_pure_rgb) {
                // 纯 RGB 数据，直接设置
                success = biz_component_.UpdateCameraImageRGB(
                    frame.raw_image(), 
                    frame.image_width(), 
                    frame.image_height());
            } else {
                // 完整 PPM 文件（含 header），使用原有方法
                success = biz_component_.UpdateCameraImage(frame.raw_image());
            }
            
            if (frame_count % 5 == 0 || frame_count <= 5) {
                Log("DEBUG", "UpdateCameraImage result: " + std::string(success ? "success" : "failed")
                    + ", format=" + (is_pure_rgb ? "RGB" : "PPM"));
            }
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
            Log("WARN", "raw_gray format not fully supported yet");
        } else {
            Log("WARN", "Unknown image format: " + frame.image_format());
        }
    } else {
        static int parse_fail_count = 0;
        if (parse_fail_count++ % 30 == 0) {
            Log("WARN", "Failed to parse camera data (Protobuf), message size=" + std::to_string(msg.data.size()));
        }
    }
}

void VisualizerServer::OnCameraChunk(const simple_middleware::Message& msg) {
    if (!running_) return;
    
    // 解析分片头：frame_id(4) + chunk_id(4) + total_chunks(4) + chunk_size(4) + chunk_data
    if (msg.data.size() < 16) {
        static int error_count = 0;
        if (error_count++ % 100 == 0) {
            Log("WARN", "Camera chunk too small: " + std::to_string(msg.data.size()) + " bytes");
        }
        return;
    }
    
    const uint32_t* header = reinterpret_cast<const uint32_t*>(msg.data.data());
    uint32_t frame_id = ntohl(header[0]);
    uint32_t chunk_id = ntohl(header[1]);
    uint32_t total_chunks = ntohl(header[2]);
    uint32_t chunk_size = ntohl(header[3]);
    
    if (msg.data.size() != 16 + chunk_size) {
        static int error_count = 0;
        if (error_count++ % 100 == 0) {
            Log("WARN", "Camera chunk size mismatch: expected " + std::to_string(16 + chunk_size) 
                + ", got " + std::to_string(msg.data.size()));
        }
        return;
    }
    
    // 提取分片数据
    std::string chunk_data = msg.data.substr(16, chunk_size);
    
    // 准备重组后的完整消息（如果需要）
    std::string full_data;
    bool should_process = false;
    
    {
        std::lock_guard<std::mutex> lock(chunk_mutex_);
        
        // 获取或创建分片缓冲区
        auto& buffer = chunk_buffers_[frame_id];
        buffer.frame_id = frame_id;
        buffer.total_chunks = total_chunks;
        buffer.last_update = std::chrono::steady_clock::now();
        
        // 确保 chunks 数组大小足够
        if (buffer.chunks.size() < total_chunks) {
            buffer.chunks.resize(total_chunks);
        }
        
        // 存储分片数据
        if (chunk_id < total_chunks) {
            buffer.chunks[chunk_id] = chunk_data;
        }
        
        // 检查是否所有分片都已收到
        bool all_received = true;
        for (size_t i = 0; i < total_chunks; ++i) {
            if (buffer.chunks[i].empty()) {
                all_received = false;
                break;
            }
        }
        
        if (all_received) {
            // 重组完整数据
            full_data.reserve(total_chunks * chunk_data.size());
            for (const auto& chunk : buffer.chunks) {
                full_data += chunk;
            }
            
            should_process = true;
            
            // 删除已重组的分片缓冲区
            chunk_buffers_.erase(frame_id);
        }
        
        // 清理超时的分片缓冲区
        auto now = std::chrono::steady_clock::now();
        for (auto it = chunk_buffers_.begin(); it != chunk_buffers_.end();) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - it->second.last_update).count();
            if (elapsed > CHUNK_TIMEOUT_MS) {
                Log("WARN", "Camera chunk timeout for frame " + std::to_string(it->first));
                it = chunk_buffers_.erase(it);
            } else {
                ++it;
            }
        }
    } // 锁在这里自动释放
    
    // 在锁外处理完整数据（避免死锁）
    if (should_process) {
        // 构造完整的 Message 并调用 OnCameraData
        simple_middleware::Message full_msg("sensor/camera/front", full_data);
        full_msg.timestamp = msg.timestamp;
        
        static int reassemble_count = 0;
        if (reassemble_count++ % 30 == 0) {
            Log("DEBUG", "Reassembled camera frame " + std::to_string(frame_id) 
                + " from " + std::to_string(total_chunks) + " chunks, total size=" 
                + std::to_string(full_data.size()));
        }
        
        OnCameraData(full_msg);
    }
}

void VisualizerServer::OnDetectionData(const simple_middleware::Message& msg) {
    if (!running_) return;
    
    try {
        static int recv_count = 0;
        recv_count++;
        // 总是打印，因为检测数据频率低（1Hz），不会太多日志
        Log("INFO", "OnDetectionData called #" + std::to_string(recv_count) + ": message size=" + std::to_string(msg.data.size()));
        
        senseauto::demo::Detection2DArray dets;
        if (dets.ParseFromString(msg.data)) {
            static int det_count = 0;
            det_count++;
            if (det_count % 3 == 0 || det_count <= 5) { // 每 3 帧或前 5 帧打印
                Log("DEBUG", "Received detection data #" + std::to_string(det_count) + ": " + std::to_string(dets.boxes_size()) + " boxes");
                
                // 打印每个检测框的详细信息
                for (int i = 0; i < dets.boxes_size(); ++i) {
                    const auto& box = dets.boxes(i);
                    Log("DEBUG", "  Detection box " + std::to_string(i) + ": x=" 
                        + std::to_string(box.x()) + ", y=" + std::to_string(box.y())
                        + ", w=" + std::to_string(box.width()) + ", h=" + std::to_string(box.height())
                        + ", label=" + box.label());
                }
            }
            biz_component_.UpdateDetections(dets);
            
            if (det_count <= 5 || det_count % 3 == 0) {
                Log("DEBUG", "Updated detections in VisualizerComponent, total boxes=" + std::to_string(dets.boxes_size()));
            }
        } else {
            static int parse_fail_count = 0;
            parse_fail_count++;
            if (parse_fail_count % 3 == 0 || parse_fail_count <= 5) {
                Log("WARN", "Failed to parse detection data (Protobuf), message size=" + std::to_string(msg.data.size()));
            }
        }
    } catch (const std::exception& e) {
        static int error_count = 0;
        if (error_count++ % 10 == 0) {
            Log("ERROR", "Exception in OnDetectionData: " + std::string(e.what()));
        }
    } catch (...) {
        static int error_count = 0;
        if (error_count++ % 10 == 0) {
            Log("ERROR", "Unknown exception in OnDetectionData");
        }
    }
}

void VisualizerServer::OnTrajectoryChunk(const simple_middleware::Message& msg) {
    if (!running_) return;
    
    try {
        // 解析分片头：frame_id(4) + chunk_id(4) + total_chunks(4) + chunk_size(4) + chunk_data
        if (msg.data.size() < 16) {
            static int error_count = 0;
            if (error_count++ % 100 == 0) {
                Log("WARN", "Trajectory chunk too small: " + std::to_string(msg.data.size()) + " bytes");
            }
            return;
        }
        
        const uint32_t* header = reinterpret_cast<const uint32_t*>(msg.data.data());
        uint32_t frame_id = ntohl(header[0]);
        uint32_t chunk_id = ntohl(header[1]);
        uint32_t total_chunks = ntohl(header[2]);
        uint32_t chunk_size = ntohl(header[3]);
        
        if (msg.data.size() != 16 + chunk_size) {
            static int error_count = 0;
            if (error_count++ % 100 == 0) {
                Log("WARN", "Trajectory chunk size mismatch: expected " + std::to_string(16 + chunk_size) 
                    + ", got " + std::to_string(msg.data.size()));
            }
            return;
        }
        
        // 提取分片数据
        std::string chunk_data = msg.data.substr(16, chunk_size);
        
        bool should_process = false;
        std::string full_data;
        
        {
            std::lock_guard<std::mutex> lock(chunk_mutex_);
            
            // 获取或创建分片缓冲区
            auto& buffer = chunk_buffers_[frame_id];
            buffer.frame_id = frame_id;
            buffer.total_chunks = total_chunks;
            buffer.last_update = std::chrono::steady_clock::now();
            
            // 确保 chunks 数组大小足够
            if (buffer.chunks.size() < total_chunks) {
                buffer.chunks.resize(total_chunks);
            }
            
            // 存储分片数据
            if (chunk_id < total_chunks) {
                buffer.chunks[chunk_id] = chunk_data;
            }
            
            // 检查是否所有分片都已收到
            bool all_received = true;
            for (size_t i = 0; i < total_chunks; ++i) {
                if (buffer.chunks[i].empty()) {
                    all_received = false;
                    break;
                }
            }
            
            if (all_received) {
                // 重组完整数据
                full_data.reserve(total_chunks * chunk_data.size());
                for (const auto& chunk : buffer.chunks) {
                    full_data += chunk;
                }
                
                should_process = true;
                chunk_buffers_.erase(frame_id);
            }
            
            // 清理超时的分片缓冲区
            auto now = std::chrono::steady_clock::now();
            for (auto it = chunk_buffers_.begin(); it != chunk_buffers_.end();) {
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - it->second.last_update).count();
                if (elapsed > CHUNK_TIMEOUT_MS) {
                    Log("WARN", "Trajectory chunk timeout for frame " + std::to_string(it->first));
                    it = chunk_buffers_.erase(it);
                } else {
                    ++it;
                }
            }
        }
        
        // 在锁外处理完整数据
        if (should_process) {
            static int reassemble_count = 0;
            if (reassemble_count++ % 10 == 0) {
                Log("DEBUG", "Reassembled trajectory frame " + std::to_string(frame_id) 
                    + " from " + std::to_string(total_chunks) + " chunks, total size=" 
                    + std::to_string(full_data.size()));
            }
            
            // 构造完整的 Message 并转发给前端
            simple_middleware::Message full_msg("planning/trajectory", full_data);
            full_msg.timestamp = msg.timestamp;
            OnMiddlewareMessage(full_msg);
        }
    } catch (const std::exception& e) {
        static int error_count = 0;
        if (error_count++ % 10 == 0) {
            Log("ERROR", "Exception in OnTrajectoryChunk: " + std::string(e.what()));
        }
    } catch (...) {
        static int error_count = 0;
        if (error_count++ % 10 == 0) {
            Log("ERROR", "Unknown exception in OnTrajectoryChunk");
        }
    }
}
