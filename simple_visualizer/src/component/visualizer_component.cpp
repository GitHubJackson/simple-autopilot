#include "visualizer_component.hpp"
#include <json11.hpp>
#include <iostream>
#include <chrono>
#include <ctime>
#include <cmath>
#include <cstring> // for memcpy
#include <simple_middleware/logger.hpp>

using namespace json11;

VisualizerComponent::VisualizerComponent() {
    Reset();
}

void VisualizerComponent::Reset() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    
    frame_data_.Clear();
    
    // 初始化一个默认状态
    auto* car = frame_data_.mutable_car_state();
    car->mutable_position()->set_x(0.0);
    car->mutable_position()->set_y(0.0);
    car->set_heading(0.0);
    car->set_speed(0.0);
    car->set_steering_angle(0.0);

    time_accumulator_ = 0.0;
}

// Visualizer 不再负责设置 Speed/Steering，它只是被动接收
// 但为了兼容旧接口，先留空或者保留打印
void VisualizerComponent::SetSpeed(double speed) {
    // Deprecated
}

void VisualizerComponent::SetSteering(double angle) {
    // Deprecated
}

// 核心更新逻辑：从 Simulator 同步状态
void VisualizerComponent::UpdateFromSimulator(const senseauto::demo::FrameData& sim_frame) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    // 直接覆盖本地状态
    frame_data_ = sim_frame;
}

void VisualizerComponent::Update(double dt) {
    // Visualizer 不再做物理积分
    // 这里的 Update 可以用来做一些纯视觉效果的动画（比如车轮转动动画），如果需要的话
    time_accumulator_ += dt;
}

bool VisualizerComponent::UpdateCameraImage(const std::string& ppm_data) {
    std::lock_guard<std::mutex> lock(img_mutex_);
    if (current_image_.FromBuffer(ppm_data)) {
        has_new_image_ = true;
        return true;
    } else {
        // 调试：检查数据格式
        if (ppm_data.size() > 0) {
            std::cerr << "[VisualizerComponent] Failed to parse PPM image. "
                      << "Size: " << ppm_data.size() 
                      << ", First bytes: " << (int)(unsigned char)ppm_data[0] 
                      << " " << (int)(unsigned char)ppm_data[1] 
                      << " " << (int)(unsigned char)ppm_data[2] << std::endl;
        }
        return false;
    }
}

bool VisualizerComponent::UpdateCameraImageRGB(const std::string& rgb_data, int width, int height) {
    std::lock_guard<std::mutex> lock(img_mutex_);
    
    // 检查数据大小是否匹配
    size_t expected_size = width * height * 3;
    if (rgb_data.size() != expected_size) {
        std::cerr << "[VisualizerComponent] RGB data size mismatch. Expected: " 
                  << expected_size << ", Got: " << rgb_data.size() << std::endl;
        return false;
    }
    
    // 直接设置图像数据（不通过 PPM 解析）
    current_image_.width = width;
    current_image_.height = height;
    current_image_.data.resize(width * height);
    
    // 将 RGB 数据复制到 SimpleImage
    const unsigned char* rgb_ptr = reinterpret_cast<const unsigned char*>(rgb_data.data());
    for (int i = 0; i < width * height; ++i) {
        current_image_.data[i] = {
            rgb_ptr[i * 3 + 0],  // R
            rgb_ptr[i * 3 + 1],  // G
            rgb_ptr[i * 3 + 2]   // B
        };
    }
    
    has_new_image_ = true;
    
    static int update_count = 0;
    update_count++;
    if (update_count % 3 == 0 || update_count <= 5) {
        simple_middleware::Logger::Debug("VisualizerComponent: UpdateCameraImageRGB: image=" 
            + std::to_string(width) + "x" + std::to_string(height) 
            + ", detections=" + std::to_string(current_detections_.boxes_size()));
    }
    
    return true;
}

