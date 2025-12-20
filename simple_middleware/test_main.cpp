/*
 * @Desc: 中间件测试程序
 * @Author: JacksonZhou
 * @Date: 2025/12/19
 * 
 * 使用方法：
 *   1. 创建一个发布者，定时发送数据
 *   2. 创建一个订阅者，接收数据
 *   3. 运行一段时间后停止
 */

#include "pub_sub_middleware.hpp"
#include "data_publisher.hpp"
#include "test_subscriber.hpp"
#include "logger.hpp"
#include <iostream>
#include <thread>
#include <chrono>

using namespace simple_middleware;

int main(int argc, char* argv[]) {
    LOG_INFO("TestMain") << "=== 简易订阅发布中间件测试程序 ===";

    // 测试主题
    const std::string test_topic = "test/topic";

    // 1. 创建发布者（每500ms发布一次）
    LOG_INFO("TestMain") << "创建数据发布者...";
    DataPublisher publisher(test_topic, 500);
    
    // 2. 创建订阅者
    LOG_INFO("TestMain") << "创建测试订阅者...";
    TestSubscriber subscriber(test_topic);

    // 3. 先启动订阅者
    if (!subscriber.start()) {
        LOG_ERROR("TestMain") << "订阅者启动失败";
        return -1;
    }

    // 4. 启动发布者
    if (!publisher.start()) {
        LOG_ERROR("TestMain") << "发布者启动失败";
        subscriber.stop();
        return -1;
    }

    LOG_INFO("TestMain") << "系统运行中，按Ctrl+C停止...";
    LOG_INFO("TestMain") << "发布间隔: " << publisher.getInterval() << "ms";
    LOG_INFO("TestMain") << "订阅主题: " << test_topic;

    // 5. 运行10秒
    for (int i = 0; i < 20; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        // 每2秒打印一次统计信息
        if (i % 4 == 3) {
            LOG_INFO("TestMain") << "统计信息 - "
                                 << "发布消息数: " << publisher.getMessageCount() 
                                 << ", 接收消息数: " << subscriber.getMessageCount();
        }
    }

    // 6. 停止
    LOG_INFO("TestMain") << "停止系统...";
    publisher.stop();
    subscriber.stop();

    // 7. 打印最终统计
    LOG_INFO("TestMain") << "=== 最终统计 ===";
    LOG_INFO("TestMain") << "发布消息总数: " << publisher.getMessageCount();
    LOG_INFO("TestMain") << "接收消息总数: " << subscriber.getMessageCount();
    LOG_INFO("TestMain") << "最后一条消息: " << subscriber.getLastMessage();

    // 8. 测试中间件统计信息
    auto& middleware = PubSubMiddleware::getInstance();
    LOG_INFO("TestMain") << "主题订阅者数量: " << middleware.getSubscriberCount(test_topic);
    
    auto topics = middleware.getAllTopics();
    LOG_INFO("TestMain") << "所有主题数量: " << topics.size();
    for (const auto& topic : topics) {
        LOG_INFO("TestMain") << "  - " << topic << " (订阅者: " 
                            << middleware.getSubscriberCount(topic) << ")";
    }

    LOG_INFO("TestMain") << "=== 测试完成 ===";
    return 0;
}
