#include "map_component.hpp"
#include <iostream>
#include <chrono>
#include <cmath>
#include "json11.hpp"

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
    std::cout << "[Map] Started loop." << std::endl;
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
                std::vector<Json> center_line_json;
                for (const auto& p : lane.center_line()) {
                    center_line_json.push_back(Json::object {
                        { "x", p.x() },
                        { "y", p.y() },
                        { "z", p.z() }
                    });
                }
                
                lanes_json.push_back(Json::object {
                    { "id", (int)lane.id() },
                    { "center_line", Json(center_line_json) }
                });
            }

            Json map_json = Json::object {
                { "lanes", Json(lanes_json) },
                { "type", "map_data" }
            };

            std::string json_string = map_json.dump();
            
            middleware.publish("visualizer/map", json_string);
        }
        
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

void MapComponent::GenerateLaneData() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    map_data_.clear_lanes();

    // 车道 1: 直线 (y=0, x从-50到100)
    auto* lane1 = map_data_.add_lanes();
    lane1->set_id(1);
    for (int x = -50; x <= 100; x += 2) {
        auto* p = lane1->add_center_line();
        p->set_x(x);
        p->set_y(0);
        p->set_z(0);
    }

    // 车道 2: 正弦曲线 (y = 10 * sin(x/20) + 15)
    auto* lane2 = map_data_.add_lanes();
    lane2->set_id(2);
    for (int x = -50; x <= 100; x += 2) {
        auto* p = lane2->add_center_line();
        p->set_x(x);
        p->set_y(10.0 * std::sin(x / 20.0) + 15.0);
        p->set_z(0);
    }
    
    std::cout << "[Map] Generated " << map_data_.lanes_size() << " lanes." << std::endl;
}