void VisualizerComponent::UpdateDetections(const senseauto::demo::Detection2DArray& dets) {
    std::lock_guard<std::mutex> lock(img_mutex_);
    current_detections_ = dets;
    
    static int update_count = 0;
    update_count++;
    // 总是打印，因为这是关键数据流
    simple_middleware::Logger::Info("VisualizerComponent: UpdateDetections: received " 
        + std::to_string(dets.boxes_size()) + " boxes, current_image: " 
        + std::to_string(current_image_.width) + "x" + std::to_string(current_image_.height));
    for (int i = 0; i < dets.boxes_size(); ++i) {
        const auto& box = dets.boxes(i);
        simple_middleware::Logger::Info("  Box " + std::to_string(i) + ": x=" + std::to_string(box.x()) 
            + ", y=" + std::to_string(box.y()) + ", w=" + std::to_string(box.width()) 
            + ", h=" + std::to_string(box.height()) + ", label=" + box.label());
    }
}

std::vector<unsigned char> VisualizerComponent::GetRenderedImage() {
    std::lock_guard<std::mutex> lock(img_mutex_);
    
    // 只要有图像数据就渲染（不依赖 has_new_image_ 标志）
    // 这样即使只有检测数据更新，也能重新绘制
    if (current_image_.width == 0 || current_image_.height == 0) {
        static int empty_count = 0;
        if (empty_count++ % 10 == 0 || empty_count <= 3) {
            simple_middleware::Logger::Debug("VisualizerComponent: GetRenderedImage: no image data, width=" 
                + std::to_string(current_image_.width) + ", height=" + std::to_string(current_image_.height));
        }
        return {};
    }

    // 创建图像副本用于绘制（避免修改原始图像，每次都是干净的图像）
    simple_image::SimpleImage render_image = current_image_;

    // 绘制 2D 检测框
    int detection_count = current_detections_.boxes_size();
    
    static int render_count = 0;
    render_count++;
    
    if (detection_count > 0) {
        // 总是打印，因为这是关键功能
        simple_middleware::Logger::Info("VisualizerComponent: GetRenderedImage: Drawing " 
            + std::to_string(detection_count) + " detection boxes on image " 
            + std::to_string(render_image.width) + "x" + std::to_string(render_image.height));
        
        for (int i = 0; i < detection_count; ++i) {
            const auto& box = current_detections_.boxes(i);
            simple_image::Pixel red = {255, 0, 0};
            
            // 确保坐标在图像范围内
            int x = std::max(0, std::min(box.x(), render_image.width - 1));
            int y = std::max(0, std::min(box.y(), render_image.height - 1));
            int w = std::max(1, std::min(box.width(), render_image.width - x));
            int h = std::max(1, std::min(box.height(), render_image.height - y));
            
            // 确保矩形不会超出图像边界
            if (x + w > render_image.width) w = render_image.width - x;
            if (y + h > render_image.height) h = render_image.height - y;
            
            // 总是打印前几次，确保能看到绘制信息
            if (render_count <= 10) {
                simple_middleware::Logger::Info("VisualizerComponent: Drawing box " + std::to_string(i) 
                    + ": original(x=" + std::to_string(box.x()) + ", y=" + std::to_string(box.y()) 
                    + ", w=" + std::to_string(box.width()) + ", h=" + std::to_string(box.height()) + ")"
                    + " -> clamped(x=" + std::to_string(x) + ", y=" + std::to_string(y) 
                    + ", w=" + std::to_string(w) + ", h=" + std::to_string(h) + ")"
                    + ", image_size=" + std::to_string(render_image.width) + "x" + std::to_string(render_image.height)
                    + ", label=" + box.label());
            }
            
            // 绘制红色检测框（使用更粗的线宽，确保可见）
            render_image.DrawRect(x, y, w, h, red, 5); // 线宽 5，更明显
            
            // 验证绘制是否成功（检查绘制后的像素是否变红）
            if (render_count <= 5) {
                // 检查左上角、右上角像素是否被绘制（应该是红色）
                if (x < render_image.width && y < render_image.height) {
                    const auto& pixel_tl = render_image.data[y * render_image.width + x];
                    simple_middleware::Logger::Info("VisualizerComponent: After DrawRect, top-left pixel at (" 
                        + std::to_string(x) + "," + std::to_string(y) + ") = RGB(" 
                        + std::to_string((int)pixel_tl.r) + "," + std::to_string((int)pixel_tl.g) + "," 
                        + std::to_string((int)pixel_tl.b) + ")");
                }
                if (x + w - 1 < render_image.width && y < render_image.height) {
                    const auto& pixel_tr = render_image.data[y * render_image.width + (x + w - 1)];
                    simple_middleware::Logger::Info("VisualizerComponent: After DrawRect, top-right pixel at (" 
                        + std::to_string(x + w - 1) + "," + std::to_string(y) + ") = RGB(" 
                        + std::to_string((int)pixel_tr.r) + "," + std::to_string((int)pixel_tr.g) + "," 
                        + std::to_string((int)pixel_tr.b) + ")");
                }
            }
        }
    } else {
        // 总是打印，因为这是问题所在
        if (render_count <= 10 || render_count % 5 == 0) {
            simple_middleware::Logger::Warn("VisualizerComponent: GetRenderedImage: No detection boxes to draw (render_count=" 
                + std::to_string(render_count) + ", image=" + std::to_string(render_image.width) 
                + "x" + std::to_string(render_image.height) + ", current_detections_.boxes_size()=" 
                + std::to_string(current_detections_.boxes_size()) + ")");
        }
    }
    
    // 返回 Raw RGB Buffer (去除 PPM Header)
    // 格式: [Width:4][Height:4][RGB Data]
    
    int w = render_image.width;
    int h = render_image.height;
    size_t size = w * h * 3;
    
    std::vector<unsigned char> result;
    result.resize(8 + size); // 4 byte width + 4 byte height + data
    
    // Write Width (Big Endian)
    result[0] = (w >> 24) & 0xFF;
    result[1] = (w >> 16) & 0xFF;
    result[2] = (w >> 8) & 0xFF;
    result[3] = w & 0xFF;
    
    // Write Height (Big Endian)
    result[4] = (h >> 24) & 0xFF;
    result[5] = (h >> 16) & 0xFF;
    result[6] = (h >> 8) & 0xFF;
    result[7] = h & 0xFF;
    
    // 将渲染后的图像数据复制到结果缓冲区
    for (size_t i = 0; i < render_image.data.size(); ++i) {
        size_t offset = 8 + i * 3;
        result[offset + 0] = render_image.data[i].r;
        result[offset + 1] = render_image.data[i].g;
        result[offset + 2] = render_image.data[i].b;
    }
    
    return result;
}

