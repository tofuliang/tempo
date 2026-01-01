package com.cappielloantonio.tempo.service

import android.content.Context
import android.net.nsd.NsdManager
import android.net.nsd.NsdServiceInfo
import android.util.Log
import com.cappielloantonio.tempo.model.AirPlayDevice
import kotlinx.coroutines.flow.MutableStateFlow
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
            val deviceId = String(attributes["deviceid"] ?: return null, Charsets.UTF_8)
            val features = String(attributes["features"] ?: byteArrayOf(48), Charsets.UTF_8).toInt(16)
            val model = String(attributes["model"] ?: byteArrayOf(), Charsets.UTF_8)

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
