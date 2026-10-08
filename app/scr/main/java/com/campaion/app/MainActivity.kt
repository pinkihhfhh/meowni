package com.companion.app

import android.Manifest
import android.annotation.SuppressLint
import android.app.Activity
import android.content.Intent
import android.os.Build
import android.os.Bundle
import android.webkit.JavascriptInterface
import android.webkit.WebView
import org.json.JSONObject

class MainActivity : Activity() {
    private lateinit var web: WebView

    @SuppressLint("SetJavaScriptEnabled")
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        if (Build.VERSION.SDK_INT >= 33) requestPermissions(arrayOf(Manifest.permission.POST_NOTIFICATIONS), 1)

        ModelRuntime.ensureInit()
        startForegroundService(Intent(this, CompanionService::class.java))

        web = WebView(this)
        web.settings.javaScriptEnabled = true
        web.addJavascriptInterface(Bridge(), "Android")
        web.loadUrl("file:///android_asset/index.html")
        setContentView(web)
    }

    private fun js(fn: String, arg: String) {
        runOnUiThread { web.evaluateJavascript("window.$fn(${JSONObject.quote(arg)})", null) }
    }

    /** What the HTML UI can call as window.Android.* */
    inner class Bridge {
        @JavascriptInterface fun status(): String = ModelRuntime.status()

        @JavascriptInterface fun registerStub(): String =
            ModelRuntime.register("stub", "", ModelKind.LLM, estRamMb = 0) ?: "ok"

        @JavascriptInterface fun ask(id: String, prompt: String) {
            ModelRuntime.async {
                val err = ModelRuntime.generate(id, prompt, maxTokens = 128) { piece -> js("onToken", piece); true }
                if (err != null) js("onError", err)
                js("onDone", "")
            }
        }

        @JavascriptInterface fun cancel() = ModelRuntime.cancel()
    }
}
