package com.companion.app

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.IBinder

/** Foreground service that keeps the native runtime alive in the background. */
class CompanionService : Service() {

    override fun onCreate() {
        super.onCreate()
        ModelRuntime.ensureInit()

        val channelId = "companion_runtime"
        val nm = getSystemService(NotificationManager::class.java)
        nm.createNotificationChannel(
            NotificationChannel(channelId, "Companion runtime", NotificationManager.IMPORTANCE_MIN))
        val notification = Notification.Builder(this, channelId)
            .setContentTitle("Companion is running")
            .setSmallIcon(android.R.drawable.ic_dialog_info)
            .setOngoing(true)
            .build()

        if (Build.VERSION.SDK_INT >= 34) {
            startForeground(1, notification, ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE)
        } else {
            startForeground(1, notification)
        }
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int) = START_STICKY

    override fun onTrimMemory(level: Int) {
        super.onTrimMemory(level)
        ModelRuntime.trimMemory(level)  // frees models when RAM gets tight (e.g. a game opens)
    }

    override fun onDestroy() {
        ModelRuntime.shutdown()
        super.onDestroy()
    }

    override fun onBind(intent: Intent?): IBinder? = null
}
