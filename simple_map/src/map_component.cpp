#include "map_component.hpp"
#include <iostream>
#include <chrono>
#include <cmath>
#include "json11.hpp"
#include <simple_middleware/logger.hpp>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace json11;

MapComponent::MapComponent() : running_(false) {
    status_reporter_ = std::make_unique<simple_middleware::StatusReporter>("MapNode");
}

MapComponent::~MapComponent() {
    Stop();
}

void MapComponent::Start() {
    if (running_) return;
    running_ = true;
    
    // 初始化生成一次地图数据即可，假设是静态地图
    GenerateLaneData();

    thread_ = std::thread(&MapComponent::RunLoop, this);
    status_reporter_->Start();
    simple_middleware::Logger::Info("Map: Started loop.");
}

void MapComponent::Stop() {
    status_reporter_->Stop();
    running_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }
}

void MapComponent::RunLoop() {
    auto& middleware = simple_middleware::PubSubMiddleware::getInstance();
    
    while (running_) {
        // 低频发布地图数据 (1Hz)
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            
            std::vector<Json> lanes_json;
            for (const auto& lane : map_data_.lanes()) {
                // 中心线
                std::vector<Json> center_line_json;
                for (const auto& p : lane.center_line()) {
                    center_line_json.push_back(Json::object {
                        { "x", p.x() },
                        { "y", p.y() },
                        { "z", p.z() }
                    });
                }
                
                // 左边界
                std::vector<Json> left_boundary_json;
                for (const auto& p : lane.left_boundary()) {
                    left_boundary_json.push_back(Json::object {
                        { "x", p.x() },
                        { "y", p.y() },
                        { "z", p.z() }
                    });
                }
                
                // 右边界
                std::vector<Json> right_boundary_json;
                for (const auto& p : lane.right_boundary()) {
                    right_boundary_json.push_back(Json::object {
                        { "x", p.x() },
                        { "y", p.y() },
                        { "z", p.z() }
                    });
                }
                
                lanes_json.push_back(Json::object {
                    { "id", (int)lane.id() },
                    { "center_line", Json(center_line_json) },
                    { "left_boundary", Json(left_boundary_json) },
                    { "right_boundary", Json(right_boundary_json) },
                    { "width", lane.width() },
                    { "left_lane_id", lane.left_lane_id() },
                    { "right_lane_id", lane.right_lane_id() },
                    { "type", lane.type() }
                });
            }

            Json map_json = Json::object {
                { "lanes", Json(lanes_json) },
                { "type", "map_data" }
            };

            std::string json_string = map_json.dump();
            
            // 【修复】UDP 包大小限制：MTU 通常是 1500 字节，减去 IP/UDP 头约 100 字节，实际可用约 1400 字节
            // 但为了安全，我们使用 1200 字节作为分片大小
            const size_t MAX_CHUNK_SIZE = 1200;
            const size_t topic_overhead = 50; // topic 名称和分隔符的开销
            const size_t chunk_header_size = 16; // 分片头：frame_id(4) + chunk_id(4) + total_chunks(4) + chunk_size(4)
            const size_t effective_chunk_size = MAX_CHUNK_SIZE - topic_overhead - chunk_header_size;
            
            bool published = false;
            if (json_string.size() <= effective_chunk_size) {
                // 数据包足够小，直接发送
                published = middleware.publish("visualizer/map", json_string);
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
                    
                    bool chunk_published = middleware.publish("visualizer/map/chunk", chunk_packet);
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
                    simple_middleware::Logger::Info("Map: Published map data in " + std::to_string(total_chunks) 
                        + " chunks, total_size=" + std::to_string(json_string.size()) + " bytes");
                }
            }
            
            static int pub_count = 0;
            if (pub_count++ % 10 == 0 || pub_count == 1) {
                simple_middleware::Logger::Info("Map: Published map data: " + std::to_string(map_data_.lanes_size()) 
                    + " lanes, size=" + std::to_string(json_string.size()) + " bytes, result=" 
                    + (published ? "success" : "failed"));
                // 打印JSON的前100个字符用于调试
                if (pub_count == 1 && json_string.size() > 0) {
                    std::string preview = json_string.substr(0, std::min(100UL, json_string.size()));
                    simple_middleware::Logger::Debug("Map: JSON preview: " + preview + "...");
                }
            }
        }
        
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