// 序列化给前端 WebSocket
std::string VisualizerComponent::GetSerializedData(int frame_id) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    
    // 1. 处理障碍物列表
    Json::array obstacles_json;
    for (int i = 0; i < frame_data_.obstacles_size(); ++i) {
        const auto& obs = frame_data_.obstacles(i);
        obstacles_json.push_back(Json::object {
            {"id", obs.id()},
            {"type", obs.type()},
            {"position", Json::object {
                {"x", obs.position().x()},
                {"y", obs.position().y()}
            }},
            // 将新增字段也传给前端（前端目前可能还没用，但以后会有用）
            {"length", obs.length()},
            {"width", obs.width()},
            {"heading", obs.heading()}
        });
    }

    // 2. 构造最终的 JSON 对象
    Json final_json = Json::object {
        {"type", "frame_data"},
        {"frame_id", frame_id},
        {"timestamp", (int)std::time(nullptr)},
        {"car_state", Json::object {
            {"speed", frame_data_.car_state().speed()},
            {"heading", frame_data_.car_state().heading()},
            {"steering_angle", frame_data_.car_state().steering_angle()},
            {"position", Json::object {
                {"x", frame_data_.car_state().position().x()},
                {"y", frame_data_.car_state().position().y()}
            }}
        }},
        {"obstacles", obstacles_json}
    };

    return final_json.dump();
}
