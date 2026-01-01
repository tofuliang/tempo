package com.cappielloantonio.tempo.player

import android.content.Context
import androidx.media3.common.AudioAttributes
import androidx.media3.common.C
import androidx.media3.common.DeviceInfo
import androidx.media3.common.MediaItem
import androidx.media3.common.PlaybackException
import androidx.media3.common.PlaybackParameters
import androidx.media3.common.Player
import androidx.media3.common.Timeline
import androidx.media3.common.Tracks
import androidx.media3.common.util.UnstableApi
import com.cappielloantonio.tempo.model.AirPlayDevice
import com.cappielloantonio.tempo.service.AirPlayConnectionManager
import com.cappielloantonio.tempo.service.AirPlaySession

@UnstableApi
class AirPlayPlayer(
    private val context: Context,
    private val connectionManager: AirPlayConnectionManager
) : Player {

    private val listeners = mutableSetOf<Player.Listener>()
    private var playWhenReady = false
    private var playbackState = Player.STATE_IDLE
    private var currentPosition = 0L
    private var currentMediaItem: MediaItem? = null
    private var currentSession: AirPlaySession? = null

    // Player interface implementation
    override fun setPlayWhenReady(playWhenReady: Boolean) {
        this.playWhenReady = playWhenReady
        if (playWhenReady && playbackState == Player.STATE_READY) {
            startPlayback()
        }
    }

    override fun getPlayWhenReady(): Boolean = playWhenReady

    override fun setMediaItems(mediaItems: List<MediaItem>) {
        if (mediaItems.isNotEmpty()) {
            currentMediaItem = mediaItems[0]
        }
    }

    override fun setMediaItems(mediaItems: List<MediaItem>, resetPosition: Boolean) {
        if (mediaItems.isNotEmpty()) {
            currentMediaItem = mediaItems[0]
            if (resetPosition) {
                currentPosition = 0
            }
        }
    }

    override fun setMediaItems(mediaItems: List<MediaItem>, startIndex: Int, startPositionMs: Long) {
        if (mediaItems.isNotEmpty()) {
            currentMediaItem = mediaItems[startIndex]
            currentPosition = startPositionMs
        }
    }

    override fun setMediaItem(mediaItem: MediaItem) {
        currentMediaItem = mediaItem
    }

    override fun setMediaItem(mediaItem: MediaItem, resetPosition: Boolean) {
        currentMediaItem = mediaItem
        if (resetPosition) {
            currentPosition = 0
        }
    }

    override fun setMediaItem(mediaItem: MediaItem, startPositionMs: Long) {
        currentMediaItem = mediaItem
        currentPosition = startPositionMs
    }

    override fun getMediaItemCount(): Int = if (currentMediaItem != null) 1 else 0

    override fun getMediaItemAt(index: Int): MediaItem {
        return currentMediaItem ?: throw IndexOutOfBoundsException()
    }

    override fun getCurrentMediaItem(): MediaItem? = currentMediaItem

    override fun addMediaItem(mediaItem: MediaItem) {
        currentMediaItem = mediaItem
    }

    override fun addMediaItem(index: Int, mediaItem: MediaItem) {
        currentMediaItem = mediaItem
    }

    override fun addMediaItems(mediaItems: List<MediaItem>) {
        if (mediaItems.isNotEmpty()) {
            currentMediaItem = mediaItems.last()
        }
    }

    override fun addMediaItems(index: Int, mediaItems: List<MediaItem>) {
        if (mediaItems.isNotEmpty()) {
            currentMediaItem = mediaItems.last()
        }
    }

    override fun removeMediaItem(index: Int) {
        if (index == 0) {
            currentMediaItem = null
        }
    }

    override fun removeMediaItems(fromIndex: Int, toIndex: Int) {
        currentMediaItem = null
    }

    override fun moveMediaItem(currentIndex: Int, newIndex: Int) {
        // No-op for single item implementation
    }

    override fun moveMediaItems(fromIndex: Int, toIndex: Int, newIndex: Int) {
        // No-op for single item implementation
    }

    override fun clearMediaItems() {
        currentMediaItem = null
    }

    override fun replaceMediaItem(index: Int, mediaItem: MediaItem) {
        currentMediaItem = mediaItem
    }

    override fun replaceMediaItems(fromIndex: Int, toIndex: Int, mediaItems: List<MediaItem>) {
        if (mediaItems.isNotEmpty()) {
            currentMediaItem = mediaItems[0]
        } else {
            currentMediaItem = null
        }
    }

    override fun getCurrentPeriodIndex(): Int = 0

    override fun getCurrentMediaItemIndex(): Int = 0

    override fun getDuration(): Long = C.TIME_UNSET

    override fun getContentDuration(): Long = C.TIME_UNSET

    override fun getCurrentPosition(): Long = currentPosition

    override fun getContentPosition(): Long = currentPosition

    override fun getBufferedPosition(): Long = 0

    override fun getContentBufferedPosition(): Long = 0

    override fun getBufferedPercentage(): Int = 0

    override fun seekToDefaultPosition() {
        seekToDefaultPosition(0)
    }

    override fun seekToDefaultPosition(mediaItemIndex: Int) {
        seekTo(mediaItemIndex, 0)
    }

    override fun seekTo(mediaItemIndex: Int, positionMs: Long) {
        currentPosition = positionMs
        // Note: Not firing discontinuity event for simplicity in skeleton
    }

    override fun seekTo(positionMs: Long) {
        seekTo(0, positionMs)
    }

    override fun seekBack() {
        seekTo(maxOf(0, currentPosition - 5000))
    }

    override fun seekForward() {
        seekTo(currentPosition + 5000)
    }

    override fun hasPreviousMediaItem(): Boolean = false

    override fun hasNextMediaItem(): Boolean = false

    override fun seekToPreviousMediaItem() {}

    override fun seekToNextMediaItem() {}

    override fun setPlaybackParameters(parameters: PlaybackParameters) {}

    override fun getPlaybackParameters(): PlaybackParameters = PlaybackParameters.DEFAULT

    override fun setPlaybackSpeed(speed: Float) {}

    override fun setShuffleModeEnabled(shuffleModeEnabled: Boolean) {}

    override fun getShuffleModeEnabled(): Boolean = false

    override fun setRepeatMode(repeatMode: Int) {}

    override fun getRepeatMode(): Int = Player.REPEAT_MODE_OFF

    override fun addListener(listener: Player.Listener) {
        listeners.add(listener)
    }

    override fun removeListener(listener: Player.Listener) {
        listeners.remove(listener)
    }

    override fun getApplicationLooper() = context.mainLooper

    override fun getPlaybackState(): Int = playbackState

    override fun getPlaybackSuppressionReason(): Int = Player.PLAYBACK_SUPPRESSION_REASON_NONE

    override fun isPlaying(): Boolean = playWhenReady && playbackState == Player.STATE_READY

    override fun isPlayingAd(): Boolean = false

    override fun getCurrentAdGroupIndex(): Int = C.INDEX_UNSET

    override fun getCurrentAdIndexInAdGroup(): Int = C.INDEX_UNSET

    override fun isLoading(): Boolean = false

    override fun seekToNextWindow() {}

    override fun seekToPreviousWindow() {}

    override fun seekToNext() {}

    override fun seekToPrevious() {}

    override fun getPlayerError(): PlaybackException? = null

    override fun getDeviceVolume(): Int = 0

    override fun setDeviceVolume(volume: Int) {
        setDeviceVolume(volume, 0)
    }

    override fun setDeviceVolume(volume: Int, flags: Int) {}

    override fun isDeviceMuted(): Boolean = false

    override fun setDeviceMuted(muted: Boolean) {
        setDeviceMuted(muted, 0)
    }

    override fun setDeviceMuted(muted: Boolean, flags: Int) {}

    override fun increaseDeviceVolume() {
        increaseDeviceVolume(0)
    }

    override fun increaseDeviceVolume(flags: Int) {}

    override fun decreaseDeviceVolume() {
        decreaseDeviceVolume(0)
    }

    override fun decreaseDeviceVolume(flags: Int) {}

    override fun getAudioAttributes(): AudioAttributes = AudioAttributes.DEFAULT

    override fun setAudioAttributes(audioAttributes: AudioAttributes, handleAudioFocus: Boolean) {}

    override fun setVolume(volume: Float) {}

    override fun getVolume(): Float = 1f

    override fun clearVideoSurface() {}

    override fun clearVideoSurface(surface: android.view.Surface?) {}

    override fun setVideoSurface(surface: android.view.Surface?) {}

    override fun setVideoSurfaceHolder(surfaceHolder: android.view.SurfaceHolder?) {}

    override fun clearVideoSurfaceHolder(surfaceHolder: android.view.SurfaceHolder?) {}

    override fun setVideoSurfaceView(surfaceView: android.view.SurfaceView?) {}

    override fun clearVideoSurfaceView(surfaceView: android.view.SurfaceView?) {}

    override fun setVideoTextureView(textureView: android.view.TextureView?) {}

    override fun clearVideoTextureView(textureView: android.view.TextureView?) {}

    override fun getVideoSize(): androidx.media3.common.VideoSize = androidx.media3.common.VideoSize.UNKNOWN

    override fun getSurfaceSize(): androidx.media3.common.util.Size = androidx.media3.common.util.Size(0, 0)

    override fun getCurrentCues(): androidx.media3.common.text.CueGroup =
        androidx.media3.common.text.CueGroup(emptyList(), 0L)

    override fun getCurrentTracks(): Tracks = Tracks.EMPTY

    override fun getTrackSelectionParameters(): androidx.media3.common.TrackSelectionParameters {
        return androidx.media3.common.TrackSelectionParameters.Builder(context).build()
    }

    override fun setTrackSelectionParameters(parameters: androidx.media3.common.TrackSelectionParameters) {}

    override fun getDeviceInfo(): DeviceInfo = DeviceInfo.UNKNOWN

    override fun getNextMediaItemIndex(): Int = C.INDEX_UNSET

    override fun getPreviousMediaItemIndex(): Int = C.INDEX_UNSET

    override fun canAdvertiseSession(): Boolean = false

    override fun getAvailableCommands(): Player.Commands = Player.Commands.EMPTY

    override fun isCommandAvailable(command: Int): Boolean = false

    override fun prepare() {
        playbackState = Player.STATE_READY
        listeners.forEach { listener ->
            listener.onPlaybackStateChanged(playbackState)
        }
    }

    override fun getSeekBackIncrement(): Long = 5000L

    override fun getSeekForwardIncrement(): Long = 5000L

    override fun getMaxSeekToPreviousPosition(): Long = 5000L

    override fun getMediaMetadata(): androidx.media3.common.MediaMetadata =
        androidx.media3.common.MediaMetadata.EMPTY

    override fun getPlaylistMetadata(): androidx.media3.common.MediaMetadata =
        androidx.media3.common.MediaMetadata.EMPTY

    override fun setPlaylistMetadata(metadata: androidx.media3.common.MediaMetadata) {}

    override fun getCurrentManifest(): Any? = null

    override fun hasNext(): Boolean = false

    override fun next() {}

    override fun hasNextWindow(): Boolean = false

    override fun getCurrentTimeline(): Timeline = Timeline.EMPTY

    override fun getCurrentWindowIndex(): Int = 0

    override fun getNextWindowIndex(): Int = C.INDEX_UNSET

    override fun getPreviousWindowIndex(): Int = C.INDEX_UNSET

    override fun isCurrentWindowDynamic(): Boolean = false

    override fun isCurrentMediaItemDynamic(): Boolean = false

    override fun isCurrentMediaItemLive(): Boolean = false

    override fun isCurrentWindowLive(): Boolean = false

    override fun isCurrentWindowSeekable(): Boolean = true

    override fun isCurrentMediaItemSeekable(): Boolean = true

    override fun getCurrentLiveOffset(): Long = C.TIME_UNSET

    override fun getTotalBufferedDuration(): Long = 0

    // AirPlay-specific methods
    suspend fun connect(device: AirPlayDevice): Boolean {
        val result = connectionManager.connect(device)
        return result.fold(
            onSuccess = { session ->
                currentSession = session
                playbackState = Player.STATE_READY
                listeners.forEach { listener ->
                    listener.onPlaybackStateChanged(playbackState)
                }
                true
            },
            onFailure = { false }
        )
    }

    fun disconnect() {
        connectionManager.disconnect()
        currentSession = null
        playbackState = Player.STATE_IDLE
        listeners.forEach { listener ->
            listener.onPlaybackStateChanged(playbackState)
        }
    }

    private fun startPlayback() {
        playbackState = Player.STATE_READY
        listeners.forEach { listener ->
            listener.onPlaybackStateChanged(playbackState)
            listener.onIsPlayingChanged(true)
        }
    }

    private fun pausePlayback() {
        listeners.forEach { listener ->
            listener.onIsPlayingChanged(false)
        }
    }

    override fun play() {
        startPlayback()
    }

    override fun pause() {
        pausePlayback()
    }

    override fun stop() {
        pausePlayback()
    }

    override fun release() {
        disconnect()
        listeners.clear()
    }
}
