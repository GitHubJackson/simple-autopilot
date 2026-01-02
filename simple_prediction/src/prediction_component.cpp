#include "prediction_component.hpp"
#include <iostream>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <arpa/inet.h> // for htonl
#include <simple_middleware/logger.hpp>

using namespace json11;

PredictionComponent::PredictionComponent() : running_(false) {
    status_reporter_ = std::make_unique<simple_middleware::StatusReporter>("PredictionNode");
}

PredictionComponent::~PredictionComponent() {
    Stop();
}

void PredictionComponent::Start() {
    if (running_) return;
    running_ = true;
    
    auto& middleware = simple_middleware::PubSubMiddleware::getInstance();
    
    // 订阅感知模块的障碍物数据
    middleware.subscribe("perception/obstacles", [this](const simple_middleware::Message& msg) {
        this->OnPerceptionObstacles(msg);
    });
    simple_middleware::Logger::Info("Prediction: Subscribed to perception/obstacles");
    
    // 订阅自车状态（用于坐标转换）
    middleware.subscribe("visualizer/data", [this](const simple_middleware::Message& msg) {
        this->OnCarStatus(msg);
    });
    simple_middleware::Logger::Info("Prediction: Subscribed to visualizer/data");
    
    thread_ = std::thread(&PredictionComponent::RunLoop, this);
    status_reporter_->Start();
    simple_middleware::Logger::Info("Prediction: Started loop.");
}

void PredictionComponent::Stop() {
    status_reporter_->Stop();
    running_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }
}

void PredictionComponent::OnCarStatus(const simple_middleware::Message& msg) {
    senseauto::demo::FrameData frame;
    if (frame.ParseFromString(msg.data)) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (frame.has_car_state()) {
            ego_state_.x = frame.car_state().position().x();
            ego_state_.y = frame.car_state().position().y();
            ego_state_.heading = frame.car_state().heading();
        }
    }
}

void PredictionComponent::OnPerceptionObstacles(const simple_middleware::Message& msg) {
    std::string err;
    Json json = Json::parse(msg.data, err);
    if (!err.empty()) {
        static int parse_fail_count = 0;
        if (parse_fail_count++ % 10 == 0) {
            simple_middleware::Logger::Warn("Prediction: Failed to parse perception/obstacles: " + err);
        }
        return;
    }
    
    if (json["type"].string_value() != "perception_obstacles" || !json["obstacles"].is_array()) {
        return;
    }
    
    int64_t current_timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    std::lock_guard<std::mutex> lock(state_mutex_);
    
    // 更新障碍物历史状态
    int obstacle_count = 0;
    for (const auto& obs_json : json["obstacles"].array_items()) {
        int32_t id = obs_json["id"].int_value();
        double x = obs_json["position"]["x"].number_value();
        double y = obs_json["position"]["y"].number_value();
        double heading = 0.0;  // 感知模块可能没有提供heading，默认为0
        
        UpdateObstacleHistory(id, x, y, heading, current_timestamp);
        obstacle_count++;
    }
    
    static int recv_count = 0;
    if (recv_count++ % 10 == 0 || recv_count == 1) {
        simple_middleware::Logger::Info("Prediction: Received " + std::to_string(obstacle_count) 
            + " obstacles from perception, total histories=" + std::to_string(obstacle_histories_.size()));
    }
    
    // 清理过期的障碍物历史（超过5秒没有更新）
    auto it = obstacle_histories_.begin();
    while (it != obstacle_histories_.end()) {
        if (current_timestamp - it->second.timestamp > 5000) {
            it = obstacle_histories_.erase(it);
        } else {
            ++it;
        }
    }
}

void PredictionComponent::UpdateObstacleHistory(int32_t obstacle_id, double x, double y, double heading, int64_t timestamp) {
    auto it = obstacle_histories_.find(obstacle_id);
    
    if (it == obstacle_histories_.end()) {
        // 新障碍物，初始化历史
        ObstacleHistory history;
        history.x = x;
        history.y = y;
        history.heading = heading;
        history.timestamp = timestamp;
        history.vx = 0.0;
        history.vy = 0.0;
        history.speed = 0.0;
        obstacle_histories_[obstacle_id] = history;
    } else {
        // 更新现有障碍物，计算速度
        ObstacleHistory& history = it->second;
        
        // 计算时间差（秒）
        double dt = (timestamp - history.timestamp) / 1000.0;
        
        if (dt > 0.01 && dt < 5.0) {  // 有效时间差范围（避免异常值）
            // 计算速度（m/s）
            history.vx = (x - history.x) / dt;
            history.vy = (y - history.y) / dt;
            history.speed = std::sqrt(history.vx * history.vx + history.vy * history.vy);
            
            // 更新位置和时间戳
            history.x = x;
            history.y = y;
            history.heading = heading;
            history.timestamp = timestamp;
        } else {
            // 时间差异常，只更新位置，不更新速度
            history.x = x;
            history.y = y;
            history.heading = heading;
            history.timestamp = timestamp;
        }
    }
}

std::vector<PredictedPoint> PredictionComponent::PredictObstacleTrajectory(
    const ObstacleHistory& history,
    double prediction_horizon,
    double time_step) {
    
    std::vector<PredictedPoint> trajectory;
    
    // 如果速度太小，视为静止障碍物
    if (history.speed < min_speed_threshold_) {
        // 静止障碍物：所有预测点都在当前位置
        for (double t = time_step; t <= prediction_horizon; t += time_step) {
            PredictedPoint pt;
            pt.x = history.x;
            pt.y = history.y;
            pt.time_offset = t;
            pt.confidence = 1.0;  // 静止物体预测置信度高
            trajectory.push_back(pt);
        }
    } else {
        // 匀速模型：假设障碍物保持当前速度
        for (double t = time_step; t <= prediction_horizon; t += time_step) {
            PredictedPoint pt;
            pt.x = history.x + history.vx * t;
            pt.y = history.y + history.vy * t;
            pt.time_offset = t;
            
            // 置信度随时间衰减（越远预测越不确定）
            pt.confidence = std::max(0.3, 1.0 - (t / prediction_horizon) * 0.5);
            
            trajectory.push_back(pt);
        }
    }
    
    return trajectory;
}