void MapComponent::GenerateLaneData() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    map_data_.clear_lanes();
    
    const double LANE_WIDTH = 3.5;  // 标准车道宽度 3.5米
    map_data_.set_default_lane_width(LANE_WIDTH);
    
    // ========== 场景设计：双向两车道道路 ==========
    // 场景描述：
    // - 从 x=-50 到 x=150 的完整道路
    // - 包含直道段和弯道段
    // - 右侧车道（Lane 1）：主车道，y=0 附近
    // - 左侧车道（Lane 2）：超车道，y=3.5 附近
    
    // 车道 1: 右侧主车道（直道 + 轻微弯道）
    auto* lane1 = map_data_.add_lanes();
    lane1->set_id(1);
    lane1->set_width(LANE_WIDTH);
    lane1->set_left_lane_id(2);   // 左侧是车道2
    lane1->set_right_lane_id(-1); // 右侧无车道（道路边界）
    lane1->set_type("straight");
    
    // 生成中心线点
    for (int x = -50; x <= 150; x += 2) {
        double y = 0.0;
        // 在 x=50 到 x=100 之间添加轻微弯道
        if (x >= 50 && x <= 100) {
            double t = (x - 50) / 50.0;  // 0 到 1
            y = 2.0 * std::sin(t * M_PI);  // 轻微S型弯道
        }
        
        auto* center = lane1->add_center_line();
        center->set_x(x);
        center->set_y(y);
        center->set_z(0);
        
        // 计算左边界（向左偏移 LANE_WIDTH/2）
        double heading = 0.0;
        if (x > 50 && x < 100) {
            // 计算弯道处的切线方向
            double dt = 0.01;
            double y_next = 2.0 * std::sin(((x + dt * 50) - 50) / 50.0 * M_PI);
            heading = std::atan2(y_next - y, dt * 50);
        }
        
        auto* left = lane1->add_left_boundary();
        left->set_x(x - (LANE_WIDTH / 2.0) * std::sin(heading));
        left->set_y(y + (LANE_WIDTH / 2.0) * std::cos(heading));
        left->set_z(0);
        
        // 计算右边界（向右偏移 LANE_WIDTH/2）
        auto* right = lane1->add_right_boundary();
        right->set_x(x + (LANE_WIDTH / 2.0) * std::sin(heading));
        right->set_y(y - (LANE_WIDTH / 2.0) * std::cos(heading));
        right->set_z(0);
    }

    // 车道 2: 左侧超车道（与车道1平行，偏移3.5米）
    auto* lane2 = map_data_.add_lanes();
    lane2->set_id(2);
    lane2->set_width(LANE_WIDTH);
    lane2->set_left_lane_id(-1);  // 左侧无车道（道路边界）
    lane2->set_right_lane_id(1);  // 右侧是车道1
    lane2->set_type("straight");
    
    for (int x = -50; x <= 150; x += 2) {
        double y = 0.0;
        // 与车道1相同的弯道形状
        if (x >= 50 && x <= 100) {
            double t = (x - 50) / 50.0;
            y = 2.0 * std::sin(t * M_PI);
        }
        // 向左偏移 3.5米（一个车道宽度）
        y += LANE_WIDTH;
        
        auto* center = lane2->add_center_line();
        center->set_x(x);
        center->set_y(y);
        center->set_z(0);
        
        // 计算左边界
        double heading = 0.0;
        if (x > 50 && x < 100) {
            double dt = 0.01;
            double y_next = 2.0 * std::sin(((x + dt * 50) - 50) / 50.0 * M_PI) + LANE_WIDTH;
            heading = std::atan2(y_next - y, dt * 50);
        }
        
        auto* left = lane2->add_left_boundary();
        left->set_x(x - (LANE_WIDTH / 2.0) * std::sin(heading));
        left->set_y(y + (LANE_WIDTH / 2.0) * std::cos(heading));
        left->set_z(0);
        
        // 计算右边界
        auto* right = lane2->add_right_boundary();
        right->set_x(x + (LANE_WIDTH / 2.0) * std::sin(heading));
        right->set_y(y - (LANE_WIDTH / 2.0) * std::cos(heading));
        right->set_z(0);
    }
    
    simple_middleware::Logger::Info("Map: Generated " + std::to_string(map_data_.lanes_size()) + " lanes with boundaries.");
}
