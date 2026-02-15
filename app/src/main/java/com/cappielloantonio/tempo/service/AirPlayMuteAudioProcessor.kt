package com.cappielloantonio.tempo.service

import androidx.media3.common.C
import androidx.media3.common.audio.AudioProcessor
import androidx.media3.common.audio.BaseAudioProcessor
import androidx.media3.common.util.UnstableApi
import java.nio.ByteBuffer

@UnstableApi
class AirPlayMuteAudioProcessor : BaseAudioProcessor() {

    private val airPlay = AirPlaySessionManager.getInstance()

    override fun onConfigure(
        inputAudioFormat: AudioProcessor.AudioFormat
    ): AudioProcessor.AudioFormat {
        return if (inputAudioFormat.encoding == C.ENCODING_PCM_16BIT ||
            inputAudioFormat.encoding == C.ENCODING_PCM_FLOAT
        ) {
            inputAudioFormat
        } else {
            AudioProcessor.AudioFormat.NOT_SET
        }
    }

    override fun queueInput(inputBuffer: ByteBuffer) {
        val remaining = inputBuffer.remaining()
        if (remaining == 0) return

        val outputBuffer = replaceOutputBuffer(remaining)

        if (!airPlay.isPushModeActive) {
            outputBuffer.put(inputBuffer)
        } else {
            fillZeros(outputBuffer, remaining)
            inputBuffer.position(inputBuffer.limit())
        }

        outputBuffer.flip()
    }

    private fun fillZeros(output: ByteBuffer, size: Int) {
        for (i in 0 until size) {
            output.put(0)
        }
    }
}
