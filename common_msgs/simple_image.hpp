#pragma once

#include <vector>
#include <string>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <cstring>

namespace simple_image {

struct Pixel {
    unsigned char r, g, b;
};

class SimpleImage {
public:
    int width = 0;
    int height = 0;
    std::vector<Pixel> data;

    SimpleImage() = default;
    SimpleImage(int w, int h) : width(w), height(h), data(w * h) {}

    // 生成一张测试图 (彩虹渐变)
    static SimpleImage CreateTestImage(int w, int h) {
        SimpleImage img(w, h);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                img.data[y * w + x] = {
                    static_cast<unsigned char>(x * 255 / w),     // R
                    static_cast<unsigned char>(y * 255 / h),     // G
                    static_cast<unsigned char>((x+y) * 255 / (w+h)) // B
                };
            }
        }
        return img;
    }

    // 保存为 PPM 格式 (P6)
    bool SavePPM(const std::string& filepath) const {
        std::ofstream ofs(filepath, std::ios::binary);
        if (!ofs) return false;
        
        // Header
        ofs << "P6\n" << width << " " << height << "\n255\n";
        // Body
        ofs.write(reinterpret_cast<const char*>(data.data()), data.size() * 3);
        return true;
    }

    // 从 PPM 格式加载
    bool LoadPPM(const std::string& filepath) {
        std::ifstream ifs(filepath, std::ios::binary);
        if (!ifs) return false;

        std::string line;
        std::getline(ifs, line); // P6
        if (line != "P6") return false;

        // Skip comments and read dimensions
        while (ifs >> std::ws && ifs.peek() == '#') {
            std::getline(ifs, line);
        }
        
        ifs >> width >> height;
        int max_val;
        ifs >> max_val;
        ifs.get(); // Consume newline

        data.resize(width * height);
        ifs.read(reinterpret_cast<char*>(data.data()), data.size() * 3);
        return true;
    }
    
    // 直接从内存加载 (假设是 PPM 数据流)
    bool FromBuffer(const std::string& buffer) {
        // 极简解析，仅用于 demo
        if (buffer.size() < 15) return false;
        const char* ptr = buffer.data();
        if (ptr[0] != 'P' || ptr[1] != '6') return false;
        
        // 简单跳过 header (这很脆弱，但在受控环境下可用)
        // P6\n320 240\n255\n
        // 找第三个换行符
        int newlines = 0;
        size_t header_end = 0;
        for (size_t i = 0; i < 50 && i < buffer.size(); ++i) {
            if (buffer[i] == '\n' || buffer[i] == ' ') { // 宽容处理空格
                 // 实际上 PPM header 解析要复杂点，这里为了 demo 简化
                 // 我们假设标准生成的 header: "P6\nW H\n255\n"
                 if (buffer[i] == '\n') newlines++;
                 if (newlines >= 3) {
                     header_end = i + 1;
                     break;
                 }
            }
        }
        
        // 重新解析宽高
        // 这是一个 hack，生产环境请用完整的 parser
        // 为了稳健，我们先假设我们只处理自己生成的标准 PPM
        size_t p1 = buffer.find('\n');
        size_t p2 = buffer.find(' ', p1);
        size_t p3 = buffer.find('\n', p2);
        if (p1 == std::string::npos || p2 == std::string::npos || p3 == std::string::npos) return false;
        
        try {
            width = std::stoi(buffer.substr(p1 + 1, p2 - p1 - 1));
            height = std::stoi(buffer.substr(p2 + 1, p3 - p2 - 1));
        } catch (...) { return false; }
        
        // Find end of maxval
        size_t p4 = buffer.find('\n', p3 + 1);
        if (p4 == std::string::npos) return false;
        
        header_end = p4 + 1;
        
        size_t data_len = buffer.size() - header_end;
        if (data_len < static_cast<size_t>(width * height * 3)) return false;
        
        data.resize(width * height);
        std::memcpy(data.data(), buffer.data() + header_end, width * height * 3);
        return true;
    }

    // 序列化到 buffer
    std::string ToBuffer() const {
        std::string header = "P6\n" + std::to_string(width) + " " + std::to_string(height) + "\n255\n";
        std::string buf;
        buf.reserve(header.size() + data.size() * 3);
        buf += header;
        buf.append(reinterpret_cast<const char*>(data.data()), data.size() * 3);
        return buf;
    }

    // 画空心矩形
    void DrawRect(int x, int y, int w, int h, Pixel color, int thickness = 2) {
        // 确保坐标在图像范围内
        x = std::max(0, std::min(x, width - 1));
        y = std::max(0, std::min(y, height - 1));
        w = std::max(1, std::min(w, width - x));
        h = std::max(1, std::min(h, height - y));
        
        for (int t = 0; t < thickness; ++t) {
            // 确保坐标不超出边界
            int y_top = std::min(y + t, height - 1);
            int y_bottom = std::max(y + h - t - 1, 0);
            int x_left = std::min(x + t, width - 1);
            int x_right = std::min(x + w - t - 1, width - 1);
            
            if (y_top >= 0 && y_top < height) {
                DrawLine(x, y_top, x + w - 1, y_top, color);         // Top
            }
            if (y_bottom >= 0 && y_bottom < height) {
                DrawLine(x, y_bottom, x + w - 1, y_bottom, color);     // Bottom
            }
            if (x_left >= 0 && x_left < width) {
                DrawLine(x_left, y, x_left, y + h - 1, color);         // Left
            }
            if (x_right >= 0 && x_right < width) {
                DrawLine(x_right, y, x_right, y + h - 1, color);     // Right
            }
        }
    }

    // 画线 (简单 Bresenham 或直接水平/垂直优化)
    void DrawLine(int x0, int y0, int x1, int y1, Pixel color) {
        // 简单实现：仅支持水平和垂直线用于画框
        if (y0 == y1) { // Horizontal
            for (int x = std::min(x0, x1); x <= std::max(x0, x1); ++x) SetPixel(x, y0, color);
        } else if (x0 == x1) { // Vertical
            for (int y = std::min(y0, y1); y <= std::max(y0, y1); ++y) SetPixel(x0, y, color);
        } else {
             // 简单的插值
             float dx = x1 - x0;
             float dy = y1 - y0;
             float steps = std::max(std::abs(dx), std::abs(dy));
             float xInc = dx / steps;
             float yInc = dy / steps;
             float x = x0;
             float y = y0;
             for (int i = 0; i <= steps; ++i) {
                 SetPixel(static_cast<int>(x), static_cast<int>(y), color);
                 x += xInc;
                 y += yInc;
             }
        }
    }

private:
    void SetPixel(int x, int y, Pixel color) {
        if (x >= 0 && x < width && y >= 0 && y < height) {
            data[y * width + x] = color;
        }
    }
};

} // namespace simple_image

