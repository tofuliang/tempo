package com.cappielloantonio.tempo.service

import android.content.Context
import android.util.Log
import androidx.media3.common.C
import androidx.media3.common.Format
import androidx.media3.common.util.UnstableApi
import androidx.media3.exoplayer.audio.AudioSink
import androidx.media3.exoplayer.audio.ForwardingAudioSink
import java.nio.ByteBuffer
import java.nio.ByteOrder

@UnstableApi
class AirPlayForwardingAudioSink(
    @Suppress("unused") context: Context,
    sink: AudioSink
) : ForwardingAudioSink(sink) {

    private val airPlay = AirPlaySessionManager.getInstance()
    private var currentSampleRate = 0
    private var currentChannelCount = 0
    private var currentEncoding = 0
    private var pushCount = 0L
    private var totalBytesPushed = 0L
    private var lastPushedPts = Long.MIN_VALUE
    private var resamplePhase = 0.0

    private var nullSinkActive = false
    private var syntheticClockStartNanos = 0L
    private var syntheticClockBaseUs = 0L
    private var syntheticClockRunning = false
    private var syntheticClockNeedsSync = true
    private var endOfStreamReceived = false

    private fun shouldUseNullSink(): Boolean =
        airPlay.isPushModeActive

    override fun handleBuffer(
        buffer: ByteBuffer,
        presentationTimeUs: Long,
        encodedAccessUnitCount: Int
    ): Boolean {
        val pushActive = airPlay.isPushModeActive
        if (pushActive && presentationTimeUs != lastPushedPts) {
            try {
                val pos = buffer.position()
                val remaining = buffer.remaining()
                if (remaining > 0) {
                    val data = ByteArray(remaining)
                    buffer.get(data)
                    buffer.position(pos)

                    val s16Data = toS16Stereo(data, currentEncoding, currentChannelCount)
                    if (s16Data != null) {
                        val pushData = if (currentSampleRate != TARGET_SAMPLE_RATE && currentSampleRate > 0) {
                            resample(s16Data, currentSampleRate)
                        } else {
                            s16Data
                        }

                        val sampleCount = pushData.size / 4
                        if (sampleCount > 0) {
                            airPlay.pushPcm(pushData, sampleCount)
                        }

                    pushCount++
                    totalBytesPushed += pushData.size
                    if (pushCount == 1L || pushCount % 100 == 0L) {
                        Log.d(TAG, "push #$pushCount: ${pushData.size}B/${sampleCount}smp, " +
                                "rate=$currentSampleRate, enc=$currentEncoding, ch=$currentChannelCount, " +
                                "totalBytes=$totalBytesPushed, nullSink=$nullSinkActive")
                    }
                    }
                }
            } catch (e: Exception) {
                Log.e(TAG, "handleBuffer push error", e)
            }
            lastPushedPts = presentationTimeUs
        }

        if (shouldUseNullSink()) {
            if (!nullSinkActive) {
                Log.d(TAG, "entering null sink mode, pts=$presentationTimeUs")
                nullSinkActive = true
                syntheticClockNeedsSync = true
            }
            if (syntheticClockNeedsSync && presentationTimeUs >= 0) {
                syntheticClockBaseUs = presentationTimeUs
                syntheticClockStartNanos = System.nanoTime()
                syntheticClockRunning = true
                syntheticClockNeedsSync = false
                Log.d(TAG, "synthetic clock synced to pts=$presentationTimeUs")
            }
            buffer.position(buffer.limit())
            return true
        }

        if (nullSinkActive) {
            Log.d(TAG, "leaving null sink → resuming AudioTrack")
            nullSinkActive = false
            syntheticClockRunning = false
            super.play()
        }

        return super.handleBuffer(buffer, presentationTimeUs, encodedAccessUnitCount)
    }

    override fun play() {
        Log.d(TAG, "play: pushModeActive=${airPlay.isPushModeActive}, nullSink=${shouldUseNullSink()}")
        if (shouldUseNullSink()) {
            nullSinkActive = true
            syntheticClockNeedsSync = true
            return
        }
        nullSinkActive = false
        super.play()
    }

    override fun pause() {
        Log.d(TAG, "pause")
        if (nullSinkActive) {
            if (syntheticClockRunning) {
                syntheticClockBaseUs += (System.nanoTime() - syntheticClockStartNanos) / 1000
                syntheticClockRunning = false
            }
            return
        }
        super.pause()
    }

    override fun getCurrentPositionUs(sourceEnded: Boolean): Long {
        if (nullSinkActive) {
            if (syntheticClockNeedsSync) return AudioSink.CURRENT_POSITION_NOT_SET
            if (!syntheticClockRunning) return syntheticClockBaseUs
            val elapsedUs = (System.nanoTime() - syntheticClockStartNanos) / 1000
            return syntheticClockBaseUs + elapsedUs
        }
        return super.getCurrentPositionUs(sourceEnded)
    }

    override fun hasPendingData(): Boolean {
        if (nullSinkActive) return false
        return super.hasPendingData()
    }

    override fun isEnded(): Boolean {
        if (nullSinkActive) return endOfStreamReceived
        return super.isEnded()
    }

    override fun playToEndOfStream() {
        if (nullSinkActive) {
            endOfStreamReceived = true
            return
        }
        super.playToEndOfStream()
    }

    private fun bytesPerSample(encoding: Int): Int = when (encoding) {
        C.ENCODING_PCM_8BIT -> 1
        C.ENCODING_PCM_16BIT, C.ENCODING_PCM_16BIT_BIG_ENDIAN -> 2
        C.ENCODING_PCM_24BIT, C.ENCODING_PCM_24BIT_BIG_ENDIAN -> 3
        C.ENCODING_PCM_32BIT, C.ENCODING_PCM_32BIT_BIG_ENDIAN, C.ENCODING_PCM_FLOAT -> 4
        else -> 2
    }

    private fun toS16Stereo(data: ByteArray, encoding: Int, channels: Int): ByteArray? {
        if (channels < 1) return null
        val bps = bytesPerSample(encoding)
        val frameSize = bps * channels
        if (frameSize == 0 || data.size < frameSize) return null
        val frames = data.size / frameSize
        val src = ByteBuffer.wrap(data).order(ByteOrder.LITTLE_ENDIAN)
        val out = ByteBuffer.allocate(frames * 4).order(ByteOrder.LITTLE_ENDIAN)

        for (i in 0 until frames) {
            val base = i * frameSize
            val l = readSampleAsS16(src, data, base, encoding)
            val r = if (channels >= 2) readSampleAsS16(src, data, base + bps, encoding) else l
            out.putShort(l)
            out.putShort(r)
        }
        return out.array()
    }

    private fun readSampleAsS16(buf: ByteBuffer, raw: ByteArray, offset: Int, encoding: Int): Short {
        return when (encoding) {
            C.ENCODING_PCM_16BIT -> buf.getShort(offset)
            C.ENCODING_PCM_FLOAT -> {
                val f = buf.getFloat(offset).coerceIn(-1.0f, 1.0f)
                (f * 32767).toInt().toShort()
            }
            C.ENCODING_PCM_24BIT -> {
                val hi = (raw[offset + 1].toInt() and 0xFF) or (raw[offset + 2].toInt() shl 8)
                hi.toShort()
            }
            C.ENCODING_PCM_32BIT -> (buf.getInt(offset) shr 16).toShort()
            C.ENCODING_PCM_8BIT -> ((raw[offset].toInt() - 128) shl 8).toShort()
            else -> buf.getShort(offset)
        }
    }

    private fun resample(input: ByteArray, srcRate: Int): ByteArray {
        val srcBuf = ByteBuffer.wrap(input).order(ByteOrder.LITTLE_ENDIAN)
        val srcFrames = input.size / 4
        val ratio = srcRate.toDouble() / TARGET_SAMPLE_RATE
        if (resamplePhase < 0) resamplePhase = 0.0

        val outFrames = ((srcFrames - resamplePhase) / ratio).toInt()
        if (outFrames <= 0) {
            resamplePhase -= srcFrames
            if (resamplePhase < 0) resamplePhase = 0.0
            return ByteArray(0)
        }

        val outBuf = ByteBuffer.allocate(outFrames * 4).order(ByteOrder.LITTLE_ENDIAN)
        var lastSrcPos = 0.0
        for (i in 0 until outFrames) {
            val srcPos = resamplePhase + i * ratio
            val idx = srcPos.toInt().coerceIn(0, srcFrames - 1)
            val frac = srcPos - idx

            if (idx + 1 < srcFrames) {
                val l0 = srcBuf.getShort(idx * 4).toInt()
                val r0 = srcBuf.getShort(idx * 4 + 2).toInt()
                val l1 = srcBuf.getShort((idx + 1) * 4).toInt()
                val r1 = srcBuf.getShort((idx + 1) * 4 + 2).toInt()
                outBuf.putShort((l0 + (l1 - l0) * frac).toInt().coerceIn(-32768, 32767).toShort())
                outBuf.putShort((r0 + (r1 - r0) * frac).toInt().coerceIn(-32768, 32767).toShort())
            } else {
                outBuf.putShort(srcBuf.getShort(idx * 4))
                outBuf.putShort(srcBuf.getShort(idx * 4 + 2))
            }
            lastSrcPos = srcPos
        }
        resamplePhase = lastSrcPos + ratio - srcFrames
        if (resamplePhase < 0) resamplePhase = 0.0

        val result = ByteArray(outBuf.position())
        outBuf.flip()
        outBuf.get(result)
        return result
    }

    override fun flush() {
        Log.d(TAG, "flush: pushModeActive=${airPlay.isPushModeActive}, pushCount=$pushCount, nullSink=$nullSinkActive")
        if (airPlay.isPushModeActive) {
            airPlay.flushPushBuffer()
        }
        if (nullSinkActive && syntheticClockRunning) {
            syntheticClockBaseUs += (System.nanoTime() - syntheticClockStartNanos) / 1000
            syntheticClockRunning = false
        }
        syntheticClockNeedsSync = true
        endOfStreamReceived = false
        pushCount = 0
        totalBytesPushed = 0
        lastPushedPts = Long.MIN_VALUE
        resamplePhase = 0.0
        super.flush()
    }

    override fun configure(
        inputFormat: Format,
        specifiedBufferSize: Int,
        outputChannels: IntArray?
    ) {
        currentSampleRate = inputFormat.sampleRate
        currentChannelCount = inputFormat.channelCount
        currentEncoding = inputFormat.pcmEncoding
        resamplePhase = 0.0
        Log.d(TAG, "configure: sampleRate=$currentSampleRate, " +
                "channels=$currentChannelCount, encoding=$currentEncoding (0x${currentEncoding.toString(16)}), " +
                "needsResample=${currentSampleRate != TARGET_SAMPLE_RATE}, " +
                "mime=${inputFormat.sampleMimeType}")
        super.configure(inputFormat, specifiedBufferSize, outputChannels)
    }

    override fun reset() {
        Log.d(TAG, "reset: pushCount=$pushCount, totalBytes=$totalBytesPushed")
        nullSinkActive = false
        syntheticClockRunning = false
        syntheticClockBaseUs = 0
        endOfStreamReceived = false
        pushCount = 0
        totalBytesPushed = 0
        lastPushedPts = Long.MIN_VALUE
        resamplePhase = 0.0
        super.reset()
    }

    companion object {
        private const val TAG = "AirPlayFwdAudioSink"
        private const val TARGET_SAMPLE_RATE = 44100
    }
}
