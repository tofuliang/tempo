package com.cappielloantonio.tempo.service

import android.content.Context
import android.net.nsd.NsdManager
import android.net.nsd.NsdServiceInfo
import android.util.Log
import com.cappielloantonio.tempo.model.AirPlayDevice
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
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
    private val activeResolvers = ConcurrentHashMap<NsdServiceInfo, NsdManager.ResolveListener>()
    private val scanScope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)

    private val _discoveredDevices = MutableStateFlow<List<AirPlayDevice>>(emptyList())
    val discoveredDevices = _discoveredDevices.asStateFlow()

    private var discoveryListener: NsdManager.DiscoveryListener? = null

    fun getAvailableDevices(): List<AirPlayDevice> {
        return deviceCache.values.filter { it.isOnline }.sortedBy { it.name }
    }

    fun addDeviceToCache(device: AirPlayDevice) {
        deviceCache[device.deviceId] = device
        _discoveredDevices.value = getAvailableDevices()
    }

    fun startScan() {
        stopScan()

        discoveryListener = object : NsdManager.DiscoveryListener {
            override fun onDiscoveryStarted(regType: String) {
                Log.i(TAG, "Discovery started: $regType")
            }

            override fun onServiceFound(serviceInfo: NsdServiceInfo) {
                Log.d(TAG, "Service found: ${serviceInfo.serviceName}")

                val resolver = object : NsdManager.ResolveListener {
                    override fun onServiceResolved(resolvedService: NsdServiceInfo) {
                        Log.d(TAG, "Service resolved: ${resolvedService.serviceName}")

                        scanScope.launch {
                            parseServiceInfo(resolvedService)?.let { device ->
                                addDeviceToCache(device)
                            }
                            // Clean up resolver
                            activeResolvers.remove(resolvedService)
                        }
                    }

                    override fun onResolveFailed(serviceInfo: NsdServiceInfo, errorCode: Int) {
                        Log.e(TAG, "Resolve failed: ${serviceInfo.serviceName}, code: $errorCode")
                        activeResolvers.remove(serviceInfo)
                    }
                }

                activeResolvers[serviceInfo] = resolver
                nsdManager.resolveService(serviceInfo, resolver)
            }

            override fun onServiceLost(serviceInfo: NsdServiceInfo) {
                Log.d(TAG, "Service lost: ${serviceInfo.serviceName}")

                scanScope.launch {
                    // Remove device from cache using serviceName as key
                    val deviceId = deviceCache.keys.firstOrNull {
                        deviceCache[it]?.name == serviceInfo.serviceName
                    }
                    if (deviceId != null) {
                        deviceCache.remove(deviceId)
                        _discoveredDevices.value = getAvailableDevices()
                    }
                }
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

        nsdManager.discoverServices(SERVICE_TYPE_AIRPLAY, NsdManager.PROTOCOL_DNS_SD, discoveryListener)
    }

    fun stopScan() {
        discoveryListener?.let {
            nsdManager.stopServiceDiscovery(it)
            discoveryListener = null
        }
        // Cancel all pending resolvers
        activeResolvers.clear()
    }

    fun clearCache() {
        deviceCache.clear()
        _discoveredDevices.value = emptyList()
    }

    fun close() {
        stopScan()
        scanScope.cancel()
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
