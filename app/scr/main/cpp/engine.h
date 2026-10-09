#pragma once
#include <cstddef>
#include <functional>
#include <memory>
#include <string>

namespace companion {

enum class ModelKind { LLM = 0, STT = 1, TTS = 2 };

struct ModelSpec {
    std::string id;
    std::string path;
    ModelKind kind = ModelKind::LLM;
    size_t est_ram_mb = 0;   // file size + KV cache estimate; used for the RAM check
    int context_len = 2048;
    int threads = 2;
};

struct GenParams {
    int max_tokens = 256;
    float temperature = 0.7f;
    float top_p = 0.9f;
};

// Called once per generated piece. Return false to stop generation.
using TokenCallback = std::function<bool(const std::string& piece)>;

// One backend per model kind. Only LLM generation is wired up for now;
// STT/TTS get their own interfaces when whisper.cpp / sherpa-onnx are added.
class IEngine {
public:
    virtual ~IEngine() = default;
    virtual bool load(const ModelSpec& spec, std::string& err) = 0;
    virtual void unload() = 0;
    virtual bool generate(const std::string& prompt, const GenParams& p,
                          const TokenCallback& cb, std::string& err) = 0;
};

std::unique_ptr<IEngine> makeStubEngine();
#ifdef COMPANION_WITH_LLAMA
std::unique_ptr<IEngine> makeLlamaEngine();
#endif
std::unique_ptr<IEngine> makeEngine(ModelKind kind);

}  // namespace companion
