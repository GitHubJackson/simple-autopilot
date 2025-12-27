#include "planning_component.hpp"
#include <iostream>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <google/protobuf/util/json_util.h>
#include <simple_middleware/logger.hpp> // Add logger include
#include <arpa/inet.h> // for htonl, ntohl
#include <cstring> // for memcpy

using namespace json11;

PlanningComponent::PlanningComponent() : running_(false) {
    status_reporter_ = std::make_unique<simple_middleware::StatusReporter>("PlanningNode");
    
    // Load config
    auto& config = simple_middleware::ConfigManager::GetInstance();
    if (config.Load("planning", "config/planning.json")) {
        loop_rate_ms_ = config.Get<int>("planning", "loop_rate_ms", 100);
        target_reach_threshold_ = config.Get<double>("planning", "target_reach_threshold", 1.0);
        default_cruise_speed_ = config.Get<double>("planning", "default_cruise_speed", 5.0);
        follow_distance_ = config.Get<double>("planning", "follow_distance", 15.0);
        acc_kp_ = config.Get<double>("planning", "acc_kp", 0.5);
    }
}

PlanningComponent::~PlanningComponent() {
    Stop();
}

void PlanningComponent::Start() {
    if (running_) return;
    running_ = true;
    
    auto& middleware = simple_middleware::PubSubMiddleware::getInstance();
    
    middleware.subscribe("visualizer/control", [this](const simple_middleware::Message& msg) {
        this->OnControlMessage(msg);
    });

    middleware.subscribe("visualizer/data", [this](const simple_middleware::Message& msg) {
        this->OnCarStatus(msg);
    });
    
    middleware.subscribe("perception/obstacles", [this](const simple_middleware::Message& msg) {
        this->OnPerceptionObstacles(msg);
    });

    thread_ = std::thread(&PlanningComponent::RunLoop, this);
    status_reporter_->Start();
    simple_middleware::Logger::Info("Started loop.");
}

void PlanningComponent::Stop() {
    status_reporter_->Stop();
    running_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }
}

void PlanningComponent::RunLoop() {
    auto& middleware = simple_middleware::PubSubMiddleware::getInstance();
    int seq_id = 0;

    while (running_) {
        GenerateTrajectory();

        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            if (!current_trajectory_.empty()) {
                senseauto::demo::FrameData frame;
                frame.set_frame_id(seq_id++);
                frame.set_timestamp(std::time(nullptr));
                
                // 填充轨迹数据
                for (const auto& p : current_trajectory_) {
                    auto* pt = frame.add_trajectory();
                    pt->set_x(p.x);
                    pt->set_y(p.y);
                    pt->set_speed(p.v);
                }

                std::string json_string;
                google::protobuf::util::JsonPrintOptions options;
                options.add_whitespace = false;
                google::protobuf::util::MessageToJsonString(frame, &json_string, options);

                // hack: 补上 type 字段
                if (!json_string.empty() && json_string.back() == '}') {
                    json_string.pop_back();
                    json_string += ", \"type\": \"planning_trajectory\"}";
                }
                
                // 【修复】UDP 包大小限制：MTU 通常是 1500 字节，减去 IP/UDP 头约 100 字节，实际可用约 1400 字节
                // 但为了安全，我们使用 1200 字节作为分片大小
                const size_t MAX_CHUNK_SIZE = 1200;
                const size_t topic_overhead = 50; // topic 名称和分隔符的开销
                const size_t chunk_header_size = 16; // 分片头：frame_id(4) + chunk_id(4) + total_chunks(4) + chunk_size(4)
                const size_t effective_chunk_size = MAX_CHUNK_SIZE - topic_overhead - chunk_header_size;
                
                if (json_string.size() <= effective_chunk_size) {
                    // 数据包足够小，直接发送
                    middleware.publish("planning/trajectory", json_string);
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
                        
                        middleware.publish("planning/trajectory/chunk", chunk_packet);
                        
                        // 在分片之间添加延迟，避免阻塞其他数据发送
                        if (chunk_id < total_chunks - 1) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(1)); // 1毫秒延迟
                        }
                    }
                    
                    static int chunk_count = 0;
                    if (chunk_count++ % 10 == 0) {
                        simple_middleware::Logger::Info("Planning: Published trajectory in " + std::to_string(total_chunks) + " chunks");
                    }
                }
                
                static int pub_count = 0;
                if (pub_count++ % 10 == 0 || pub_count <= 5) {
                    simple_middleware::Logger::Info("Planning: Published trajectory with " + std::to_string(current_trajectory_.size()) + " points");
                }
            } else {
                static int empty_count = 0;
                if (empty_count++ % 100 == 0) {
                    simple_middleware::Logger::Debug("Planning: Trajectory is empty, target_active=" + std::string(target_point_.active ? "true" : "false"));
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(loop_rate_ms_));
    }
}

