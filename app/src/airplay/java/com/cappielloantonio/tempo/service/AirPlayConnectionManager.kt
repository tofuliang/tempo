package com.cappielloantonio.tempo.service

import android.util.Log
import com.cappielloantonio.tempo.model.AirPlayDevice
import com.cappielloantonio.tempo.model.AirPlayDeviceCapabilities
import com.cappielloantonio.tempo.protocol.rtsp.RTSPClient
import com.cappielloantonio.tempo.protocol.rtsp.RTSPRequest
import com.cappielloantonio.tempo.protocol.rtsp.RTSPResponse
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

class AirPlayConnectionManager {
    companion object {
        private const val TAG = "AirPlayConnectionManager"
    }

    private var currentClient: RTSPClient? = null
    private var currentDevice: AirPlayDevice? = null

    suspend fun connect(device: AirPlayDevice): Result<AirPlaySession> {
        return withContext(Dispatchers.IO) {
            try {
                // Create RTSP client
                val client = RTSPClient(device.host, device.port)

                // Establish connection
                if (!client.connect()) {
                    return@withContext Result.failure(Exception("Failed to connect"))
                }

                // Send OPTIONS request
                val optionsRequest = RTSPRequest(
                    method = RTSPRequest.RTSPMethod.OPTIONS,
                    uri = "*"
                )

                val optionsResponse = client.send(optionsRequest)
                if (optionsResponse == null || !optionsResponse.isSuccess) {
                    client.disconnect()
                    return@withContext Result.failure(Exception("OPTIONS failed"))
                }

                // TODO: Implement authentication if device requires it
                // TODO: Query device capabilities
                // TODO: Setup stream

                currentClient = client
                currentDevice = device

                val session = AirPlaySession(
                    client = client,
                    device = device,
                    capabilities = AirPlayDeviceCapabilities()
                )

                Result.success(session)
            } catch (e: Exception) {
                Log.e(TAG, "Connection failed", e)
                Result.failure(e)
            }
        }
    }

    fun disconnect() {
        currentClient?.disconnect()
        currentClient = null
        currentDevice = null
    }

    val isConnected: Boolean
        get() = currentClient?.isConnected == true
}

data class AirPlaySession(
    val client: RTSPClient,
    val device: AirPlayDevice,
    val capabilities: AirPlayDeviceCapabilities
)
