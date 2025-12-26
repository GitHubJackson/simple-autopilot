#include <iostream>
#include <string>
#include "../common_msgs/simple_image.hpp"

// 生成测试资源的小工具
int main() {
    // 160x120 pixels * 3 bytes/pixel = 57,600 bytes + Header < 60KB (UDP Safe)
    auto img = simple_image::SimpleImage::CreateTestImage(160, 120);
    if (img.SavePPM("simple-autopilot/assets/test_image.ppm")) {
        std::cout << "Generated simple-autopilot/assets/test_image.ppm" << std::endl;
    } else {
        std::cerr << "Failed to generate image!" << std::endl;
        return 1;
    }
    return 0;
}

