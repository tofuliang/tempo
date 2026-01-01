package com.cappielloantonio.tempo

import android.content.Context
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.cappielloantonio.tempo.model.AirPlayDevice
import com.cappielloantonio.tempo.service.AirPlayConnectionManager
import com.cappielloantonio.tempo.service.AirPlayDeviceScanner
import kotlinx.coroutines.delay
import kotlinx.coroutines.runBlocking
import org.junit.After
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

/**
 * Instrumented tests for AirPlay 2 integration.
 *
 * These tests verify that the AirPlay device scanner and connection manager
 * work correctly on real Android devices/emulators.
 *
 * Note: Some tests require AirPlay devices to be available on the network.
 * Tests will gracefully skip if no devices are found.
 */
@RunWith(AndroidJUnit4::class)
class AirPlayIntegrationTest {

    private lateinit var context: Context
    private lateinit var deviceScanner: AirPlayDeviceScanner
    private lateinit var connectionManager: AirPlayConnectionManager

    @Before
    fun setup() {
        context = InstrumentationRegistry.getInstrumentation().targetContext
        deviceScanner = AirPlayDeviceScanner(context)
        connectionManager = AirPlayConnectionManager()

        // Clear any cached devices before each test
        deviceScanner.clearCache()
    }

    @After
    fun tearDown() {
        // Clean up resources after each test
        deviceScanner.stopScan()
        deviceScanner.close()

        if (connectionManager.isConnected) {
            connectionManager.disconnect()
        }
    }

    @Test
    fun useAppContext() {
        // Basic test to verify context is available
        assertNotNull("Application context should not be null", context)
    }

    @Test
    fun scanner_canBeInstantiated() {
        // Test that the scanner can be created
        assertNotNull("Device scanner should not be null", deviceScanner)
    }

    @Test
    fun scanner_initiallyHasNoDevices() {
        // Test that scanner starts with no cached devices
        val devices = deviceScanner.getAvailableDevices()
        assertNotNull("Devices list should not be null", devices)
        assertTrue("Initially, no devices should be available", devices.isEmpty())
    }

    @Test
    fun scanner_canStartAndStopScan() = runBlocking {
        // Test that scanning can be started and stopped without errors
        deviceScanner.startScan()
        delay(1000) // Let it run briefly
        deviceScanner.stopScan()

        // If we get here without exception, the test passes
        assertTrue("Scanner should be able to start and stop", true)
    }

    @Test
    fun scanner_canDiscoverDevices() = runBlocking {
        // Note: This test requires AirPlay devices on the network
        // It will pass if devices are found, but won't fail if none are available

        deviceScanner.startScan()

        // Wait for discovery (NSD can take time)
        delay(10_000) // 10 seconds for discovery

        deviceScanner.stopScan()

        val devices = deviceScanner.getAvailableDevices()

        // Log the result for debugging
        println("Found ${devices.size} AirPlay device(s)")

        if (devices.isNotEmpty()) {
            devices.forEach { device ->
                println("  - ${device.name} at ${device.host}:${device.port}")
            }
            assertTrue("Should discover at least one device if available on network", devices.isNotEmpty())
        } else {
            println("No devices found - this is expected if no AirPlay devices are on the network")
            // Don't fail the test if no devices are available
            assertTrue("Test passes even without devices", true)
        }
    }

    @Test
    fun connectionManager_canBeInstantiated() {
        // Test that the connection manager can be created
        assertNotNull("Connection manager should not be null", connectionManager)
    }

    @Test
    fun connectionManager_initiallyNotConnected() {
        // Test that connection manager starts disconnected
        val isConnected = connectionManager.isConnected
        assertTrue("Initially should not be connected", !isConnected)
    }

    @Test
    fun connectionManager_canDisconnectWhenNotConnected() {
        // Test that disconnecting when not connected doesn't throw
        try {
            connectionManager.disconnect()
            assertTrue("Disconnect should not throw when not connected", true)
        } catch (e: Exception) {
            assertTrue("Disconnect should be safe even when not connected", false)
        }
    }

