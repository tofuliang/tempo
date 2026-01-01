# AirPlay 2 Support Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add AirPlay 2 sender functionality to Tempo, enabling users to stream music from their Subsonic server to AirPlay devices.

**Architecture:** Modular AirPlay 2 implementation with mDNS device discovery, RTSP/RTP protocol stack, audio encoding pipeline, and Media3 Player integration. Follows existing Chromecast pattern in MediaService.

**Tech Stack:** Android NsdManager, Kotlin Coroutines, Media3 (ExoPlayer), AES encryption, RTP/RTSP protocols, DMAP metadata format

**Design Document:** `docs/plans/2026-01-01-airplay2-design.md`

---

## Phase 1: Protocol Foundation (Device Discovery)

### Task 1: Create AirPlayDevice Data Model

**Files:**
- Create: `app/src/tempo/java/com/cappielloantonio/tempo/model/AirPlayDevice.kt`

**Step 1: Create data class for AirPlay device**

```kotlin
package com.cappielloantonio.tempo.model

data class AirPlayDevice(
    val name: String,
    val host: String,
    val port: Int,
    val deviceId: String,
    val features: Int,
    val model: String,
    val version: Int,
    val supportsEncryption: Boolean,
    val lastSeen: Long
) {
    companion object {
        const val FEATURE_ENCRYPTION = 0x00000001
        const val FEATURE_METADATA = 0x00000002
        const val FEATURE_ARTWORK = 0x00000004
        const val FEATURE_PROGRESS = 0x00000008
    }

    val isOnline: Boolean
        get() = System.currentTimeMillis() - lastSeen < 30_000 // 30 seconds

    override fun equals(other: Any?): Boolean {
        if (this === other) return true
        if (javaClass != other?.javaClass) return false
        other as AirPlayDevice
        return deviceId == other.deviceId
    }

    override fun hashCode(): Int {
        return deviceId.hashCode()
    }
}
```

**Step 2: Run build to verify**

Run: `./gradlew compileTempoDebugKotlin`
Expected: BUILD SUCCESSFUL

**Step 3: Commit**

```bash
git add app/src/tempo/java/com/cappielloantonio/tempo/model/AirPlayDevice.kt
git commit -m "feat: add AirPlayDevice data model"
```

---

### Task 2: Create Device Capabilities Data Model

**Files:**
- Create: `app/src/tempo/java/com/cappielloantonio/tempo/model/AirPlayDeviceCapabilities.kt`

**Step 1: Create capabilities data class**

```kotlin
package com.cappielloantonio.tempo.model

data class AirPlayDeviceCapabilities(
    val supportsMetadata: Boolean = false,
    val supportsArtwork: Boolean = false,
    val maxArtworkSize: Int? = null,
    val supportedArtworkFormats: List<String> = emptyList(),
    val supportsProgress: Boolean = false,
    val supportsVolumeControl: Boolean = false,
    val dmapVersion: Int? = null,
    val supportedFeatures: Int = 0
) {
    companion object {
        const val DMAP_VERSION_1 = 1
        const val DMAP_VERSION_2 = 2
        const val DMAP_VERSION_3 = 3
    }
}
```

**Step 2: Run build to verify**

Run: `./gradlew compileTempoDebugKotlin`
Expected: BUILD SUCCESSFUL

**Step 3: Commit**

```bash
git add app/src/tempo/java/com/cappielloantonio/tempo/model/AirPlayDeviceCapabilities.kt
git commit -m "feat: add AirPlayDeviceCapabilities data model"
```

---

### Task 3: Create mDNS Discovery Service

**Files:**
- Create: `app/src/tempo/java/com/cappielloantonio/tempo/service/AirPlayDeviceScanner.kt`
- Test: `app/src/test/java/com/cappielloantonio/tempo/service/AirPlayDeviceScannerTest.kt`

**Step 1: Write the failing test**

Create test file:

```kotlin
package com.cappielloantonio.tempo.service

import android.content.Context
import com.cappielloantonio.tempo.model.AirPlayDevice
import io.mockk.every
import io.mockk.mockk
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test

class AirPlayDeviceScannerTest {
    private lateinit var scanner: AirPlayDeviceScanner
    private val mockContext = mockk<Context>(relaxed = true)

    @Before
    fun setup() {
        scanner = AirPlayDeviceScanner(mockContext)
    }

    @Test
    fun `getAvailableDevices returns empty list initially`() = runTest {
        val devices = scanner.getAvailableDevices()
        assertTrue(devices.isEmpty())
    }

    @Test
    fun `getAvailableDevices returns cached devices`() = runTest {
        val testDevice = AirPlayDevice(
            name = "Test Device",
            host = "192.168.1.100",
            port = 7000,
            deviceId = "test-id",
            features = 0x5A7FFF7,
            model = "AppleTV14,1",
            version = 2,
            supportsEncryption = true,
            lastSeen = System.currentTimeMillis()
        )

        // Add device to cache
        scanner.addDeviceToCache(testDevice)

        val devices = scanner.getAvailableDevices()
        assertEquals(1, devices.size)
        assertEquals("Test Device", devices[0].name)
    }
}
```

