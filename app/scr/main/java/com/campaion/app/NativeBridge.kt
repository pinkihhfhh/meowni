package com.companion.app

/** Receives streamed output. Return false to stop generation. */
fun interface TokenListener {
    fun onToken(piece: ByteArray): Boolean
}

/** Raw JNI surface. Use [ModelRuntime] instead of calling this directly. */
object NativeBridge {
    init { System.loadLibrary("companion_native") }

    external fun nativeInit(headroomMb: Int, maxResident: Int, idleSeconds: Int)
    external fun nativeShutdown()
    external fun nativeRegisterModel(id: String, path: String, kind: Int, estRamMb: Int, ctx: Int, threads: Int): String
    external fun nativeLoad(id: String): String
    external fun nativeUnload(id: String)
    external fun nativeGenerate(id: String, prompt: ByteArray, maxTokens: Int, temperature: Float, topP: Float, listener: TokenListener): String
    external fun nativeCancel()
    external fun nativeTrimMemory(level: Int)
    external fun nativeStatus(): String
}
