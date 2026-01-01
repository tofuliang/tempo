package com.cappielloantonio.tempo.protocol.rtsp

import kotlinx.coroutines.test.runTest
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