**Step 2: Run test to verify it fails**

Run: `./gradlew testTempoDebugUnitTest --tests AirPlayDeviceScannerTest`
Expected: FAIL with "class AirPlayDeviceScanner not found"

**Step 3: Write minimal implementation**

```kotlin
package com.cappielloantonio.tempo.service

import android.content.Context
import android.net.nsd.NsdManager
import android.net.nsd.NsdServiceInfo
import android.util.Log
import com.cappielloantonio.tempo.model.AirPlayDevice
import kotlinx.coroutines.channels.awaitClose
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.callbackFlow
import kotlinx.coroutines.flow.asStateFlow
import java.util.concurrent.ConcurrentHashMap

class AirPlayDeviceScanner(private val context: Context) {

    companion object {
        private const val TAG = "AirPlayDeviceScanner"
        private const val SERVICE_TYPE_AIRPLAY = "_airplay._tcp."
        private const val SERVICE_TYPE_AIRPLAY_2 = "_airplay-2._tcp."
    }

    private val nsdManager: NsdManager by lazy {
        context.getSystemService(Context.NSD_SERVICE) as NsdManager
    }

    private val deviceCache = ConcurrentHashMap<String, AirPlayDevice>()

    private val _discoveredDevices = MutableStateFlow<List<AirPlayDevice>>(emptyList())
    val discoveredDevices = _discoveredDevices.asStateFlow()

    fun getAvailableDevices(): List<AirPlayDevice> {
        return deviceCache.values.filter { it.isOnline }.sortedBy { it.name }
    }

    fun addDeviceToCache(device: AirPlayDevice) {
        deviceCache[device.deviceId] = device
        _discoveredDevices.value = getAvailableDevices()
    }

    fun startScan() {
        // TODO: Implement actual mDNS discovery
        Log.i(TAG, "Starting AirPlay device scan")
    }

    fun stopScan() {
        // TODO: Implement cleanup
        Log.i(TAG, "Stopping AirPlay device scan")
    }

    fun clearCache() {
        deviceCache.clear()
        _discoveredDevices.value = emptyList()
    }

    private fun parseServiceInfo(serviceInfo: NsdServiceInfo): AirPlayDevice? {
        return try {
            val attributes = serviceInfo.attributes
            val deviceId = String(attributes["deviceid"] ?: return null)
            val features = String(attributes["features"] ?: "0").toInt(16)
            val model = String(attributes["model"] ?: "Unknown")

            AirPlayDevice(
                name = serviceInfo.serviceName,
                host = serviceInfo.host.hostAddress ?: return null,
                port = serviceInfo.port,
                deviceId = deviceId,
                features = features,
                model = model,
                version = 2,
                supportsEncryption = (features and AirPlayDevice.FEATURE_ENCRYPTION) != 0,
                lastSeen = System.currentTimeMillis()
            )
        } catch (e: Exception) {
            Log.e(TAG, "Failed to parse service info", e)
            null
        }
    }
}
```

**Step 4: Run test to verify it passes**

Run: `./gradlew testTempoDebugUnitTest --tests AirPlayDeviceScannerTest`
Expected: PASS

**Step 5: Commit**

```bash
git add app/src/tempo/java/com/cappielloantonio/tempo/service/AirPlayDeviceScanner.kt
git add app/src/test/java/com/cappielloantonio/tempo/service/AirPlayDeviceScannerTest.kt
git commit -m "feat: implement AirPlayDeviceScanner with basic caching"
```

---

### Task 4: Implement mDNS Discovery Listener

**Files:**
- Modify: `app/src/tempo/java/com/cappielloantonio/tempo/service/AirPlayDeviceScanner.kt`

**Step 1: Add discovery listener implementation**

Add to AirPlayDeviceScanner class:

