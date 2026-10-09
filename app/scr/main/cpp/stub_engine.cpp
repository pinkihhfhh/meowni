#include <chrono>
#include <sstream>
#include <thread>

#include "engine.h"

namespace companion {

// Placeholder backend: streams the prompt back word by word. It lets you test the whole
// path (UI -> Kotlin -> JNI -> manager -> streaming -> cancel) before real models exist.
class StubEngine : public IEngine {
public:
    bool load(const ModelSpec& spec, std::string&) override {
        id_ = spec.id;
        loaded_ = true;
        return true;
    }
    void unload() override { loaded_ = false; }
    bool generate(const std::string& prompt, const GenParams& p, const TokenCallback& cb,
                  std::string& err) override {
        if (!loaded_) { err = "stub engine not loaded"; return false; }
        if (!cb("[" + id_ + "] ")) return true;
        std::istringstream words(prompt);
        std::string w;
        int n = 0;
        while (n < p.max_tokens && words >> w) {
            if (!cb(w + " ")) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            ++n;
        }
        return true;
    }

private:
    std::string id_;
    bool loaded_ = false;
};

std::unique_ptr<IEngine> makeStubEngine() { return std::unique_ptr<IEngine>(new StubEngine()); }

}  // namespace companion
