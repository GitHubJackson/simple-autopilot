# 简易订阅发布中间件

## 概述

这是一个独立的、线程安全的订阅发布中间件实现，提供基本的消息传递功能。完全解耦，不依赖任何外部项目。

## 项目结构

```
simple_middleware/
├── pub_sub_middleware.hpp      # 中间件核心头文件
├── pub_sub_middleware.cpp      # 中间件核心实现
├── data_publisher.hpp          # 数据发布模块头文件
├── data_publisher.cpp          # 数据发布模块实现
├── test_subscriber.hpp         # 测试订阅者头文件
├── test_subscriber.cpp         # 测试订阅者实现
├── test_main.cpp               # 测试程序
├── logger.hpp                  # 简单日志工具（独立实现）
├── CMakeLists.txt              # CMake构建文件
└── README.md                   # 本文件
```

## 依赖

- C++11 或更高版本
- pthread（Linux 系统自带）
- CMake 3.10+（用于构建）

**无其他外部依赖！**

## 编译

```bash
mkdir build
cd build
cmake ..
make
```

编译后会生成 `bin/test_middleware` 可执行文件。

## 运行测试

```bash
./bin/test_middleware
```

## 核心功能

### 1. PubSubMiddleware（中间件核心）

单例模式的订阅发布中间件，提供以下功能：

- **发布消息**: `publish(topic, data)`
- **订阅主题**: `subscribe(topic, callback)`
- **取消订阅**: `unsubscribe(subscribe_id)`
- **查询统计**: `getSubscriberCount(topic)`, `getAllTopics()`

### 2. DataPublisher（数据发布模块）

定时发布测试数据的模块：

- 可配置发布间隔
- 自动生成 JSON 格式的测试数据
- 线程安全

### 3. TestSubscriber（测试订阅者）

用于测试的订阅者：

- 自动统计接收到的消息数量
- 保存最后一次接收的消息

## 使用方法

### 基本使用示例

```cpp
#include "pub_sub_middleware.hpp"
#include "data_publisher.hpp"
#include "test_subscriber.hpp"

using namespace simple_middleware;

// 1. 创建发布者（每1秒发布一次）
DataPublisher publisher("test/topic", 1000);
publisher.start();

// 2. 创建订阅者
TestSubscriber subscriber("test/topic");
subscriber.start();

// 3. 运行一段时间
std::this_thread::sleep_for(std::chrono::seconds(10));

// 4. 停止
publisher.stop();
subscriber.stop();

// 5. 查看统计
std::cout << "发布: " << publisher.getMessageCount() << std::endl;
std::cout << "接收: " << subscriber.getMessageCount() << std::endl;
```

### 直接使用中间件 API

```cpp
using namespace simple_middleware;

auto& middleware = PubSubMiddleware::getInstance();

// 订阅
int64_t sub_id = middleware.subscribe("my/topic",
    [](const Message& msg) {
        std::cout << "收到消息: " << msg.data << std::endl;
    }
);

// 发布
middleware.publish("my/topic", "Hello, World!");

// 取消订阅
middleware.unsubscribe(sub_id);
```

## 特性

1. **完全独立**: 不依赖任何外部项目或库
2. **线程安全**: 使用 mutex 保护所有共享数据
3. **单例模式**: 全局唯一的中间件实例
4. **回调机制**: 使用 std::function 实现灵活的回调
5. **简单易用**: API 设计简洁明了
6. **轻量级**: 代码量小，易于理解和维护

## 命名空间

所有代码都在 `simple_middleware` 命名空间下，避免与其他项目冲突。

## 日志系统

项目包含一个简单的日志工具 `logger.hpp`，支持：

- DEBUG, INFO, WARN, ERROR 四个级别
- 线程安全的日志输出
- 可通过修改 `Logger::min_level_` 控制日志级别

## 注意事项

1. 订阅回调函数应该是线程安全的
2. 回调函数中不要执行耗时操作，避免阻塞其他订阅者
3. 取消订阅后，回调函数不会再被调用
4. 消息是同步传递的，发布者会等待所有订阅者回调完成

## 扩展建议

- 添加消息队列缓冲
- 支持消息过滤
- 添加消息持久化
- 支持多播和广播
- 添加性能监控
- 支持异步消息传递

## 许可证

本项目为独立项目，可根据需要添加许可证信息。