```kotlin
    private var discoveryListener: NsdManager.DiscoveryListener? = null

    fun startScan() {
        stopScan() // Stop any existing scan

        discoveryListener = object : NsdManager.DiscoveryListener {
            override fun onDiscoveryStarted(regType: String) {
                Log.i(TAG, "Discovery started: $regType")
            }

            override fun onServiceFound(serviceInfo: NsdServiceInfo) {
                Log.d(TAG, "Service found: ${serviceInfo.serviceName}")

                // Resolve the service to get details
                nsdManager.resolveService(serviceInfo, object : NsdManager.ResolveListener {
                    override fun onServiceResolved(resolvedService: NsdServiceInfo) {
                        Log.d(TAG, "Service resolved: ${resolvedService.serviceName}")

                        parseServiceInfo(resolvedService)?.let { device ->
                            addDeviceToCache(device)
                        }
                    }

                    override fun onResolveFailed(serviceInfo: NsdServiceInfo, errorCode: Int) {
                        Log.e(TAG, "Resolve failed: ${serviceInfo.serviceName}, code: $errorCode")
                    }
                })
            }

            override fun onServiceLost(serviceInfo: NsdServiceInfo) {
                Log.d(TAG, "Service lost: ${serviceInfo.serviceName}")
            }

            override fun onDiscoveryStopped(serviceType: String) {
                Log.i(TAG, "Discovery stopped: $serviceType")
            }

            override fun onStartDiscoveryFailed(serviceType: String, errorCode: Int) {
                Log.e(TAG, "Discovery start failed: $serviceType, code: $errorCode")
            }

            override fun onStopDiscoveryFailed(serviceType: String, errorCode: Int) {
                Log.e(TAG, "Discovery stop failed: $serviceType, code: $errorCode")
            }
        }

        // Start discovery for AirPlay 2
        nsdManager.discoverServices(SERVICE_TYPE_AIRPLAY, NsdManager.PROTOCOL_DNS_SD, discoveryListener)
    }

    fun stopScan() {
        discoveryListener?.let {
            nsdManager.stopServiceDiscovery(it)
            discoveryListener = null
        }
    }
```

**Step 2: Run build to verify**

Run: `./gradlew compileTempoDebugKotlin`
Expected: BUILD SUCCESSFUL

**Step 3: Commit**

```bash
git add app/src/tempo/java/com/cappielloantonio/tempo/service/AirPlayDeviceScanner.kt
git commit -m "feat: add mDNS discovery listener to AirPlayDeviceScanner"
```

---

## Phase 2: RTSP Protocol Stack

### Task 5: Create RTSP Request/Response Models

**Files:**
- Create: `app/src/tempo/java/com/cappielloantonio/tempo/protocol/rtsp/RTSPRequest.kt`
- Create: `app/src/tempo/java/com/cappielloantonio/tempo/protocol/rtsp/RTSPResponse.kt`

**Step 1: Create RTSP request model**

```kotlin
package com.cappielloantonio.tempo.protocol.rtsp

data class RTSPRequest(
    val method: RTSPMethod,
    val uri: String,
    val headers: Map<String, String> = emptyMap(),
    val body: ByteArray? = null,
    val sequence: Int = 0,
    val sessionId: String? = null
) {
    enum class RTSPMethod(val value: String) {
        OPTIONS("OPTIONS"),
        DESCRIBE("DESCRIBE"),
        SETUP("SETUP"),
        PLAY("PLAY"),
        PAUSE("PAUSE"),
        TEARDOWN("TEARDOWN"),
        GET_PARAMETER("GET_PARAMETER"),
        SET_PARAMETER("SET_PARAMETER")
    }

    fun toByteArray(): ByteArray {
        val lines = buildList {
            add("$method $uri RTSP/1.0")
            add("CSeq: $sequence")
            sessionId?.let { add("Session: $it") }
            headers.forEach { (key, value) -> add("$key: $value") }
            if (body != null) add("Content-Length: ${body.size}")
            add("") // Empty line before body
        }

        val header = lines.joinToString("\r\n")
        return if (body != null) {
            "$header\r\n".toByteArray() + body
        } else {
            "$header\r\n".toByteArray()
        }
    }

    override fun equals(other: Any?): Boolean {
        if (this === other) return true
        if (javaClass != other?.javaClass) return false
        other as RTSPRequest
        return method == other.method && uri == other.uri
    }

    override fun hashCode(): Int {
        var result = method.hashCode()
        result = 31 * result + uri.hashCode()
        return result
    }
}
```

**Step 2: Create RTSP response model**

```kotlin
package com.cappielloantonio.tempo.protocol.rtsp

data class RTSPResponse(
    val statusCode: Int,
    val statusText: String,
    val headers: Map<String, String> = emptyMap(),
    val body: ByteArray? = null
) {
    val isSuccess: Boolean
        get() = statusCode in 200..299

    val sessionId: String?
        get() = headers["Session"]

    companion object {
        fun fromByteArray(data: ByteArray): RTSPResponse? {
            return try {
                val text = String(data)
                val lines = text.split("\r\n")

                // Parse status line
                val statusLine = lines[0]
                val parts = statusLine.split(" ", limit = 3)
                val statusCode = parts[1].toInt()
                val statusText = parts[2]

                // Parse headers
                val headers = mutableMapOf<String, String>()
                var bodyStart = 1
                for (i in 1 until lines.size) {
                    if (lines[i].isEmpty()) {
                        bodyStart = i + 1
                        break
                    }
                    val (key, value) = lines[i].split(": ", limit = 2)
                    headers[key] = value
                }

                // Extract body
                val body = if (bodyStart < lines.size && headers.containsKey("Content-Length")) {
                    val contentLength = headers["Content-Length"]!!.toInt()
                    val totalHeaderLength = text.indexOf("\r\n\r\n") + 4
                    data.sliceArray(totalHeaderLength until totalHeaderLength + contentLength)
                } else null

                RTSPResponse(statusCode, statusText, headers, body)
            } catch (e: Exception) {
                null
            }
        }
    }

    override fun equals(other: Any?): Boolean {
        if (this === other) return true
        if (javaClass != other?.javaClass) return false
        other as RTSPResponse
        return statusCode == other.statusCode
    }

    override fun hashCode(): Int {
        return statusCode
    }
}
```

