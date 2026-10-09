// Real LLM backend. Only compiled with -DCOMPANION_WITH_LLAMA=ON (see CMakeLists.txt).
// Written against the current llama.cpp C API (llama_model_load_from_file / llama_vocab /
// llama_memory_clear). Pin llama.cpp to a tag you have built; the API moves between releases.
#ifdef COMPANION_WITH_LLAMA
#include <mutex>
#include <vector>

#include "engine.h"
#include "llama.h"

namespace companion {

class LlamaEngine : public IEngine {
public:
    ~LlamaEngine() override { unload(); }

    bool load(const ModelSpec& spec, std::string& err) override {
        static std::once_flag once;
        std::call_once(once, [] { llama_backend_init(); });

        llama_model_params mp = llama_model_default_params();
        mp.use_mmap = true;  // pages load lazily, and the OS can reclaim them under pressure
        model_ = llama_model_load_from_file(spec.path.c_str(), mp);
        if (!model_) { err = "cannot load model file: " + spec.path; return false; }

        llama_context_params cp = llama_context_default_params();
        cp.n_ctx = spec.context_len;
        cp.n_threads = spec.threads;
        cp.n_threads_batch = spec.threads;
        ctx_ = llama_init_from_model(model_, cp);
        if (!ctx_) { err = "cannot create context"; unload(); return false; }
        n_ctx_ = spec.context_len;
        return true;
    }

    void unload() override {
        if (ctx_) { llama_free(ctx_); ctx_ = nullptr; }
        if (model_) { llama_model_free(model_); model_ = nullptr; }
    }

    bool generate(const std::string& prompt, const GenParams& p, const TokenCallback& cb,
                  std::string& err) override {
        if (!ctx_) { err = "model not loaded"; return false; }
        const llama_vocab* vocab = llama_model_get_vocab(model_);

        int n = -llama_tokenize(vocab, prompt.c_str(), (int)prompt.size(), nullptr, 0, true, true);
        if (n <= 0) { err = "tokenize failed"; return false; }
        if (n + p.max_tokens > n_ctx_) { err = "prompt + max_tokens exceeds context length"; return false; }
        std::vector<llama_token> toks(n);
        if (llama_tokenize(vocab, prompt.c_str(), (int)prompt.size(), toks.data(), n, true, true) < 0) {
            err = "tokenize failed";
            return false;
        }

        llama_memory_clear(llama_get_memory(ctx_), true);

        llama_sampler* smpl = llama_sampler_chain_init(llama_sampler_chain_default_params());
        llama_sampler_chain_add(smpl, llama_sampler_init_top_p(p.top_p, 1));
        llama_sampler_chain_add(smpl, llama_sampler_init_temp(p.temperature));
        llama_sampler_chain_add(smpl, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));

        llama_batch batch = llama_batch_get_one(toks.data(), (int)toks.size());
        bool ok = true;
        for (int i = 0; i < p.max_tokens; ++i) {
            if (llama_decode(ctx_, batch) != 0) { err = "decode failed"; ok = false; break; }
            llama_token tok = llama_sampler_sample(smpl, ctx_, -1);
            if (llama_vocab_is_eog(vocab, tok)) break;
            char buf[256];
            int len = llama_token_to_piece(vocab, tok, buf, sizeof buf, 0, true);
            if (len < 0) len = 0;
            if (!cb(std::string(buf, len))) break;
            batch = llama_batch_get_one(&tok, 1);
        }
        llama_sampler_free(smpl);
        return ok;
    }

private:
    llama_model* model_ = nullptr;
    llama_context* ctx_ = nullptr;
    int n_ctx_ = 0;
};

std::unique_ptr<IEngine> makeLlamaEngine() { return std::unique_ptr<IEngine>(new LlamaEngine()); }

}  // namespace companion
#endif