void PlanningComponent::GenerateTrajectory() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    current_trajectory_.clear();

    if (!target_point_.active) {
        static int no_target_count = 0;
        if (no_target_count++ % 100 == 0) {
            simple_middleware::Logger::Debug("Planning: No target point active (count=" + std::to_string(no_target_count) + ")");
        }
        return;
    }
    
    // 检查是否有有效的自车位置
    if (current_pose_.x == 0.0 && current_pose_.y == 0.0) {
        static int no_pose_count = 0;
        if (no_pose_count++ % 100 == 0) {
            simple_middleware::Logger::Warn("Planning: Current pose is (0,0), may not have received car status yet (count=" + std::to_string(no_pose_count) + ")");
        }
        // 即使位置是 (0,0)，也尝试生成轨迹（可能是真的在原点）
    }

    double start_x = current_pose_.x;
    double start_y = current_pose_.y;
    double end_x = target_point_.x;
    // 使用 target_point_.y 加上我们的避障偏移
    // 假设 target_point_ 总是在 Y=0 附近
    
    // 【避障决策逻辑】(Nudge Decision)
    double target_lane_y = target_point_.y; 
    
    if (has_obstacle_) {
        // Recalculate distance to obstacle
        double dx = closest_obstacle_.position().x() - current_pose_.x;
        double dy = closest_obstacle_.position().y() - current_pose_.y;
        double dist = std::sqrt(dx*dx + dy*dy);
        
        // 如果距离小于 20m 且在正前方
        // 触发变道 (向左偏移 3.5m)
        if (dist < 20.0 && dist > 0.0) {
            simple_middleware::Logger::Info("Obstacle detected at " + std::to_string(dist) + "m. Initiating Nudge Left.");
            // 简单的状态机：如果有障碍，目标车道变为 Left (+3.5)
            // 这里我们只是在路径生成时偏移终点，这是一种"Local Planner"的做法
            target_lane_y += 3.5; 
        }
    } else {
        // 无障碍，或者已经过了障碍
        // 我们需要判断是否需要切回原车道
        // 简单逻辑：如果没有检测到前方障碍，就切回 target_point_.y
    }
    
    // 为了平滑，我们应该对 target_lane_y 进行滤波，或者让终点慢慢变
    // 但作为 demo，直接改贝塞尔曲线的 P2, P3 即可
    
    // 重新定义终点 Y
    double end_y = target_lane_y;

    // FSM Logic & Speed Planning
    double target_speed = default_cruise_speed_;
    state_ = PlanningState::CRUISE;

    // 如果变道了，就不需要减速停车了，除非左边也有车（目前感知只选最近的一个，可能有bug，但作为demo足够）
    // 如果没有足够的距离变道（比如 < 5m），还是得停车
    if (has_obstacle_) {
         double dx = closest_obstacle_.position().x() - current_pose_.x;
         double dist = std::sqrt(dx*dx); // 只看纵向距离
         if (dist < 5.0) {
             // 距离太近，来不及变道，紧急停车
             state_ = PlanningState::STOP;
             target_speed = 0.0;
             simple_middleware::Logger::Warn("EMERGENCY STOP! Dist: " + std::to_string(dist));
         }
    }

    // 贝塞尔曲线生成
    // P0: 起点 (start_x, start_y)
    // P3: 终点 (end_x, end_y) -> 这里的 end_y 可能是 +3.5 的
    
    double start_heading = current_pose_.heading;
    double dist = std::hypot(end_x - start_x, end_y - start_y);
    
    if (dist < target_reach_threshold_) {
        target_point_.active = false;
        current_trajectory_.clear();
        simple_middleware::Logger::Info("Target reached.");
        return;
    }

    // P1: 起点沿车头方向延伸 1/3 距离
    double p1_x = start_x + (dist / 3.0) * std::cos(start_heading);
    double p1_y = start_y + (dist / 3.0) * std::sin(start_heading);

    // P2: 终点沿终点切线方向反向延伸 1/3 距离 
    // 假设终点朝向为 0 (沿 X 轴)
    double end_heading = 0.0; 
    double p2_x = end_x - (dist / 3.0) * std::cos(end_heading);
    double p2_y = end_y - (dist / 3.0) * std::sin(end_heading);

    int num_points = std::min(50, std::max(10, static_cast<int>(dist * 2)));

    // 【修复2】确保规划线的第一个点就是当前自车位置
    // 先添加起点（自车当前位置）
    current_trajectory_.push_back({start_x, start_y, target_speed});
    
    simple_middleware::Logger::Info("Planning: Generating trajectory from (" + std::to_string(start_x) + ", " + std::to_string(start_y) 
        + ") to (" + std::to_string(end_x) + ", " + std::to_string(end_y) + "), num_points=" + std::to_string(num_points));
    
    // 然后生成贝塞尔曲线上的点（从 i=1 开始，因为 i=0 已经添加了起点）
    for (int i = 1; i <= num_points; ++i) {
        double t = static_cast<double>(i) / num_points;
        double u = 1 - t;
        double tt = t * t;
        double uu = u * u;
        double uuu = uu * u;
        double ttt = tt * t;

        // 三阶贝塞尔公式
        double x = uuu * start_x + 3 * uu * t * p1_x + 3 * u * tt * p2_x + ttt * end_x;
        double y = uuu * start_y + 3 * uu * t * p1_y + 3 * u * tt * p2_y + ttt * end_y;
        
        current_trajectory_.push_back({x, y, target_speed});
    }
    
    simple_middleware::Logger::Info("Planning: Generated trajectory with " + std::to_string(current_trajectory_.size()) + " points");
}