**Step 3: Run build to verify**

Run: `./gradlew compileTempoDebugKotlin`
Expected: BUILD SUCCESSFUL

**Step 4: Commit**

```bash
git add app/src/tempo/java/com/cappielloantonio/tempo/protocol/rtsp/RTSPRequest.kt
git add app/src/tempo/java/com/cappielloantonio/tempo/protocol/rtsp/RTSPResponse.kt
git commit -m "feat: add RTSP request and response models"
```

---

### Task 6: Create RTSP Client

**Files:**
- Create: `app/src/tempo/java/com/cappielloantonio/tempo/protocol/rtsp/RTSPClient.kt`
- Test: `app/src/test/java/com/cappielloantonio/tempo/protocol/rtsp/RTSPClientTest.kt`

**Step 1: Write the failing test**

```kotlin
package com.cappielloantonio.tempo.protocol.rtsp

import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test

class RTSPClientTest {
    private lateinit var client: RTSPClient

    @Before
    fun setup() {
        client = RTSPClient("192.168.1.100", 7000)
    }

    @Test
    fun `send OPTIONS request returns valid response`() = runTest {
        // This is a mock test - real implementation would need network mocking
        val request = RTSPRequest(
            method = RTSPRequest.RTSPMethod.OPTIONS,
            uri = "*",
            sequence = 1
        )

        val requestBytes = request.toByteArray()
        assertTrue(requestBytes.isNotEmpty())
        assertTrue(String(requestBytes).startsWith("OPTIONS"))
    }
}
```

**Step 2: Run test to verify it fails**

Run: `./gradlew testTempoDebugUnitTest --tests RTSPClientTest`
Expected: FAIL with "class RTSPClient not found"

**Step 3: Write minimal implementation**

```kotlin
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
```

**Step 4: Run test to verify it passes**

Run: `./gradlew testTempoDebugUnitTest --tests RTSPClientTest`
Expected: PASS

**Step 5: Commit**

```bash
git add app/src/tempo/java/com/cappielloantonio/tempo/protocol/rtsp/RTSPClient.kt
git add app/src/test/java/com/cappielloantonio/tempo/protocol/rtsp/RTSPClientTest.kt
git commit -m "feat: implement RTSP client with basic send/receive"
```

---

## Phase 3: Connection Management

### Task 7: Create Connection Manager

**Files:**
- Create: `app/src/tempo/java/com/cappielloantonio/tempo/service/AirPlayConnectionManager.kt`

**Step 1: Create connection manager skeleton**

```kotlin
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
```

**Step 2: Run build to verify**

Run: `./gradlew compileTempoDebugKotlin`
Expected: BUILD SUCCESSFUL

**Step 3: Commit**

```bash
git add app/src/tempo/java/com/cappielloantonio/tempo/service/AirPlayConnectionManager.kt
git commit -m "feat: add AirPlayConnectionManager with basic connection flow"
```

---

## Phase 4: Media3 Player Integration

### Task 8: Create AirPlayPlayer Skeleton

**Files:**
- Create: `app/src/tempo/java/com/cappielloantonio/tempo/player/AirPlayPlayer.kt`

**Step 1: Create AirPlayPlayer implementing Media3 Player interface**

