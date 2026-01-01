package com.cappielloantonio.tempo.service

import android.content.Context
import com.cappielloantonio.tempo.model.AirPlayDevice
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
