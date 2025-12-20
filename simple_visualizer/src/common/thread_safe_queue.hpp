#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>
#include <memory>

template<typename T>
class ThreadSafeQueue {
public:
    ThreadSafeQueue() {}

    // 生产数据：入队
    void Push(T value) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(std::move(value));
        // 唤醒一个正在等待的消费者
        cond_.notify_one();
    }

    // 消费数据：出队 (阻塞等待)
    // 如果队列为空，线程会挂起，直到有数据进来
    bool Pop(T& value) {
        std::unique_lock<std::mutex> lock(mutex_);
        // 等待条件：队列不为空 或 收到停止信号(暂未实现停止信号)
        cond_.wait(lock, [this] { return !queue_.empty(); });

        value = std::move(queue_.front());
        queue_.pop();
        return true;
    }

    // 尝试非阻塞出队
    bool TryPop(T& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) {
            return false;
        }
        value = std::move(queue_.front());
        queue_.pop();
        return true;
    }

    bool Empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    size_t Size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

private:
    std::queue<T> queue_;
    mutable std::mutex mutex_;
    std::condition_variable cond_;
};