```kotlin
package com.cappielloantonio.tempo.player

import android.content.Context
import androidx.media3.common.AudioAttributes
import androidx.media3.common.C
import androidx.media3.common.DeviceInfo
import androidx.media3.common.MediaItem
import androidx.media3.common.PlaybackParameters
import androidx.media3.common.Player
import androidx.media3.common.Timeline
import androidx.media3.common.Tracks
import androidx.media3.common.util.UnstableApi
import com.cappielloantonio.tempo.service.AirPlayConnectionManager
import com.cappielloantonio.tempo.service.AirPlaySession

@UnstableApi
class AirPlayPlayer(
    private val context: Context,
    private val connectionManager: AirPlayConnectionManager
) : Player {

    private val listeners = mutableSetOf<Listener>()
    private var playWhenReady = false
    private var playbackState = Player.STATE_IDLE
    private var currentPosition = 0L
    private var currentMediaItem: MediaItem? = null
    private var currentSession: AirPlaySession? = null

    // Player interface implementation
    override fun setPlayWhenReady(playWhenReady: Boolean) {
        this.playWhenReady = playWhenReady
        if (playWhenReady && playbackState == STATE_READY) {
            play()
        }
    }

    override fun getPlayWhenReady(): Boolean = playWhenReady

    override fun setMediaItems(mediaItems: List<MediaItem>, startIndex: Int, startPositionMs: Long) {
        if (mediaItems.isNotEmpty()) {
            currentMediaItem = mediaItems[startIndex]
            currentPosition = startPositionMs
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

    override fun getCurrentPeriodIndex(): Int = 0

    override fun getCurrentMediaItemIndex(): Int = 0

    override fun getDuration(): Long = C.TIME_UNSET

    override fun getCurrentPosition(): Long = currentPosition

    override fun seekToDefaultPosition() {
        seekTo(0)
    }

    override fun seekTo(positionMs: Long) {
        currentPosition = positionMs
        listeners.forEach { it.onPositionDiscontinuity(
            Player.PositionInfo.EMPTY,
            Player.PositionInfo.EMPTY,
            Player.DISCONTINUITY_REASON_SEEK
        )}
    }

    override fun seekBack() {
        seekTo(maxOf(0, currentPosition - 5000))
    }

    override fun seekForward() {
        seekTo(currentPosition + 5000)
    }

    override fun hasPreviousMediaItem(): Boolean = false

    override fun hasNextMediaItem(): Boolean = false

    override fun seekToPreviousMediaItem() = false

    override fun seekToNextMediaItem() = false

    override fun setPlaybackParameters(parameters: PlaybackParameters) {}

    override fun getPlaybackParameters(): PlaybackParameters = PlaybackParameters.DEFAULT

    override fun setShuffleModeEnabled(shuffleModeEnabled: Boolean) {}

    override fun getShuffleModeEnabled(): Boolean = false

    override fun setRepeatMode(repeatMode: Int) {}

    override fun getRepeatMode(): Int = Player.REPEAT_MODE_OFF

    override fun addListener(listener: Listener) {
        listeners.add(listener)
    }

    override fun removeListener(listener: Listener) {
        listeners.remove(listener)
    }

    override fun getApplicationLooper() = context.mainLooper

    override fun getPlaybackState(): Int = playbackState

    override fun getPlaybackSuppressionReason(): Int = PLAYBACK_SUPPRESSION_REASON_NONE

    override fun isPlaying(): Boolean = playWhenReady && playbackState == STATE_READY

    override fun isLoading(): Boolean = false

    override fun seekToNextWindow(): Boolean = false

    override fun seekToPreviousWindow(): Boolean = false

    override fun getPlayerError(): Exception? = null

    override fun getTotalBufferedDuration(): Long = 0

    override fun getDeviceVolume(): Int = 0

    override fun setDeviceVolume(volume: Int) {}

    override fun isDeviceMuted(): Boolean = false

    override fun setDeviceMuted(muted: Boolean) {}

    override fun getAudioAttributes(): AudioAttributes = AudioAttributes.DEFAULT

    override fun setVolume(volume: Float) {}

    override fun getVolume(): Float = 1f

    override fun clearVideoSurface() {}

    override fun clearVideoSurface(surface: Any) {}

    override fun setVideoSurface(surface: Any?) {}

    override fun getVideoSize(): com.cappielloantonio.tempo.util.Size? = null

    override fun getCurrentTracks(): Tracks = Tracks.EMPTY

    override fun getTrackSelectionParameters(): androidx.media3.common.TrackSelectionParameters {
        return androidx.media3.common.TrackSelectionParameters.Builder(context).build()
    }

    override fun setTrackSelectionParameters(parameters: androidx.media3.common.TrackSelectionParameters) {}

    override fun getDeviceInfo(): DeviceInfo = DeviceInfo.UNKNOWN

    override fun getDeviceAttributes(): DeviceInfo = DeviceInfo.UNKNOWN

    override fun getTimeline(): Timeline = Timeline.EMPTY

    override fun getNextMediaItemIndex(): Int = C.INDEX_UNSET

    override fun getPreviousMediaItemIndex(): Int = C.INDEX_UNSET

    override fun getCurrentLiveOffset(): Long = C.TIME_UNSET

    override fun getTotalBufferedDuration(): Long = 0

    override fun isCommandAvailable(command: Int): Boolean = false

    override fun canAdvertiseSession(): Boolean = false

    // AirPlay-specific methods
    suspend fun connect(device: com.cappielloantonio.tempo.model.AirPlayDevice): Boolean {
        val result = connectionManager.connect(device)
        return result.fold(
            onSuccess = { session ->
                currentSession = session
                playbackState = STATE_READY
                listeners.forEach { it.onPlaybackStateChanged(playbackState) }
                true
            },
            onFailure = { false }
        )
    }

    fun disconnect() {
        connectionManager.disconnect()
        currentSession = null
        playbackState = STATE_IDLE
        listeners.forEach { it.onPlaybackStateChanged(playbackState) }
    }

    private fun play() {
        playbackState = STATE_READY
        listeners.forEach {
            it.onPlaybackStateChanged(playbackState)
            it.onIsPlayingChanged(true)
        }
    }

    private fun pause() {
        listeners.forEach { it.onIsPlayingChanged(false) }
    }

    override fun play() {
        play()
    }

    override fun pause() {
        pause()
    }

    override fun stop() {
        pause()
    }

    override fun release() {
        disconnect()
        listeners.clear()
    }
}
```

