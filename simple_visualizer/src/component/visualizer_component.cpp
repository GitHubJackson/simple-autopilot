#include "visualizer_component.hpp"
// 如果安装了完整的 protobuf 库，可以使用 json_util
#include <google/protobuf/util/json_util.h> 

VisualizerComponent::VisualizerComponent() {
    Reset();
}

void VisualizerComponent::Reset() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    
    // Protobuf 清空对象
    frame_data_.Clear();
    
    // 初始化车辆
    auto* car = frame_data_.mutable_car_state();
    car->mutable_position()->set_x(0.0);
    car->mutable_position()->set_y(0.0);
    car->set_heading(0.0);
    car->set_speed(0.0);
    car->set_steering_angle(0.0);

    // 初始化障碍物
    auto* obs1 = frame_data_.add_obstacles();
    obs1->set_id(1);
    obs1->mutable_position()->set_x(20.0);
    obs1->mutable_position()->set_y(10.0);
    obs1->set_type("cone");

    auto* obs2 = frame_data_.add_obstacles();
    obs2->set_id(2);
    obs2->mutable_position()->set_x(30.0);
    obs2->mutable_position()->set_y(-5.0);
    obs2->set_type("pedestrian");
    
    auto* obs3 = frame_data_.add_obstacles();
    obs3->set_id(3);
    obs3->mutable_position()->set_x(50.0);
    obs3->mutable_position()->set_y(20.0);
    obs3->set_type("car");

    time_accumulator_ = 0.0;
}

void VisualizerComponent::SetSpeed(double speed) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    auto* car = frame_data_.mutable_car_state();
    car->set_speed(std::max(0.0, std::min(speed, 30.0)));
    std::cout << "[Biz] Set speed to: " << car->speed() << std::endl;
}

void VisualizerComponent::SetSteering(double angle) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    auto* car = frame_data_.mutable_car_state();
    car->set_steering_angle(std::max(-0.5, std::min(angle, 0.5)));
    std::cout << "[Biz] Set steering to: " << car->steering_angle() << std::endl;
}

void VisualizerComponent::Update(double dt) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    
    auto* car = frame_data_.mutable_car_state();
    double speed = car->speed();
    double heading = car->heading();
    double steering = car->steering_angle();
    double L = 2.8;

    // 更新位置
    double dx = speed * std::cos(heading) * dt;
    double dy = speed * std::sin(heading) * dt;
    double dheading = (speed / L) * std::tan(steering) * dt;

    car->mutable_position()->set_x(car->position().x() + dx);
    car->mutable_position()->set_y(car->position().y() + dy);
    car->set_heading(heading + dheading);

    // 更新动态障碍物 (id=2)
    for (int i = 0; i < frame_data_.obstacles_size(); ++i) {
        auto* obs = frame_data_.mutable_obstacles(i);
        if (obs->id() == 2) {
            obs->mutable_position()->set_y(-5.0 + 2.0 * std::sin(time_accumulator_));
        }
    }
    time_accumulator_ += dt;
}

std::string VisualizerComponent::GetSerializedData(int frame_id) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    
    // 设置帧头信息
    frame_data_.set_frame_id(frame_id);
    frame_data_.set_timestamp(std::time(nullptr));
    
    // 方案 A: 使用 Protobuf 官方的 JSON 转换工具 (最推荐)
    std::string json_string;
    google::protobuf::util::JsonPrintOptions options;
    options.add_whitespace = false; // 压缩 JSON
    // options.always_print_primitive_fields = true; // 即使是默认值(0)也输出
    auto status = google::protobuf::util::MessageToJsonString(frame_data_, &json_string, options);
    
    if (status.ok()) {
        // 由于 Protobuf 生成的 JSON 字段名是 lowerCamelCase 或 snake_case，
        // 而前端可能期待特定的字段名。为了兼容之前的 Demo 前端，我们需要确认字段名是否一致。
        // visualizer_data.proto 里定义的是 snake_case (e.g., car_state)，Protobuf 默认转 JSON 也是 snake_case。
        // 但我们之前手写的 JSON 用的是 "car_state"。
        // 唯一的区别是：之前我们把 type: "frame_data" 放在最外层。
        // Protobuf 转出来的 JSON 纯粹是 frame_data 对象的内容。
        // 前端如果依靠 `type` 字段来判断消息类型，我们需要手动补上，或者改前端。
        
        // 简单修补：手动包一层，或者让前端适应新的格式
        // 这里为了兼容性，我们手动拼接 type 字段，这虽然有点 hacky，但最稳。
        // JSON: { ...ProtobufJSON... } -> 替换最后的 '}' 为 ', "type": "frame_data"}'
        if (!json_string.empty() && json_string.back() == '}') {
             json_string.pop_back(); // 去掉 '}'
             json_string += ", \"type\": \"frame_data\"}";
        }
        return json_string;
    } else {
        std::cerr << "Proto to JSON failed: " << status.ToString() << std::endl;
        return "{}";
    }
}
