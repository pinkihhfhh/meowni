#include <jni.h>

#include <memory>
#include <mutex>
#include <string>

#include "model_manager.h"

using namespace companion;

namespace {
std::mutex g_mu;
std::shared_ptr<ModelManager> g_mgr;

std::shared_ptr<ModelManager> mgr() {
    std::lock_guard<std::mutex> g(g_mu);
    return g_mgr;
}

std::string toStd(JNIEnv* env, jstring s) {
    if (!s) return "";
    const char* c = env->GetStringUTFChars(s, nullptr);
    std::string out(c ? c : "");
    if (c) env->ReleaseStringUTFChars(s, c);
    return out;
}

jstring toJ(JNIEnv* env, const std::string& s) { return env->NewStringUTF(s.c_str()); }

// Length of the longest prefix that does not end in a half-finished UTF-8 character.
// Tokens can split a multi-byte character, so pieces are buffered until they are complete.
size_t completeUtf8Prefix(const std::string& s) {
    size_t n = s.size();
    for (size_t back = 1; back <= 4 && back <= n; ++back) {
        unsigned char c = static_cast<unsigned char>(s[n - back]);
        if ((c & 0xC0) == 0x80) continue;  // continuation byte, keep looking for the lead byte
        size_t need = c < 0x80 ? 1 : c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
        return back >= need ? n : n - back;
    }
    return n;
}
}  // namespace

extern "C" {

JNIEXPORT void JNICALL Java_com_companion_app_NativeBridge_nativeInit(
    JNIEnv*, jobject, jint headroomMb, jint maxResident, jint idleSeconds) {
    std::lock_guard<std::mutex> g(g_mu);
    if (g_mgr) return;
    ManagerConfig cfg;
    cfg.ram_headroom_mb = headroomMb;
    cfg.max_resident = static_cast<size_t>(maxResident);
    cfg.idle_unload_seconds = idleSeconds;
    g_mgr = std::make_shared<ModelManager>(cfg);
}

JNIEXPORT void JNICALL Java_com_companion_app_NativeBridge_nativeShutdown(JNIEnv*, jobject) {
    std::shared_ptr<ModelManager> m;
    {
        std::lock_guard<std::mutex> g(g_mu);
        m = std::move(g_mgr);
    }
    if (m) m->cancel();  // the manager is destroyed when the last running call releases it
}

JNIEXPORT jstring JNICALL Java_com_companion_app_NativeBridge_nativeRegisterModel(
    JNIEnv* env, jobject, jstring id, jstring path, jint kind, jint estRamMb, jint ctx, jint threads) {
    auto m = mgr();
    if (!m) return toJ(env, "native layer not initialised");
    ModelSpec spec;
    spec.id = toStd(env, id);
    spec.path = toStd(env, path);
    spec.kind = static_cast<ModelKind>(kind);
    spec.est_ram_mb = estRamMb > 0 ? static_cast<size_t>(estRamMb) : 0;
    spec.context_len = ctx;
    spec.threads = threads;
    std::string err;
    return m->registerModel(spec, err) ? toJ(env, "") : toJ(env, err);
}

JNIEXPORT jstring JNICALL Java_com_companion_app_NativeBridge_nativeLoad(JNIEnv* env, jobject, jstring id) {
    auto m = mgr();
    if (!m) return toJ(env, "native layer not initialised");
    std::string err;
    return m->load(toStd(env, id), err) ? toJ(env, "") : toJ(env, err);
}

JNIEXPORT void JNICALL Java_com_companion_app_NativeBridge_nativeUnload(JNIEnv* env, jobject, jstring id) {
    if (auto m = mgr()) m->unload(toStd(env, id));
}

// Prompt arrives as UTF-8 bytes (JNI's own string encoding mangles emoji).
// Output pieces go to listener.onToken(byte[]) -> boolean. Returns "" or an error message.
JNIEXPORT jstring JNICALL Java_com_companion_app_NativeBridge_nativeGenerate(
    JNIEnv* env, jobject, jstring id, jbyteArray prompt, jint maxTokens, jfloat temperature,
    jfloat topP, jobject listener) {
    auto m = mgr();
    if (!m) return toJ(env, "native layer not initialised");

    jsize plen = env->GetArrayLength(prompt);
    std::string promptStr(static_cast<size_t>(plen), '\0');
    if (plen > 0)
        env->GetByteArrayRegion(prompt, 0, plen, reinterpret_cast<jbyte*>(&promptStr[0]));

    jclass cls = env->GetObjectClass(listener);
    jmethodID onToken = env->GetMethodID(cls, "onToken", "([B)Z");
    if (!onToken) return toJ(env, "listener has no onToken(byte[])");

    GenParams gp;
    gp.max_tokens = maxTokens;
    gp.temperature = temperature;
    gp.top_p = topP;

    std::string pending;
    auto emit = [&](const std::string& bytes) -> bool {
        jbyteArray arr = env->NewByteArray(static_cast<jsize>(bytes.size()));
        env->SetByteArrayRegion(arr, 0, static_cast<jsize>(bytes.size()),
                                reinterpret_cast<const jbyte*>(bytes.data()));
        jboolean keepGoing = env->CallBooleanMethod(listener, onToken, arr);
        env->DeleteLocalRef(arr);
        if (env->ExceptionCheck()) { env->ExceptionClear(); return false; }
        return keepGoing == JNI_TRUE;
    };

    std::string err;
    bool ok = m->generate(toStd(env, id), promptStr, gp,
        [&](const std::string& piece) {
            pending += piece;
            size_t n = completeUtf8Prefix(pending);
            if (n == 0) return true;
            std::string out = pending.substr(0, n);
            pending.erase(0, n);
            return emit(out);
        }, err);
    if (!pending.empty()) emit(pending);  // flush whatever is left
    return ok ? toJ(env, "") : toJ(env, err);
}

JNIEXPORT void JNICALL Java_com_companion_app_NativeBridge_nativeCancel(JNIEnv*, jobject) {
    if (auto m = mgr()) m->cancel();
}

JNIEXPORT void JNICALL Java_com_companion_app_NativeBridge_nativeTrimMemory(JNIEnv*, jobject, jint level) {
    if (auto m = mgr()) m->onTrimMemory(level);
}

JNIEXPORT jstring JNICALL Java_com_companion_app_NativeBridge_nativeStatus(JNIEnv* env, jobject) {
    auto m = mgr();
    return toJ(env, m ? m->statusJson() : "{\"error\":\"native layer not initialised\"}");
}

}  // extern "C"
