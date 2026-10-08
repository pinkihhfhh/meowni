package com.companion.app

import java.util.concurrent.Executors

enum class ModelKind(val code: Int) { LLM(0), STT(1), TTS(2) }

/** Friendly wrapper over [NativeBridge]. Functions returning String? give null on success. */
object ModelRuntime {
    private var started = false
    private val worker = Executors.newSingleThreadExecutor()  // generations run one at a time

    @Synchronized
    fun ensureInit(headroomMb: Int = 800, maxResident: Int = 1, idleSeconds: Int = 90) {
        if (started) return
        NativeBridge.nativeInit(headroomMb, maxResident, idleSeconds)
        started = true
    }

    fun register(id: String, path: String, kind: ModelKind, estRamMb: Int, ctx: Int = 2048, threads: Int = 2): String? =
        NativeBridge.nativeRegisterModel(id, path, kind.code, estRamMb, ctx, threads).ifEmpty { null }

    fun load(id: String): String? = NativeBridge.nativeLoad(id).ifEmpty { null }
    fun unload(id: String) = NativeBridge.nativeUnload(id)
    fun cancel() = NativeBridge.nativeCancel()   // never queued behind a running generation
    fun status(): String = NativeBridge.nativeStatus()

    /** Blocking. Call from [async] or your own background thread, never the main thread. */
    fun generate(id: String, prompt: String, maxTokens: Int = 256,
                 temperature: Float = 0.7f, topP: Float = 0.9f,
                 onToken: (String) -> Boolean): String? =
        NativeBridge.nativeGenerate(id, prompt.toByteArray(Charsets.UTF_8), maxTokens, temperature, topP,
            TokenListener { bytes -> onToken(String(bytes, Charsets.UTF_8)) }).ifEmpty { null }

    fun async(block: () -> Unit) { worker.execute(block) }

    /** Wire this to Service.onTrimMemory. Runs off the main thread because it waits for generation to stop. */
    fun trimMemory(level: Int) { Thread { NativeBridge.nativeTrimMemory(level) }.start() }

    @Synchronized
    fun shutdown() {
        if (!started) return
        NativeBridge.nativeShutdown()
        started = false
    }
}
