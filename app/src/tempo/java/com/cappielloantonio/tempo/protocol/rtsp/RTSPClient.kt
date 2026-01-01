package com.cappielloantonio.tempo.protocol.rtsp

import android.util.Log
import java.io.InputStream
import java.io.OutputStream
import java.net.Socket
import kotlin.coroutines.resume
import kotlin.coroutines.suspendCoroutine

class RTSPClient(
    private val host: String,
    private val port: Int
) {
    companion object {
        private const val TAG = "RTSPClient"
        private const val READ_TIMEOUT = 5000
        private const val CONNECTION_TIMEOUT = 5000
    }

    private var socket: Socket? = null
    private var outputStream: OutputStream? = null
    private var inputStream: InputStream? = null
    private var currentSequence = 0
    private var sessionId: String? = null

    suspend fun connect(): Boolean {
        return suspendCoroutine { continuation ->
            try {
                socket = Socket(host, port).apply {
                    soTimeout = READ_TIMEOUT
                }
                outputStream = socket?.getOutputStream()
                inputStream = socket?.getInputStream()
                continuation.resume(true)
            } catch (e: Exception) {
                Log.e(TAG, "Failed to connect to $host:$port", e)
                continuation.resume(false)
            }
        }
    }

    suspend fun send(request: RTSPRequest): RTSPResponse? {
        return suspendCoroutine { continuation ->
            try {
                val output = outputStream ?: throw IllegalStateException("Not connected")
                val input = inputStream ?: throw IllegalStateException("Not connected")

                // Add sequence and session
                val enrichedRequest = request.copy(
                    sequence = ++currentSequence,
                    sessionId = sessionId
                )

                // Send request
                Log.d(TAG, "Sending: ${enrichedRequest.method} ${enrichedRequest.uri}")
                output.write(enrichedRequest.toByteArray())
                output.flush()

                // Read response
                val responseBuffer = mutableListOf<Byte>()
                val buffer = ByteArray(4096)
                var bytesRead: Int

                // Read until we have complete response
                while (input.read(buffer).also { bytesRead = it } > 0) {
                    responseBuffer.addAll(buffer.take(bytesRead).toList())

                    // Check if we have complete response
                    val responseBytes = responseBuffer.toByteArray()
                    RTSPResponse.fromByteArray(responseBytes)?.let { response ->
                        // Update session ID if present
                        response.sessionId?.let { sessionId = it }

                        continuation.resume(response)
                        return@suspendCoroutine
                    }
                }

                continuation.resume(null)
            } catch (e: Exception) {
                Log.e(TAG, "Failed to send request", e)
                continuation.resume(null)
            }
        }
    }

    fun disconnect() {
        try {
            outputStream?.close()
            inputStream?.close()
            socket?.close()
        } catch (e: Exception) {
            Log.e(TAG, "Error closing connection", e)
        } finally {
            outputStream = null
            inputStream = null
            socket = null
            sessionId = null
        }
    }

    val isConnected: Boolean
        get() = socket?.isConnected == true && socket?.isClosed == false
}
