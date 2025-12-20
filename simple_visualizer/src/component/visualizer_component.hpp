#pragma once

// 引入生成的 Protobuf 头文件
// 注意：该文件由 CMake 在构建目录下生成
#include <common_msgs/visualizer_data.pb.h> 

#include <mutex>
#include <vector>
#include <string>
#include <ctime>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <iostream>

class VisualizerComponent {
public:
    VisualizerComponent();

    void Reset();
    void SetSpeed(double speed);
    void SetSteering(double angle);
    void Update(double dt);
    
    // 返回 Protobuf 序列化后的 JSON 字符串
    // 注意：Protobuf 原生是二进制，转 JSON 需要辅助库，或者我们先转成 string 形式
    // 这里为了兼容前端 WebSocket，我们先用 Protobuf 的 DebugString() 或者手动转 JSON
    std::string GetSerializedData(int frame_id);

private:
    std::mutex state_mutex_;
    
    // 使用 Protobuf 生成的类
    senseauto::demo::FrameData frame_data_;
    
    // 辅助变量用于动画
    double time_accumulator_ = 0.0;
};
