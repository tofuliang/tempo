package com.cappielloantonio.tempo.airplay

class AirPlayClient {

    interface StateCallback {
        fun onStateChanged(state: Int)
    }

    external fun connectAirPlay(ip: String, port: Int, callback: StateCallback): Int
    external fun stopAirPlay()
    external fun pauseAirPlay(): Int
    external fun resumeAirPlay(): Int
    external fun seekAirPlay(positionMs: Int): Int
    external fun setVolume(volumePct: Int): Int
    external fun setMetadata(
        title: String?, artist: String?, album: String?, genre: String?,
        durationMs: Int, trackNumber: Int, discNumber: Int
    ): Int
    external fun setArtwork(data: ByteArray, isPng: Boolean): Int
    external fun getVolumePct(): Int
    external fun getPositionMs(): Int
    external fun getDurationMs(): Int
    external fun nativeStartPushMode()
    external fun nativePushPcm(pcmData: ByteArray, sampleCount: Int): Int
    external fun nativeFlushPushBuffer()

    companion object {
        init {
            System.loadLibrary("avutil")
            System.loadLibrary("swresample")
            System.loadLibrary("avcodec")
            System.loadLibrary("avformat")
            System.loadLibrary("tempo-airplay")
        }

        const val STATE_STOPPED = 0
        const val STATE_CONNECTED = 1
        const val STATE_STREAMING = 2
        const val STATE_PAUSED = 3
        const val STATE_FAILED = 4

        const val STATE_IDLE = 5

        val STATE_NAMES = arrayOf("STOPPED", "CONNECTED", "STREAMING", "PAUSED", "FAILED", "IDLE")

        fun stateName(state: Int): String = STATE_NAMES.getOrElse(state) { "UNKNOWN($state)" }
    }
}