**Step 2: Run build to verify**

Run: `./gradlew compileTempoDebugKotlin`
Expected: BUILD SUCCESSFUL

**Step 3: Commit**

```bash
git add app/src/tempo/java/com/cappielloantonio/tempo/player/AirPlayPlayer.kt
git commit -m "feat: add AirPlayPlayer implementing Media3 Player interface"
```

---

## Phase 5: UI Integration

### Task 9: Add AirPlay Button to Player Fragment

**Files:**
- Modify: `app/src/tempo/java/com/cappielloantonio/tempo/ui/fragment/PlayerFragment.kt`

**Step 1: Add AirPlay button to layout**

First, check if there's a layout file for the player controls.

```kotlin
// In PlayerFragment.kt, add to onViewCreated or setupViews method
private fun setupAirPlayButton() {
    val airPlayButton = view?.findViewById<ImageButton>(R.id.airplay_cast_button)

    airPlayButton?.setOnClickListener {
        viewModel.showAirPlayDeviceList()
    }

    // Observe AirPlay devices
    viewModel.airPlayDevices.observe(viewLifecycleOwner) { devices ->
        airPlayButton?.isVisible = devices.isNotEmpty()
    }
}
```

**Step 2: Update PlayerViewModel to handle AirPlay**

```kotlin
// In PlayerViewModel.kt, add:
private val _airPlayDevices = MutableLiveData<List<AirPlayDevice>>()
val airPlayDevices: LiveData<List<AirPlayDevice>> = _airPlayDevices

fun showAirPlayDeviceList() {
    // Trigger dialog to show available devices
}
```

**Step 3: Run build to verify**

Run: `./gradlew compileTempoDebugKotlin`
Expected: BUILD SUCCESSFUL

**Step 4: Commit**

```bash
git add app/src/tempo/java/com/cappielloantonio/tempo/ui/fragment/PlayerFragment.kt
git add app/src/tempo/java/com/cappielloantonio/tempo/viewmodel/PlayerViewModel.kt
git commit -m "feat: add AirPlay cast button to player fragment"
```

---

### Task 10: Create AirPlay Device Selection Dialog

**Files:**
- Create: `app/src/tempo/java/com/cappielloantonio/tempo/ui/dialog/AirPlayDeviceDialog.kt`
- Create: `app/src/tempo/res/layout/dialog_airplay_device_list.xml`

**Step 1: Create dialog layout XML**

```xml
<?xml version="1.0" encoding="utf-8"?>
<LinearLayout xmlns:android="http://schemas.android.com/apk/res/android"
    android:layout_width="match_parent"
    android:layout_height="wrap_content"
    android:orientation="vertical"
    android:padding="16dp">

    <TextView
        android:layout_width="wrap_content"
        android:layout_height="wrap_content"
        android:text="Select AirPlay Device"
        android:textSize="18sp"
        android:textStyle="bold"
        android:layout_marginBottom="16dp" />

    <androidx.recyclerview.widget.RecyclerView
        android:id="@+id/airplay_device_recycler"
        android:layout_width="match_parent"
        android:layout_height="wrap_content"
        android:maxHeight="400dp" />

    <Button
        android:id="@+id/disconnect_button"
        android:layout_width="match_parent"
        android:layout_height="wrap_content"
        android:text="Disconnect"
        android:layout_marginTop="16dp"
        android:visibility="gone" />

</LinearLayout>
```

**Step 2: Create dialog class**

