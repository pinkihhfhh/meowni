# Companion: native runtime skeleton

Layers: HTML UI (assets/index.html) -> Kotlin (ModelRuntime, CompanionService) -> JNI -> C++ ModelManager -> engines.

## Build
Push to GitHub. Actions builds the debug APK (Actions tab -> run -> Artifacts -> companion-debug-apk).

## Test without any model
Open the app, tap "Register stub model", then "Ask". Tokens stream back through the whole stack. "Cancel" stops it mid-stream.

## Host test (no Android needed)
g++ -std=c++17 -pthread native_tests/test_manager.cpp app/src/main/cpp/model_manager.cpp app/src/main/cpp/stub_engine.cpp -o t && ./t

## Add real models (llama.cpp)
1. git submodule add https://github.com/ggml-org/llama.cpp app/src/main/cpp/third_party/llama.cpp
2. Pin it to a release tag.
3. In app/build.gradle.kts set -DCOMPANION_WITH_LLAMA=ON.
4. Put a .gguf on the phone and register it: ModelRuntime.register("qwen", "/sdcard/.../model.gguf", ModelKind.LLM, estRamMb = 1200)

## Manager rules (ManagerConfig)
- max_resident = 1: loading a model evicts the least recently used one first
- ram_headroom_mb: refuses to load if free RAM minus the model would drop below this
- idle_unload_seconds: unloads a model after this long unused
- onTrimMemory(level >= 15): cancels the running reply and frees everything (game opened, RAM tight)
