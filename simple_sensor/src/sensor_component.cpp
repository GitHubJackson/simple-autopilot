#include "sensor_component.hpp"
#include <iostream>
#include <cmath>
#include <chrono>
#include <thread> // for std::this_thread::sleep_for
#include <cstdlib> // for rand()
#include <fstream>
#include <vector>
#include <cstring> // for memcpy
#include <arpa/inet.h> // for htonl
#include <simple_middleware/logger.hpp> // Add logger include
#include <common_msgs/simple_image.hpp> // For SimpleImage

using namespace simple_middleware;

SensorComponent::SensorComponent() 
    : running_(false), noise_distribution_(0.0f, 0.2f) { // 噪声标准差 0.2m
    status_reporter_ = std::make_unique<StatusReporter>("SensorNode");
}

SensorComponent::~SensorComponent() {
    Stop();
}

void SensorComponent::Start() {
    if (running_) return;
    running_ = true;

    // 订阅真值数据
    auto& middleware = PubSubMiddleware::getInstance();
    middleware.subscribe("visualizer/data", [this](const Message& msg) {
        this->OnVisualizerData(msg);
    });

    thread_ = std::thread(&SensorComponent::RunLoop, this);
    status_reporter_->Start();
    
    // 加载静态测试图片
    // 优先使用 src/assets/test.png，如果不存在则尝试其他路径
    std::ifstream file;
    bool image_loaded = false;
    
    // 尝试多个路径
    std::vector<std::string> paths = {
        "src/assets/test.png",
        "../src/assets/test.png",
    };
    
    for (const auto& path : paths) {
        file.open(path, std::ios::binary | std::ios::ate);
        if (file) {
             std::streamsize size = file.tellg();
             file.seekg(0, std::ios::beg);
             raw_image_buffer_.resize(size);
             if (file.read(raw_image_buffer_.data(), size)) {
                simple_middleware::Logger::Info("Loaded image from: " + path + " (" + std::to_string(size) + " bytes)");
                image_loaded = true;
                break;
            }
        }
        file.close();
    }
    
            // 【简化】不再解析图片文件，直接生成白色背景图像
            // 白色背景上红色检测框更明显
            simple_middleware::Logger::Info("Generating white background image (160x120)");
            raw_image_buffer_.clear(); // 清空，不再使用文件数据

    simple_middleware::Logger::Info("Started camera simulation.");
}

void SensorComponent::Stop() {
    status_reporter_->Stop();
    running_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }
}

void SensorComponent::OnVisualizerData(const Message& msg) {
    std::lock_guard<std::mutex> lock(data_mutex_);
    if (ground_truth_.ParseFromString(msg.data)) {
        has_ground_truth_ = true;
        static int recv_count = 0;
        if (recv_count++ % 30 == 0) { // 每 30 次（1秒）输出一次
            simple_middleware::Logger::Debug("Sensor: Received visualizer/data, has_car_state=" 
                + std::string(ground_truth_.has_car_state() ? "true" : "false"));
        }
    } else {
        static int fail_count = 0;
        if (fail_count++ % 30 == 0) {
            simple_middleware::Logger::Warn("Sensor: Failed to parse visualizer/data");
        }
    }
}