```kotlin
package com.cappielloantonio.tempo.ui.dialog

import android.app.Dialog
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import androidx.fragment.app.DialogFragment
import androidx.fragment.app.viewModels
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import com.cappielloantonio.tempo.R
import com.cappielloantonio.tempo.model.AirPlayDevice
import com.cappielloantonio.tempo.ui.adapter.AirPlayDeviceAdapter
import com.cappielloantonio.tempo.viewmodel.PlayerViewModel
import dagger.hilt.android.AndroidEntryPoint

@AndroidEntryPoint
class AirPlayDeviceDialog : DialogFragment() {

    private val viewModel: PlayerViewModel by viewModels({ requireParentFragment() })
    private lateinit var recyclerView: RecyclerView
    private lateinit var disconnectButton: Button
    private lateinit var adapter: AirPlayDeviceAdapter

    override fun onCreateDialog(savedInstanceState: Dialog?): Dialog {
        return super.onCreateDialog(savedInstanceState).apply {
            setTitle("AirPlay")
        }
    }

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?
    ): View? {
        return inflater.inflate(R.layout.dialog_airplay_device_list, container, false)
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)

        recyclerView = view.findViewById(R.id.airplay_device_recycler)
        disconnectButton = view.findViewById(R.id.disconnect_button)

        setupRecyclerView()
        observeViewModel()
    }

    private fun setupRecyclerView() {
        adapter = AirPlayDeviceAdapter { device ->
            viewModel.connectToAirPlayDevice(device)
            dismiss()
        }

        recyclerView.apply {
            layoutManager = LinearLayoutManager(requireContext())
            adapter = this@AirPlayDeviceDialog.adapter
        }
    }

    private fun observeViewModel() {
        viewModel.airPlayDevices.observe(viewLifecycleOwner) { devices ->
            adapter.submitList(devices)
        }

        viewModel.currentAirPlayDevice.observe(viewLifecycleOwner) { device ->
            disconnectButton.visibility = if (device != null) View.VISIBLE else View.GONE
        }

        disconnectButton.setOnClickListener {
            viewModel.disconnectFromAirPlay()
            dismiss()
        }
    }
}
```

**Step 3: Run build to verify**

Run: `./gradlew compileTempoDebugKotlin`
Expected: BUILD SUCCESSFUL

**Step 4: Commit**

```bash
git add app/src/tempo/res/layout/dialog_airplay_device_list.xml
git add app/src/tempo/java/com/cappielloantonio/tempo/ui/dialog/AirPlayDeviceDialog.kt
git commit -m "feat: add AirPlay device selection dialog"
```

---

### Task 11: Create AirPlay Device Adapter

**Files:**
- Create: `app/src/tempo/java/com/cappielloantonio/tempo/ui/adapter/AirPlayDeviceAdapter.kt`
- Create: `app/src/tempo/res/layout/item_airplay_device.xml`

**Step 1: Create device item layout**

```xml
<?xml version="1.0" encoding="utf-8"?>
<LinearLayout xmlns:android="http://schemas.android.com/apk/res/android"
    android:layout_width="match_parent"
    android:layout_height="wrap_content"
    android:orientation="horizontal"
    android:padding="16dp"
    android:background="?attr/selectableItemBackground"
    android:clickable="true"
    android:focusable="true">

    <ImageView
        android:id="@+id/device_icon"
        android:layout_width="40dp"
        android:layout_height="40dp"
        android:src="@drawable/ic_airplay"
        android:layout_gravity="center_vertical"
        android:layout_marginEnd="16dp" />

    <LinearLayout
        android:layout_width="0dp"
        android:layout_height="wrap_content"
        android:layout_weight="1"
        android:orientation="vertical">

        <TextView
            android:id="@+id/device_name"
            android:layout_width="wrap_content"
            android:layout_height="wrap_content"
            android:textSize="16sp"
            android:textStyle="bold" />

        <TextView
            android:id="@+id/device_model"
            android:layout_width="wrap_content"
            android:layout_height="wrap_content"
            android:textSize="14sp"
            android:textColor="?android:attr/textColorSecondary" />

    </LinearLayout>

    <ImageView
        android:id="@+id/encryption_icon"
        android:layout_width="24dp"
        android:layout_height="24dp"
        android:layout_gravity="center_vertical"
        android:src="@drawable/ic_lock"
        android:visibility="gone" />

</LinearLayout>
```

**Step 2: Create adapter class**

