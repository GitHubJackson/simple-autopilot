#pragma once

// 引入生成的 Protobuf 头文件
// 注意：该文件由 CMake 在构建目录下生成
#include <common_msgs/visualizer_data.pb.h> 
#include <common_msgs/sensor_data.pb.h> // New
#include <common_msgs/simple_image.hpp> // New

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
    
    // 更新外部数据
    void UpdateFromSimulator(const senseauto::demo::FrameData& sim_frame);
    void UpdateCameraImage(const std::string& ppm_data);
    void UpdateDetections(const senseauto::demo::Detection2DArray& dets);
    
    // 获取处理后带框的图像数据 (RGB Buffer)
    // 返回格式: [Width:4][Height:4][RGB...]
    std::vector<unsigned char> GetRenderedImage();
    
    std::string GetSerializedData(int frame_id);

private:
    std::mutex state_mutex_;
    std::mutex img_mutex_;
    
    senseauto::demo::FrameData frame_data_;
    
    // 图像与检测结果
    simple_image::SimpleImage current_image_;
    senseauto::demo::Detection2DArray current_detections_;
    bool has_new_image_ = false;

    double time_accumulator_ = 0.0;
};