void SensorComponent::RunLoop() {
    auto& middleware = PubSubMiddleware::getInstance();
    
    while (running_) {
        auto start_time = std::chrono::steady_clock::now();

        // 1. 获取最新真值
        senseauto::demo::FrameData current_gt;
        bool has_data = false;
        {
            std::lock_guard<std::mutex> lock(data_mutex_);
            if (has_ground_truth_) {
                current_gt = ground_truth_;
                has_data = true;
            }
        }

        if (!has_data) {
            static int no_data_count = 0;
            if (no_data_count++ % 50 == 0) { // 每 50 次（约1.6秒）输出一次
                simple_middleware::Logger::Debug("Sensor: RunLoop - no ground truth data yet");
            }
        } else if (!current_gt.has_car_state()) {
            static int no_car_state_count = 0;
            if (no_car_state_count++ % 50 == 0) {
                simple_middleware::Logger::Debug("Sensor: RunLoop - has data but no car_state");
            }
        }

        if (has_data && current_gt.has_car_state()) {
            senseauto::demo::CameraFrame camera_frame;
            camera_frame.set_timestamp(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
            
            // 【架构调整】Sensor 只负责发送原始图像数据，不包含 objects
            // Objects 应该由 Perception 模块通过检测算法从图像中识别出来
            // 这里我们不再填充 objects 数据

            // 【模拟生成图像数据】
            // 如果成功加载了图片，则发送；否则发送原来的噪点图
            if (!raw_image_buffer_.empty()) {
                // 从 PPM header 中解析实际尺寸（简单解析）
                // PPM 格式: "P6\nWIDTH HEIGHT\n255\n[DATA]"
                int w = 160, h = 120; // 默认值
                size_t header_end = 0;
                
                std::string header = raw_image_buffer_.substr(0, std::min(50UL, raw_image_buffer_.size()));
                size_t first_nl = header.find('\n');
                if (first_nl != std::string::npos) {
                    size_t space_pos = header.find(' ', first_nl);
                    size_t second_nl = header.find('\n', space_pos);
                    if (space_pos != std::string::npos && second_nl != std::string::npos) {
                        try {
                            w = std::stoi(header.substr(first_nl + 1, space_pos - first_nl - 1));
                            h = std::stoi(header.substr(space_pos + 1, second_nl - space_pos - 1));
                        } catch (...) {
                            // 解析失败，使用默认值
                        }
                    }
                }
                
                // 找到 header 结束位置（第三个换行符之后）
                size_t p1 = raw_image_buffer_.find('\n');
                size_t p2 = raw_image_buffer_.find(' ', p1);
                size_t p3 = raw_image_buffer_.find('\n', p2);
                if (p3 != std::string::npos) {
                    size_t p4 = raw_image_buffer_.find('\n', p3 + 1);
                    if (p4 != std::string::npos) {
                        header_end = p4 + 1;
                    }
                }
                
                // 【修改】直接生成白色背景图像，不解析 PPM 文件
                // 这样更简单，而且白色背景上红色检测框更明显
                int width = 160;
                int height = 120;
                std::string white_image_data;
                white_image_data.resize(width * height * 3); // RGB 数据
                // 填充白色像素 (255, 255, 255)
                std::memset(white_image_data.data(), 255, white_image_data.size());
                camera_frame.set_image_width(width);
                camera_frame.set_image_height(height);
                camera_frame.set_image_format("ppm"); // 标记为 ppm 格式，但实际只包含 RGB 数据
                camera_frame.set_raw_image(white_image_data);
                
                static int log_counter = 0;
                if (log_counter++ % 10 == 0) {
                    simple_middleware::Logger::Debug("Publishing white background image: " 
                        + std::to_string(width) + "x" + std::to_string(height)
                        + ", RGB size=" + std::to_string(white_image_data.size()) + " bytes");
                }
            } else {
                // 生成白色背景图像 (fallback)
                int width = 160;
                int height = 120;
                std::string white_image_data;
                white_image_data.resize(width * height * 3); // RGB 数据
                // 填充白色像素 (255, 255, 255)
                std::memset(white_image_data.data(), 255, white_image_data.size());
                camera_frame.set_image_width(width);
                camera_frame.set_image_height(height);
                camera_frame.set_image_format("ppm"); // 标记为 ppm 格式，但实际只包含 RGB 数据
                camera_frame.set_raw_image(white_image_data);
            }

            // 发布传感器数据（如果数据包太大，需要分片发送）
            std::string serialized_data;
            if (camera_frame.SerializeToString(&serialized_data)) {
                // UDP 数据包大小限制：MTU 通常是 1500 字节，减去 IP/UDP 头约 100 字节，实际可用约 1400 字节
                // 但为了安全，我们使用 1200 字节作为分片大小
                const size_t MAX_CHUNK_SIZE = 1200;
                const size_t topic_overhead = 50; // topic 名称和分隔符的开销
                const size_t chunk_header_size = 16; // 分片头：frame_id(4) + chunk_id(4) + total_chunks(4) + chunk_size(4)
                const size_t effective_chunk_size = MAX_CHUNK_SIZE - topic_overhead - chunk_header_size;
                
                if (serialized_data.size() <= effective_chunk_size) {
                    // 数据包足够小，直接发送
                    middleware.publish("sensor/camera/front", serialized_data);
                    
                    static int log_counter = 0;
                    if (log_counter++ % 10 == 0) {
                        simple_middleware::Logger::Debug("Published frame. Image size: " + std::to_string(camera_frame.raw_image().size()));
                    }
                } else {
                    // 数据包太大，需要分片发送
                    // 使用二进制分片协议：frame_id(4) + chunk_id(4) + total_chunks(4) + chunk_size(4) + chunk_data
                    size_t total_chunks = (serialized_data.size() + effective_chunk_size - 1) / effective_chunk_size;
                    static uint32_t frame_id_counter = 0;
                    uint32_t frame_id = ++frame_id_counter;
                    
                    // 【优化】先发送元数据，再发送分片，避免长时间阻塞
                    senseauto::demo::CameraFrame metadata_frame;
                    metadata_frame.set_timestamp(camera_frame.timestamp());
                    metadata_frame.set_image_width(camera_frame.image_width());
                    metadata_frame.set_image_height(camera_frame.image_height());
                    metadata_frame.set_image_format(camera_frame.image_format());
                    
                    std::string metadata_data;
                    if (metadata_frame.SerializeToString(&metadata_data)) {
                        middleware.publish("sensor/camera/front", metadata_data);
                    }
                    
                    // 然后发送分片，分片之间添加延迟，避免阻塞其他数据
                    for (size_t chunk_id = 0; chunk_id < total_chunks; ++chunk_id) {
                        size_t chunk_start = chunk_id * effective_chunk_size;
                        size_t chunk_size = std::min(effective_chunk_size, serialized_data.size() - chunk_start);
                        
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
                        std::memcpy(&chunk_packet[16], &serialized_data[chunk_start], chunk_size);
                        
                        middleware.publish("sensor/camera/front/chunk", chunk_packet);
                        
                        // 在分片之间添加延迟，避免阻塞其他数据发送（特别是 Simulator 的自车数据）
                        if (chunk_id < total_chunks - 1) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(2)); // 2毫秒延迟，让其他数据有机会发送
                        }
                    }
                    
                    static int log_counter = 0;
                    if (log_counter++ % 10 == 0) {
                        simple_middleware::Logger::Debug("Published frame in " + std::to_string(total_chunks) 
                            + " chunks (image) + 1 metadata frame. "
                            + "Total size: " + std::to_string(serialized_data.size())
                            + ", Metadata size: " + std::to_string(metadata_data.size()));
                    }
                }
            }
        }

        // 1Hz 频率控制（每秒1帧）
        std::this_thread::sleep_until(start_time + std::chrono::milliseconds(1000));
    }
}

