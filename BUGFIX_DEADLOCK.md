# 死锁/阻塞问题修复记录

## 问题描述

1. **检测框不显示**：Perception 模块生成的检测框无法在前端显示
2. **偶发卡死**：系统在某些操作（如点击"go to target"）后会卡死

## 根本原因

`PubSubMiddleware::dispatchLocal` 函数在持有 `mutex_` 锁的情况下同步调用回调函数，导致：

1. **锁持有时间过长**：如果回调函数执行时间较长，会长时间占用锁，阻塞其他线程
2. **潜在死锁**：如果回调函数内部再次调用 `publish` → `dispatchLocal`，会再次尝试获取 `mutex_`，导致死锁
3. **阻塞传播**：一个慢回调会阻塞所有后续消息的分发

## 修复方法

### 修复前（有问题的代码）

```cpp
void PubSubMiddleware::dispatchLocal(const std::string& topic, const std::string& data) {
    std::lock_guard<std::mutex> lock(mutex_);  // 🔒 持有锁

    // ... 准备消息 ...

    // ❌ 问题：在持有锁的情况下直接调用回调函数
    for (int64_t sub_id : it->second) {
        sub_it->second.callback(msg);  // 回调可能执行很长时间，或者再次尝试获取锁
    }
}  // 锁在这里才释放
```

### 修复后（正确的代码）

```cpp
void PubSubMiddleware::dispatchLocal(const std::string& topic, const std::string& data) {
    std::vector<std::function<void()>> callbacks_to_execute;

    {
        std::lock_guard<std::mutex> lock(mutex_);  // 🔒 只在访问共享数据时持有锁

        // ... 准备消息 ...

        // ✅ 修复：在锁内只收集回调函数，不执行
        for (int64_t sub_id : it->second) {
            callbacks_to_execute.push_back([topic, sub_id, msg, sub_it]() {
                sub_it->second.callback(msg);
            });
        }
    } // 🔓 锁在这里释放

    // ✅ 修复：在锁外执行所有回调函数
    for (auto& callback : callbacks_to_execute) {
        callback();  // 此时没有持有锁，可以安全执行
    }
}
```

## 修复要点

1. **最小化锁持有时间**：只在访问共享数据结构时持有锁
2. **锁外执行回调**：回调函数在锁外执行，避免长时间占用锁
3. **避免死锁**：即使回调中再次调用 `publish`，也不会因为重复获取同一把锁而死锁

## 为什么之前会偶发卡死？

- 回调函数执行时间较长（如 Planning 模块处理复杂逻辑）
- 回调函数内部可能再次调用 `publish`（间接调用 `dispatchLocal`）
- 多个模块同时发布消息，竞争同一把锁

## 修复效果

✅ **检测框正常显示**：Perception 发布 `perception/detection_2d` 不再阻塞  
✅ **偶发卡死消失**：回调在锁外执行，不会长时间占用锁  
✅ **系统响应性提升**：消息分发不再被慢回调阻塞

## 经验总结

这是一个典型的**"锁粒度优化"**问题，遵循**"最小化锁持有时间"**原则：

- 锁只保护必要的临界区（共享数据结构）
- 耗时操作（回调函数）在锁外执行
- 避免在持有锁的情况下调用可能再次获取锁的函数
