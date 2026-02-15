package com.cappielloantonio.tempo.service

import android.graphics.Bitmap
import android.util.Log
import androidx.lifecycle.LiveData
import androidx.lifecycle.MutableLiveData
import com.cappielloantonio.tempo.airplay.AirPlayClient
import com.cappielloantonio.tempo.model.AirPlayDevice
import java.io.ByteArrayOutputStream
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors

class AirPlaySessionManager private constructor() {

    private val airPlayClient = AirPlayClient()
    private val executorService: ExecutorService = Executors.newSingleThreadExecutor()
    private val stateLiveData = MutableLiveData(STATE_DISCONNECTED)

    var currentDevice: AirPlayDevice? = null
        private set

    @Volatile
    var isPushModeActive = false
        private set

    private val stateCallback = object : AirPlayClient.StateCallback {
        override fun onStateChanged(nativeState: Int) {
            Log.d(TAG, "native state: ${AirPlayClient.stateName(nativeState)}")
            when (nativeState) {
                AirPlayClient.STATE_STOPPED -> {
                    isPushModeActive = false
                    updateState(STATE_DISCONNECTED)
                    currentDevice = null
                }
                AirPlayClient.STATE_CONNECTED -> updateState(STATE_CONNECTING)
                AirPlayClient.STATE_STREAMING -> updateState(STATE_PLAYING)
                AirPlayClient.STATE_PAUSED -> updateState(STATE_PAUSED)
                AirPlayClient.STATE_FAILED -> {
                    isPushModeActive = false
                    updateState(STATE_DISCONNECTED)
                    currentDevice = null
                }
                AirPlayClient.STATE_IDLE -> updateState(STATE_CONNECTED)
            }
        }
    }

    val state: LiveData<Int> get() = stateLiveData

    val isActive: Boolean
        get() {
            val s = stateLiveData.value
            return s != null && s != STATE_DISCONNECTED
        }

    val isPlaying: Boolean
        get() {
            val s = stateLiveData.value
            return s != null && (s == STATE_PLAYING || s == STATE_BUFFERING)
        }

    fun connect(device: AirPlayDevice?) {
        if (device == null) return
        currentDevice = device
        updateState(STATE_CONNECTING)

        executorService.submit {
            val ret = airPlayClient.connectAirPlay(device.ip, device.port, stateCallback)
            if (ret == 0) {
                Log.i(TAG, "Connect initiated to ${device.name}")
            } else {
                Log.e(TAG, "connectAirPlay failed: $ret")
                currentDevice = null
                updateState(STATE_DISCONNECTED)
            }
        }
    }

    fun pause() {
        executorService.submit { airPlayClient.pauseAirPlay() }
    }

    fun resume() {
        executorService.submit { airPlayClient.resumeAirPlay() }
    }

    fun stop() {
        currentDevice = null
        isPushModeActive = false
        executorService.submit { airPlayClient.stopAirPlay() }
    }

    fun seek(positionMs: Long) {
        executorService.submit { airPlayClient.seekAirPlay(positionMs.toInt()) }
    }

    /** Start push mode: C layer reads PCM from ring buffer instead of downloading. */
    fun startPushMode() {
        Log.d(TAG, "startPushMode requested, currentState=${stateLiveData.value}")
        isPushModeActive = true
        executorService.submit {
            Log.d(TAG, "startPushMode: calling native")
            airPlayClient.nativeStartPushMode()
            Log.d(TAG, "startPushMode: native returned")
        }
    }

    /** Push PCM data into the native ring buffer. Called from AudioSink thread. */
    fun pushPcm(pcmData: ByteArray, sampleCount: Int) {
        if (!isPushModeActive) return
        airPlayClient.nativePushPcm(pcmData, sampleCount)
    }

    /**
     * Flush the native ring buffer (e.g. on seek or track transition).
     * MUST be synchronous: called from the audio rendering thread, the buffer
     * must be empty before the next handleBuffer() pushes new PCM.
     */
    fun flushPushBuffer() {
        if (!isPushModeActive) return
        Log.d(TAG, "flushPushBuffer (sync)")
        airPlayClient.nativeFlushPushBuffer()
    }

    fun setVolume(volumePercent: Int) {
        executorService.submit { airPlayClient.setVolume(volumePercent) }
    }

    /** Thread-safe: native getter reads atomic values only. */
    val volumePct: Int get() = airPlayClient.getVolumePct()

    fun updateMetadata(title: String?, artist: String?, album: String?, durationMs: Long, artwork: Bitmap?) {
        executorService.submit {
            airPlayClient.setMetadata(title, artist, album, null, durationMs.toInt(), 0, 0)
            if (artwork != null) {
                val stream = ByteArrayOutputStream()
                artwork.compress(Bitmap.CompressFormat.PNG, 100, stream)
                airPlayClient.setArtwork(stream.toByteArray(), true)
            }
        }
    }

    private fun updateState(newState: Int) {
        stateLiveData.postValue(newState)
    }

    companion object {
        private const val TAG = "AirPlaySessionManager"

        const val STATE_DISCONNECTED = 0
        const val STATE_CONNECTING = 1
        const val STATE_CONNECTED = 6
        const val STATE_PLAYING = 2
        const val STATE_PAUSED = 3
        const val STATE_BUFFERING = 4
        const val STATE_STOPPED = 5

        @JvmStatic
        @Volatile
        private var instance: AirPlaySessionManager? = null

        @JvmStatic
        fun getInstance(): AirPlaySessionManager {
            return instance ?: synchronized(this) {
                instance ?: AirPlaySessionManager().also { instance = it }
            }
        }
    }
}