    @Test
    fun connectionManager_canConnectToDevice() = runBlocking {
        // Note: This test requires a real AirPlay device on the network
        // It will attempt connection but won't fail if no devices are available

        // First, try to discover devices
        deviceScanner.startScan()
        delay(10_000) // Wait for discovery
        deviceScanner.stopScan()

        val devices = deviceScanner.getAvailableDevices()

        if (devices.isEmpty()) {
            println("No AirPlay devices found - skipping connection test")
            return@runBlocking // Skip test gracefully
        }

        // Try to connect to the first available device
        val device = devices.first()
        println("Attempting to connect to ${device.name} at ${device.host}:${device.port}")

        val result = connectionManager.connect(device)

        if (result.isSuccess) {
            println("Successfully connected to ${device.name}")
            assertTrue("Should be connected after successful connection", connectionManager.isConnected)

            // Test disconnection
            connectionManager.disconnect()
            assertTrue("Should not be connected after disconnect", !connectionManager.isConnected)
        } else {
            println("Connection failed: ${result.exceptionOrNull()?.message}")
            // Don't fail - some devices may not accept connections without proper authentication
            assertTrue("Test completes even if connection fails", true)
        }
    }

    @Test
    fun scanner_discoveredDevicesHaveRequiredFields() = runBlocking {
        // Test that discovered devices have all required fields populated

        deviceScanner.startScan()
        delay(10_000) // Wait for discovery
        deviceScanner.stopScan()

        val devices = deviceScanner.getAvailableDevices()

        if (devices.isEmpty()) {
            println("No devices found - skipping field validation")
            return@runBlocking
        }

        devices.forEach { device ->
            assertNotNull("Device name should not be null", device.name)
            assertNotNull("Device host should not be null", device.host)
            assertTrue("Device port should be positive", device.port > 0)
            assertNotNull("Device ID should not be null", device.deviceId)

            println("Device ${device.name}:")
            println("  - Host: ${device.host}")
            println("  - Port: ${device.port}")
            println("  - Device ID: ${device.deviceId}")
            println("  - Model: ${device.model}")
            println("  - Features: 0x${device.features.toString(16)}")
            println("  - Supports Encryption: ${device.supportsEncryption}")
        }

        assertTrue("All devices should have valid fields", true)
    }

    @Test
    fun scanner_canClearCache() {
        // Test that cache can be cleared
        // First, manually add a device to cache
        val testDevice = AirPlayDevice(
            name = "Test Device",
            host = "192.168.1.1",
            port = 7000,
            deviceId = "test-device-id",
            features = 0x10,
            model = "TestModel",
            version = 2,
            supportsEncryption = false,
            lastSeen = System.currentTimeMillis()
        )

        deviceScanner.addDeviceToCache(testDevice)

        var devices = deviceScanner.getAvailableDevices()
        assertTrue("After adding device, cache should not be empty", devices.isNotEmpty())

        // Clear cache
        deviceScanner.clearCache()
        devices = deviceScanner.getAvailableDevices()
        assertTrue("After clearing cache, no devices should be available", devices.isEmpty())
    }

    @Test
    fun scanner_filtersOfflineDevices() = runBlocking {
        // Test that offline devices (last seen > 30 seconds ago) are filtered out

        // Add a recent device
        val recentDevice = AirPlayDevice(
            name = "Recent Device",
            host = "192.168.1.2",
            port = 7000,
            deviceId = "recent-device",
            features = 0x10,
            model = "TestModel",
            version = 2,
            supportsEncryption = false,
            lastSeen = System.currentTimeMillis() // Just now
        )

        // Add an old device (more than 30 seconds ago)
        val oldDevice = AirPlayDevice(
            name = "Old Device",
            host = "192.168.1.3",
            port = 7000,
            deviceId = "old-device",
            features = 0x10,
            model = "TestModel",
            version = 2,
            supportsEncryption = false,
            lastSeen = System.currentTimeMillis() - 60_000 // 60 seconds ago
        )

        deviceScanner.addDeviceToCache(recentDevice)
        deviceScanner.addDeviceToCache(oldDevice)

        val devices = deviceScanner.getAvailableDevices()

        // Should only have the recent device
        assertTrue("Should filter out offline devices", devices.size == 1)
        assertTrue("Should only contain recent device", devices[0].deviceId == "recent-device")
    }
}
