#pragma once
#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>

// fixed-window, in-memory limiter. per-instance only: behind multiple replicas
// use a gateway/Redis-backed limiter for global limits.
class RateLimiter {
  public:
    bool allow(const std::string& key, int limit) {
        using namespace std::chrono;
        const auto now = steady_clock::now();
        std::lock_guard lk(m_);
        if (windows_.size() > 50000) purge(now);
        auto& w = windows_[key];
        if (now - w.start >= seconds(60)) {
            w.start = now;
            w.count = 0;
        }
        return ++w.count <= limit;
    }

  private:
    struct Window {
        std::chrono::steady_clock::time_point start{};
        int count{0};
    };
    void purge(std::chrono::steady_clock::time_point now) {
        for (auto it = windows_.begin(); it != windows_.end();) {
            if (now - it->second.start >= std::chrono::seconds(60))
                it = windows_.erase(it);
            else
                ++it;
        }
    }
    std::mutex m_;
    std::unordered_map<std::string, Window> windows_;
};