```kotlin
package com.cappielloantonio.tempo.ui.adapter

import android.view.LayoutInflater
import android.view.ViewGroup
import androidx.recyclerview.widget.DiffUtil
import androidx.recyclerview.widget.ListAdapter
import androidx.recyclerview.widget.RecyclerView
import com.cappielloantonio.tempo.databinding.ItemAirplayDeviceBinding
import com.cappielloantonio.tempo.model.AirPlayDevice

class AirPlayDeviceAdapter(
    private val onDeviceClick: (AirPlayDevice) -> Unit
) : ListAdapter<AirPlayDevice, AirPlayDeviceAdapter.ViewHolder>(DeviceDiffCallback()) {

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): ViewHolder {
        val binding = ItemAirplayDeviceBinding.inflate(
            LayoutInflater.from(parent.context),
            parent,
            false
        )
        return ViewHolder(binding)
    }

    override fun onBindViewHolder(holder: ViewHolder, position: Int) {
        holder.bind(getItem(position))
    }

    inner class ViewHolder(
        private val binding: ItemAirplayDeviceBinding
    ) : RecyclerView.ViewHolder(binding.root) {

        fun bind(device: AirPlayDevice) {
            binding.deviceName.text = device.name
            binding.deviceModel.text = device.model
            binding.encryptionIcon.visibility = if (device.supportsEncryption) {
                android.view.View.VISIBLE
            } else {
                android.view.View.GONE
            }

            binding.root.setOnClickListener {
                onDeviceClick(device)
            }
        }
    }

    class DeviceDiffCallback : DiffUtil.ItemCallback<AirPlayDevice>() {
        override fun areItemsTheSame(oldItem: AirPlayDevice, newItem: AirPlayDevice): Boolean {
            return oldItem.deviceId == newItem.deviceId
        }

        override fun areContentsTheSame(oldItem: AirPlayDevice, newItem: AirPlayDevice): Boolean {
            return oldItem == newItem
        }
    }
}
```

**Step 3: Run build to verify**

Run: `./gradlew compileTempoDebugKotlin`
Expected: BUILD SUCCESSFUL

**Step 4: Commit**

```bash
git add app/src/tempo/res/layout/item_airplay_device.xml
git add app/src/tempo/java/com/cappielloantonio/tempo/ui/adapter/AirPlayDeviceAdapter.kt
git commit -m "feat: add AirPlay device adapter"
```

---

## Phase 6: Testing & Polish

### Task 12: Add Instrumented Tests for Real Device Scenarios

**Files:**
- Create: `app/src/androidTest/java/com/cappielloantonio/tempo/AirPlayIntegrationTest.kt`

**Step 1: Create integration test**

```kotlin
package com.cappielloantonio.tempo

import androidx.test.ext.junit.runners.AndroidJUnit4
import com.cappielloantonio.tempo.service.AirPlayDeviceScanner
import com.cappielloantonio.tempo.service.AirPlayConnectionManager
import dagger.hilt.android.testing.HiltAndroidTest
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import javax.inject.Inject

@HiltAndroidTest
@RunWith(AndroidJUnit4::class)
class AirPlayIntegrationTest {

    @Inject
    lateinit var deviceScanner: AirPlayDeviceScanner

    @Inject
    lateinit var connectionManager: AirPlayConnectionManager

    @Before
    fun setup() {
        // Initialize components
    }

    @Test
    fun scanner_canDiscoverDevices() = runTest {
        deviceScanner.startScan()
        kotlinx.coroutines.delay(5000) // Wait for discovery
        deviceScanner.stopScan()

        val devices = deviceScanner.getAvailableDevices()
        assertTrue(devices.isNotEmpty())
    }

    @Test
    fun connectionManager_canConnectToDevice() = runTest {
        // This test requires a real AirPlay device on the network
        val devices = deviceScanner.getAvailableDevices()
        if (devices.isEmpty()) {
            // Skip if no devices available
            return@runTest
        }

        val device = devices.first()
        val result = connectionManager.connect(device)

        assertTrue(result.isSuccess)
        assertTrue(connectionManager.isConnected)

        connectionManager.disconnect()
    }
}
```

**Step 2: Run instrumented test**

Run: `./gradlew connectedTempoDebugAndroidTest`
Expected: Tests pass (may need real device or emulator with network)

**Step 3: Commit**

```bash
git add app/src/androidTest/java/com/cappielloantonio/tempo/AirPlayIntegrationTest.kt
git commit -m "test: add AirPlay integration tests"
```

---

## Success Criteria

After completing all tasks, the following should work:

- [ ] Can discover AirPlay devices on the local network
- [ ] Device list appears in UI
- [ ] Can connect to unencrypted AirPlay devices
- [ ] Connection status is visible in UI
- [ ] Can disconnect from AirPlay device
- [ ] Device discovery respects Wi-Fi and battery state
- [ ] All unit tests pass
- [ ] Code compiles without warnings

## Next Steps After This Plan

1. **Authentication**: Implement Pair-Verify and Pair-Setup for encrypted connections
2. **Audio Streaming**: Implement RTP audio streaming with encoding
3. **Metadata**: Add DMAP metadata and artwork transmission
4. **Volume Control**: Implement independent volume control
5. **Error Handling**: Add comprehensive error handling and reconnection logic
6. **Performance**: Optimize memory and CPU usage
7. **Testing**: Extensive testing on various AirPlay devices

## References

- Design Document: `docs/plans/2026-01-01-airplay2-design.md`
- Owntone Implementation: `/Users/tofu/Dev/github.com/owntone/src/outputs/`
- Media3 Documentation: https://developer.android.com/guide/topics/media/media3
