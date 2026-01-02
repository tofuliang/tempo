package com.cappielloantonio.tempo.service

import android.util.Log
import com.cappielloantonio.tempo.model.AirPlayDevice
import com.cappielloantonio.tempo.model.AirPlayDeviceCapabilities
import com.cappielloantonio.tempo.protocol.rtsp.RTSPClient
import com.cappielloantonio.tempo.protocol.rtsp.RTSPRequest
import com.cappielloantonio.tempo.protocol.rtsp.RTSPResponse
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.util.UUID

class AirPlayConnectionManager {
    companion object {
        private const val TAG = "AirPlayConnectionManager"
        private const val RTSP_PORT = 6001  // Default AirPlay/RAOP port
    }

    private var currentClient: RTSPClient? = null
    private var currentDevice: AirPlayDevice? = null
    private var sessionInfo: AirPlaySessionInfo? = null

    suspend fun connect(device: AirPlayDevice): Result<AirPlaySession> {
        return withContext(Dispatchers.IO) {
            try {
                Log.i(TAG, "Connecting to ${device.name} at ${device.host}:${device.port}")

                // Create RTSP client
                val client = RTSPClient(device.host, device.port)

                // Establish TCP connection
                if (!client.connect()) {
                    return@withContext Result.failure(Exception("Failed to connect to ${device.host}:${device.port}"))
                }

                // Step 1: OPTIONS - Check supported methods
                Log.d(TAG, "Sending OPTIONS request...")
                val optionsResponse = sendRequest(client, RTSPRequest(
                    method = RTSPRequest.RTSPMethod.OPTIONS,
                    uri = "*"
                ))
                if (optionsResponse == null || !optionsResponse.isSuccess) {
                    client.disconnect()
                    return@withContext Result.failure(Exception("OPTIONS failed: ${optionsResponse?.statusCode}"))
                }
                Log.d(TAG, "OPTIONS successful - Public: ${optionsResponse.headers["Public"]}")

                // Step 2: ANNOUNCE - Tell receiver about our stream format
                Log.d(TAG, "Sending ANNOUNCE request...")
                val sessionId = UUID.randomUUID().toString()
                val announceResponse = sendRequest(client, createAnnounceRequest(sessionId))
                if (announceResponse == null || !announceResponse.isSuccess) {
                    client.disconnect()
                    return@withContext Result.failure(Exception("ANNOUNCE failed: ${announceResponse?.statusCode}"))
                }
                Log.d(TAG, "ANNOUNCE successful")

                // Step 3: SETUP - Establish streaming session
                Log.d(TAG, "Sending SETUP request...")
                val setupResponse = sendRequest(client, createSetupRequest(sessionId))
                if (setupResponse == null || !setupResponse.isSuccess) {
                    client.disconnect()
                    return@withContext Result.failure(Exception("SETUP failed: ${setupResponse?.statusCode}"))
                }

                // Extract session info from SETUP response
                val setupSessionInfo = parseSetupResponse(setupResponse, sessionId)
                this.sessionInfo = setupSessionInfo
                Log.d(TAG, "SETUP successful - Session: $sessionId")
                Log.d(TAG, "Server port: ${setupSessionInfo.serverPort}, Session ID: ${setupSessionInfo.sessionId}")

                // Step 4: RECORD - Start streaming
                Log.d(TAG, "Sending RECORD request...")
                val recordResponse = sendRequest(client, createRecordRequest(sessionId))
                if (recordResponse == null || !recordResponse.isSuccess) {
                    // RECORD might fail for some devices, but connection is still valid
                    Log.w(TAG, "RECORD failed but continuing: ${recordResponse?.statusCode}")
                } else {
                    Log.d(TAG, "RECORD successful - Streaming started!")
                }

                currentClient = client
                currentDevice = device

                val session = AirPlaySession(
                    client = client,
                    device = device,
                    capabilities = AirPlayDeviceCapabilities(),
                    sessionId = sessionId,
                    serverPort = sessionInfo?.serverPort ?: 6001
                )

                Log.i(TAG, "Successfully connected to ${device.name}")
                Result.success(session)
            } catch (e: Exception) {
                Log.e(TAG, "Connection failed", e)
                Result.failure(e)
            }
        }
    }

    private suspend fun sendRequest(client: RTSPClient, request: RTSPRequest): RTSPResponse? {
        return try {
            client.send(request)
        } catch (e: Exception) {
            Log.e(TAG, "Request failed: ${request.method}", e)
            null
        }
    }

    private fun createAnnounceRequest(sessionId: String): RTSPRequest {
        return RTSPRequest(
            method = RTSPRequest.RTSPMethod.ANNOUNCE,
            uri = "/${sessionId}",
            headers = mapOf(
                "Content-Type" to "application/sdp",
                "CSeq" to "1"
            )
        )
    }

    private fun createSetupRequest(sessionId: String): RTSPRequest {
        return RTSPRequest(
            method = RTSPRequest.RTSPMethod.SETUP,
            uri = "/${sessionId}",
            headers = mapOf(
                "Transport" to "RTP/AVP;unicast;client_port=6000",
                "CSeq" to "2"
            )
        )
    }

    private fun createRecordRequest(sessionId: String): RTSPRequest {
        return RTSPRequest(
            method = RTSPRequest.RTSPMethod.RECORD,
            uri = "/${sessionId}",
            headers = mapOf(
                "Range" to "npt=0-",
                "CSeq" to "3"
            )
        )
    }

    private fun parseSetupResponse(response: RTSPResponse, sessionId: String): AirPlaySessionInfo {
        val transportHeader = response.headers["Transport"] ?: ""
        // Parse server port from Transport header
        // Format: "RTP/AVP;unicast;server_port=6001"
        val serverPort = Regex("server_port=(\\d+)").find(transportHeader)?.groupValues?.get(1)?.toInt() ?: 6001

        return AirPlaySessionInfo(
            sessionId = sessionId,
            serverPort = serverPort
        )
    }

    fun disconnect() {
        try {
            // Send TEARDOWN if we have an active session
            currentClient?.let { client ->
                sessionInfo?.let { session ->
                    // TEARDOWN is optional and may fail
                    try {
                        // Note: Can't call suspend function here, so we create request directly
                        val teardown = RTSPRequest(
                            method = RTSPRequest.RTSPMethod.TEARDOWN,
                            uri = "/${session.sessionId}",
                            headers = mapOf("CSeq" to "4")
                        )
                        client.send(teardown)
                    } catch (e: Exception) {
                        Log.w(TAG, "TEARDOWN failed (non-critical)", e)
                    }
                }
                client.disconnect()
            }
        } catch (e: Exception) {
            Log.e(TAG, "Error during disconnect", e)
        } finally {
            currentClient = null
            currentDevice = null
            sessionInfo = null
        }
    }

    val isConnected: Boolean
        get() = currentClient?.isConnected == true
}

data class AirPlaySession(
    val client: RTSPClient,
    val device: AirPlayDevice,
    val capabilities: AirPlayDeviceCapabilities,
    val sessionId: String,
    val serverPort: Int
)

data class AirPlaySessionInfo(
    val sessionId: String,
    val serverPort: Int
)