void PredictionComponent::RunLoop() {
    auto& middleware = simple_middleware::PubSubMiddleware::getInstance();
    
    while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));  // 10Hz
        
        std::vector<Json> predicted_obstacles_json;
        
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            
            // 为每个障碍物生成预测轨迹
            for (const auto& [obstacle_id, history] : obstacle_histories_) {
                // 跳过无效的障碍物（timestamp为0表示未初始化）
                if (history.timestamp == 0) {
                    continue;
                }
                
                // 即使速度是0（静止障碍物），也生成预测轨迹
                // 生成预测轨迹
                auto trajectory = PredictObstacleTrajectory(history, prediction_horizon_, time_step_);
                
                // 转换为JSON格式
                std::vector<Json> trajectory_json;
                for (const auto& pt : trajectory) {
                    trajectory_json.push_back(Json::object{
                        {"x", pt.x},
                        {"y", pt.y},
                        {"time_offset", pt.time_offset},
                        {"confidence", pt.confidence}
                    });
                }
                
                predicted_obstacles_json.push_back(Json::object{
                    {"id", static_cast<int>(obstacle_id)},
                    {"current_position", Json::object{
                        {"x", history.x},
                        {"y", history.y}
                    }},
                    {"velocity", Json::object{
                        {"vx", history.vx},
                        {"vy", history.vy},
                        {"speed", history.speed}
                    }},
                    {"trajectory", Json(trajectory_json)}
                });
            }
        }
        
        // 发布预测结果（即使没有障碍物也发布，让前端知道预测模块在工作）
        int64_t timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        Json prediction_json = Json::object{
            {"type", "prediction_trajectories"},
            {"timestamp", static_cast<double>(timestamp_ms)},
            {"obstacles", Json(predicted_obstacles_json)}
        };
        
        std::string json_string = prediction_json.dump();
        
        // 【修复】UDP 包大小限制：MTU 通常是 1500 字节，减去 IP/UDP 头约 100 字节，实际可用约 1400 字节
        // 但为了安全，我们使用 1200 字节作为分片大小
        const size_t MAX_CHUNK_SIZE = 1200;
        const size_t topic_overhead = 50; // topic 名称和分隔符的开销
        const size_t chunk_header_size = 16; // 分片头：frame_id(4) + chunk_id(4) + total_chunks(4) + chunk_size(4)
        const size_t effective_chunk_size = MAX_CHUNK_SIZE - topic_overhead - chunk_header_size;
        
        bool published = false;
        if (json_string.size() <= effective_chunk_size) {
            // 数据包足够小，直接发送
            published = middleware.publish("prediction/trajectories", json_string);
        } else {
            // 数据包太大，需要分片发送
            // 使用二进制分片协议：frame_id(4) + chunk_id(4) + total_chunks(4) + chunk_size(4) + chunk_data
            size_t total_chunks = (json_string.size() + effective_chunk_size - 1) / effective_chunk_size;
            static uint32_t frame_id_counter = 0;
            uint32_t frame_id = ++frame_id_counter;
            
            // 发送分片
            for (size_t chunk_id = 0; chunk_id < total_chunks; ++chunk_id) {
                size_t chunk_start = chunk_id * effective_chunk_size;
                size_t chunk_size = std::min(effective_chunk_size, json_string.size() - chunk_start);
                
                // 构造分片数据包：header + data
                std::string chunk_packet;
                chunk_packet.resize(16 + chunk_size);
                
                // 写入 header（大端序）
                uint32_t* header = reinterpret_cast<uint32_t*>(&chunk_packet[0]);
                header[0] = htonl(frame_id);
                header[1] = htonl(static_cast<uint32_t>(chunk_id));
                header[2] = htonl(static_cast<uint32_t>(total_chunks));
                header[3] = htonl(static_cast<uint32_t>(chunk_size));
                
                // 写入数据
                std::memcpy(&chunk_packet[16], json_string.data() + chunk_start, chunk_size);
                
                bool chunk_published = middleware.publish("prediction/trajectories/chunk", chunk_packet);
                if (chunk_id == 0) {
                    published = chunk_published; // 使用第一个分片的发布结果
                }
                
                // 在分片之间添加延迟，避免阻塞其他数据发送
                if (chunk_id < total_chunks - 1) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1)); // 1毫秒延迟
                }
            }
            
            static int chunk_count = 0;
            if (chunk_count++ % 10 == 0 || chunk_count == 1) {
                simple_middleware::Logger::Info("Prediction: Published trajectories in " + std::to_string(total_chunks) 
                    + " chunks, total_size=" + std::to_string(json_string.size()) + " bytes, obstacles=" 
                    + std::to_string(predicted_obstacles_json.size()));
            }
        }
        
        static int pub_count = 0;
        if (pub_count++ % 10 == 0 || pub_count == 1) {
            simple_middleware::Logger::Info("Prediction: Published trajectories for " 
                + std::to_string(predicted_obstacles_json.size()) + " obstacles, size=" 
                + std::to_string(json_string.size()) + " bytes, result=" 
                + (published ? "success" : "failed") + ", total_histories=" 
                + std::to_string(obstacle_histories_.size()));
        }
    }
}

