package com.cytarch.cellularity.platform.logging

import android.util.Log

internal object AppLog {
    private const val TAG = "Cellularity"

    fun debug(message: String) {
        Log.d(TAG, message)
    }

    fun error(message: String, failure: Throwable? = null) {
        Log.e(TAG, message, failure)
    }
}
