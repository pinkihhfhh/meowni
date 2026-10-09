#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "engine.h"

namespace companion {

struct ManagerConfig {
    long ram_headroom_mb = 800;  // always leave this much free for the OS / games
    size_t max_resident = 1;     // how many models may be in RAM at once
    int idle_unload_seconds = 90;
};

class ModelManager {
public:
    explicit ModelManager(const ManagerConfig& cfg = ManagerConfig());
    ~ModelManager();

    bool registerModel(const ModelSpec& spec, std::string& err);
    bool load(const std::string& id, std::string& err);
    void unload(const std::string& id);
    void unloadAll();

    // Loads the model if needed (evicting the least recently used one), then streams
    // output through cb. A cancelled run returns true with whatever was emitted so far.
    bool generate(const std::string& id, const std::string& prompt, const GenParams& p,
                  const TokenCallback& cb, std::string& err);

    // Safe to call from any thread, never blocks.
    void cancel();

    // Android ComponentCallbacks2 level. Call from a background thread: it waits for
    // the running generation to stop, then frees every model.
    void onTrimMemory(int level);

    std::string statusJson();
    static long availableRamMb();  // MemAvailable from /proc/meminfo, -1 if unknown

private:
    struct Resident {
        std::unique_ptr<IEngine> engine;
        std::chrono::steady_clock::time_point last_used;
    };

    bool loadLocked(const std::string& id, std::string& err);
    void evictLruLocked();
    void watchdog();

    ManagerConfig cfg_;
    std::mutex mu_;  // guards registry_ and resident_; held during generation
    std::map<std::string, ModelSpec> registry_;
    std::map<std::string, Resident> resident_;

    std::atomic<bool> cancel_{false};
    std::atomic<bool> stop_{false};
    std::mutex cv_mu_;
    std::condition_variable cv_;
    std::thread watchdog_;
};

}  // namespace companion
