package com.cappielloantonio.tempo.service

import android.graphics.Bitmap
import android.util.Log
import androidx.lifecycle.Observer
import androidx.media3.common.Player
import androidx.media3.common.util.UnstableApi
import com.bumptech.glide.Glide
import com.cappielloantonio.tempo.App
import com.cappielloantonio.tempo.glide.CustomGlideRequest
import com.cappielloantonio.tempo.util.Preferences
import java.util.concurrent.Executors


/**
 * Bridges Media3 ExoPlayer with AirPlay output.
 * Handles state observation, local mute enforcement, track forwarding,
 * and playback control synchronization.
 *
 * Lifecycle: create in MediaService.onCreate(), release in onDestroy().
 */
@UnstableApi
class AirPlayMediaBridge(private val player: Player) {

    private val airPlay = AirPlaySessionManager.getInstance()
    private val artworkExecutor = Executors.newSingleThreadExecutor()

    private val stateObserver = Observer<Int> { state ->
        Log.d(TAG, "AirPlay state=$state, isPlaying=${player.isPlaying}, playWhenReady=${player.playWhenReady}")
        when (state) {
            AirPlaySessionManager.STATE_CONNECTED -> {
                if (player.isPlaying || player.playWhenReady) {
                    sendCurrentTrack()
                }
            }
        }
    }

    fun initialize() {
        airPlay.state.observeForever(stateObserver)
    }

    fun release() {
        airPlay.state.removeObserver(stateObserver)
        airPlay.stop()
    }

    /**
     * Forward current track to AirPlay device using push mode.
     * In push mode, PCM is intercepted from Media3's AudioSink and pushed to
     * the native ring buffer via JNI. The C layer no longer downloads/decodes.
     */
    fun sendCurrentTrack() {
        if (!airPlay.isActive) return

        val mediaItem = player.currentMediaItem ?: return
        val id = mediaItem.mediaId
        if (id.isNullOrBlank()) return

        Log.d(TAG, "sendCurrentTrack (push mode): id=$id")
        airPlay.startPushMode()

        val meta = player.mediaMetadata
        val title = meta.title?.toString() ?: ""
        val artist = meta.artist?.toString() ?: ""
        val album = meta.albumTitle?.toString() ?: ""
        val durationMs = player.duration.coerceAtLeast(0)
        Log.d(TAG, "sendMetadata: $artist - $title [$album] ${durationMs}ms")
        airPlay.updateMetadata(title, artist, album, durationMs, null)

        val coverArtId = meta.extras?.getString("coverArtId")
        if (!coverArtId.isNullOrBlank()) {
            val url = CustomGlideRequest.createUrl(coverArtId, Preferences.getImageSize())
            artworkExecutor.submit {
                try {
                    val bmp = Glide.with(App.getContext())
                        .asBitmap().load(url)
                        .submit(600, 600).get()
                    airPlay.updateMetadata(title, artist, album, durationMs, bmp)
                } catch (e: Exception) {
                    Log.w(TAG, "Artwork load failed: ${e.message}")
                }
            }
        }
    }

    /** Sync pause state from ExoPlayer to AirPlay. */
    fun onPlayerPaused() {
        if (airPlay.isPlaying) airPlay.pause()
    }

    /** Sync resume state from ExoPlayer to AirPlay. */
    fun onPlayerResumed() {
        val st = airPlay.state.value
        if (st == AirPlaySessionManager.STATE_PAUSED) {
            airPlay.resume()
        } else if (st == AirPlaySessionManager.STATE_CONNECTED && !airPlay.isPushModeActive) {
            sendCurrentTrack()
        }
    }

    /** Forward seek to AirPlay. */
    fun onPlayerSeeked(positionMs: Long) {
        if (airPlay.isPlaying) {
            airPlay.seek(positionMs)
        }
    }

    val isActive: Boolean get() = airPlay.isActive

    companion object {
        private const val TAG = "AirPlayMediaBridge"
    }
}
