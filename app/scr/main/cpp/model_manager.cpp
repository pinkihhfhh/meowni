#include "model_manager.h"

#include <cstdio>
#include <fstream>
#include <sstream>

namespace companion {

namespace {
std::string jsonEscape(const std::string& s) {
    std::string o;
    for (unsigned char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if (c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); o += b; }
                else o += static_cast<char>(c);
        }
    }
    return o;
}
}  // namespace

std::unique_ptr<IEngine> makeEngine(ModelKind kind) {
#ifdef COMPANION_WITH_LLAMA
    if (kind == ModelKind::LLM) return makeLlamaEngine();
#endif
    (void)kind;
    return makeStubEngine();
}

ModelManager::ModelManager(const ManagerConfig& cfg) : cfg_(cfg) {
    if (cfg_.max_resident < 1) cfg_.max_resident = 1;
    watchdog_ = std::thread(&ModelManager::watchdog, this);
}

ModelManager::~ModelManager() {
    cancel_ = true;
    stop_ = true;
    cv_.notify_all();
    if (watchdog_.joinable()) watchdog_.join();
    std::lock_guard<std::mutex> g(mu_);
    resident_.clear();  // engines unload in their destructors
}

bool ModelManager::registerModel(const ModelSpec& spec, std::string& err) {
    if (spec.id.empty()) { err = "model id is empty"; return false; }
    std::lock_guard<std::mutex> g(mu_);
    registry_[spec.id] = spec;
    return true;
}

long ModelManager::availableRamMb() {
    std::ifstream f("/proc/meminfo");
    std::string key, unit;
    long kb = 0;
    while (f >> key >> kb >> unit) {
        if (key == "MemAvailable:") return kb / 1024;
    }
    return -1;
}

void ModelManager::evictLruLocked() {
    auto victim = resident_.end();
    for (auto it = resident_.begin(); it != resident_.end(); ++it)
        if (victim == resident_.end() || it->second.last_used < victim->second.last_used)
            victim = it;
    if (victim != resident_.end()) {
        victim->second.engine->unload();
        resident_.erase(victim);
    }
}

bool ModelManager::loadLocked(const std::string& id, std::string& err) {
    auto reg = registry_.find(id);
    if (reg == registry_.end()) { err = "unknown model: " + id; return false; }
    if (resident_.count(id)) return true;

    while (!resident_.empty() && resident_.size() >= cfg_.max_resident) evictLruLocked();

    const ModelSpec& spec = reg->second;
    long avail = availableRamMb();
    long need = static_cast<long>(spec.est_ram_mb) + cfg_.ram_headroom_mb;
    if (avail >= 0 && avail < need) {
        err = "not enough free RAM: " + std::to_string(avail) + " MB free, need " +
              std::to_string(need) + " MB (model + headroom)";
        return false;
    }

    auto engine = makeEngine(spec.kind);
    if (!engine->load(spec, err)) return false;
    resident_[id] = Resident{std::move(engine), std::chrono::steady_clock::now()};
    return true;
}

bool ModelManager::load(const std::string& id, std::string& err) {
    std::lock_guard<std::mutex> g(mu_);
    return loadLocked(id, err);
}

void ModelManager::unload(const std::string& id) {
    std::lock_guard<std::mutex> g(mu_);
    auto it = resident_.find(id);
    if (it == resident_.end()) return;
    it->second.engine->unload();
    resident_.erase(it);
}

void ModelManager::unloadAll() {
    std::lock_guard<std::mutex> g(mu_);
    for (auto& kv : resident_) kv.second.engine->unload();
    resident_.clear();
}

bool ModelManager::generate(const std::string& id, const std::string& prompt,
                            const GenParams& p, const TokenCallback& cb, std::string& err) {
    std::lock_guard<std::mutex> g(mu_);
    cancel_ = false;
    if (!loadLocked(id, err)) return false;
    Resident& r = resident_[id];
    auto wrapped = [this, &cb](const std::string& piece) {
        if (cancel_) return false;
        return cb(piece);
    };
    bool ok = r.engine->generate(prompt, p, wrapped, err);
    r.last_used = std::chrono::steady_clock::now();
    return ok;
}

void ModelManager::cancel() { cancel_ = true; }

void ModelManager::onTrimMemory(int level) {
    // 15 = TRIM_MEMORY_RUNNING_CRITICAL, 40+ = app is in the background and being trimmed
    if (level < 15) return;
    cancel_ = true;
    unloadAll();  // waits for the running generation (it stops at the next token)
}

void ModelManager::watchdog() {
    while (!stop_) {
        {
            std::unique_lock<std::mutex> lk(cv_mu_);
            cv_.wait_for(lk, std::chrono::seconds(5), [this] { return stop_.load(); });
        }
        if (stop_) break;
        std::unique_lock<std::mutex> g(mu_, std::try_to_lock);
        if (!g.owns_lock()) continue;  // a generation is running
        auto now = std::chrono::steady_clock::now();
        for (auto it = resident_.begin(); it != resident_.end();) {
            auto idle = std::chrono::duration_cast<std::chrono::seconds>(now - it->second.last_used);
            if (idle.count() >= cfg_.idle_unload_seconds) {
                it->second.engine->unload();
                it = resident_.erase(it);
            } else {
                ++it;
            }
        }
    }
}

std::string ModelManager::statusJson() {
    std::lock_guard<std::mutex> g(mu_);
    std::ostringstream o;
    o << "{\"available_ram_mb\":" << availableRamMb()
      << ",\"max_resident\":" << cfg_.max_resident << ",\"resident\":[";
    bool first = true;
    for (auto& kv : resident_) {
        if (!first) o << ",";
        first = false;
        o << "\"" << jsonEscape(kv.first) << "\"";
    }
    o << "],\"registered\":[";
    first = true;
    for (auto& kv : registry_) {
        if (!first) o << ",";
        first = false;
        o << "{\"id\":\"" << jsonEscape(kv.first) << "\",\"kind\":" << static_cast<int>(kv.second.kind)
          << ",\"est_ram_mb\":" << kv.second.est_ram_mb << "}";
    }
    o << "]}";
    return o.str();
}

}  // namespace companion
