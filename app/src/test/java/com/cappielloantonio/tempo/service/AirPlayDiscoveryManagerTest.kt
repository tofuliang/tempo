package com.cappielloantonio.tempo.service

import android.content.Context
import android.net.nsd.NsdManager
import org.junit.Before
import org.junit.Test
import org.mockito.Mock
import org.mockito.Mockito.any
import org.mockito.Mockito.eq
import org.mockito.Mockito.verify
import org.mockito.Mockito.`when`
import org.mockito.MockitoAnnotations

class AirPlayDiscoveryManagerTest {

    @Mock
    lateinit var context: Context

    @Mock
    lateinit var nsdManager: NsdManager

    lateinit var discoveryManager: AirPlayDiscoveryManager

    @Before
    fun setUp() {
        MockitoAnnotations.openMocks(this)
        `when`(context.getSystemService(Context.NSD_SERVICE)).thenReturn(nsdManager)
        discoveryManager = AirPlayDiscoveryManager(context)
    }

    @Test
    fun testStartDiscovery() {
        discoveryManager.startDiscovery()
        verify(nsdManager).discoverServices(
            eq("_raop._tcp."),
            eq(NsdManager.PROTOCOL_DNS_SD),
            any(NsdManager.DiscoveryListener::class.java)
        )
    }

    @Test
    fun testStopDiscovery() {
        discoveryManager.startDiscovery()
        discoveryManager.stopDiscovery()
        verify(nsdManager).discoverServices(
            eq("_raop._tcp."),
            eq(NsdManager.PROTOCOL_DNS_SD),
            any(NsdManager.DiscoveryListener::class.java)
        )
    }
}