void PlanningComponent::OnControlMessage(const simple_middleware::Message& msg) {
    simple_middleware::Logger::Info("Planning: Received control message, size=" + std::to_string(msg.data.size()));
    
    std::string err;
    Json json = Json::parse(msg.data, err);

    if (!err.empty()) {
        simple_middleware::Logger::Error("Planning: JSON parse error: " + err);
        return;
    }

    std::string cmd = json["cmd"].string_value();
    simple_middleware::Logger::Info("Planning: Parsed command: " + cmd);
    
    if (cmd == "set_target") {
        double x = json["x"].number_value();
        double y = json["y"].number_value();
        
        std::lock_guard<std::mutex> lock(state_mutex_);
        target_point_.x = x;
        target_point_.y = y;
        target_point_.active = true;
        simple_middleware::Logger::Info("Planning: New target received: (" + std::to_string(x) + ", " + std::to_string(y) 
            + "), current_pose=(" + std::to_string(current_pose_.x) + ", " + std::to_string(current_pose_.y) + ")");
    } else {
        simple_middleware::Logger::Debug("Planning: Unknown command: " + cmd);
    }
}

void PlanningComponent::OnCarStatus(const simple_middleware::Message& msg) {
    // 尝试解析 Protobuf
    senseauto::demo::FrameData frame;
    if (frame.ParseFromString(msg.data)) {
        if (frame.has_car_state()) {
            std::lock_guard<std::mutex> lock(state_mutex_);
            current_pose_.x = frame.car_state().position().x();
            current_pose_.y = frame.car_state().position().y();
            current_pose_.heading = frame.car_state().heading();
            return;
        }
    }

    // 兼容旧的 JSON 格式 (如果有其他模块还在发 JSON)
    std::string err;
    Json json = Json::parse(msg.data, err);
    if (!err.empty()) return;

    if (json["carState"].is_object()) {
        Json car = json["carState"];
        if (car["position"].is_object()) {
            Json pos = car["position"];
            
            std::lock_guard<std::mutex> lock(state_mutex_);
            current_pose_.x = pos["x"].number_value();
            current_pose_.y = pos["y"].number_value();
            current_pose_.heading = car["heading"].number_value();
        }
    }
}

void PlanningComponent::OnPerceptionObstacles(const simple_middleware::Message& msg) {
    std::string err;
    Json json = Json::parse(msg.data, err);
    if (!err.empty()) return;

    if (json["type"].string_value() == "perception_obstacles" && json["obstacles"].is_array()) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        
        has_obstacle_ = false;
        double min_dist = std::numeric_limits<double>::max();
        
        // Find closest obstacle in EGO frame
        for (const auto& obs_json : json["obstacles"].array_items()) {
            double ox = obs_json["position"]["x"].number_value();
            double oy = obs_json["position"]["y"].number_value();
            
            // Calculate relative position to ego
            double dx = ox - current_pose_.x;
            double dy = oy - current_pose_.y;
            
            // Rotate to ego frame
            double heading = current_pose_.heading;
            double rx = dx * std::cos(-heading) - dy * std::sin(-heading);
            double ry = dx * std::sin(-heading) + dy * std::cos(-heading);
            
            // Filter: only consider obstacles in front (rx > 0) and within lateral range (|ry| < 2.5)
            // Using slightly wider range than car width to be safe
            if (rx > 0 && std::abs(ry) < 2.5) {
                if (rx < min_dist) {
                    min_dist = rx;
                    closest_obstacle_.mutable_position()->set_x(ox);
                    closest_obstacle_.mutable_position()->set_y(oy);
                    closest_obstacle_.set_id(obs_json["id"].int_value());
                    has_obstacle_ = true;
                }
            }
        }
    }
}
