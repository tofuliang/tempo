package com.cappielloantonio.tempo.service

import android.content.Context
import android.net.nsd.NsdManager
import android.net.nsd.NsdServiceInfo
import android.util.Log
import androidx.lifecycle.LiveData
import androidx.lifecycle.MutableLiveData
import com.cappielloantonio.tempo.model.AirPlayDevice
import java.util.ArrayDeque

class AirPlayDiscoveryManager(context: Context) {

    private val nsdManager = context.getSystemService(Context.NSD_SERVICE) as NsdManager
    private var discoveryListener: NsdManager.DiscoveryListener? = null
    private val _devices = MutableLiveData<List<AirPlayDevice>>(emptyList())
    private val devices = mutableListOf<AirPlayDevice>()
    private var isDiscovering = false
    private val resolveQueue = ArrayDeque<NsdServiceInfo>()
    private var isResolving = false

    val devicesLiveData: LiveData<List<AirPlayDevice>> get() = _devices

    fun startDiscovery() {
        if (isDiscovering) return

        val listener = object : NsdManager.DiscoveryListener {
            override fun onDiscoveryStarted(regType: String) {
                Log.d(TAG, "Service discovery started")
                isDiscovering = true
            }

            override fun onServiceFound(service: NsdServiceInfo) {
                Log.d(TAG, "Service found: $service")
                if (service.serviceType == SERVICE_TYPE) {
                    enqueueResolve(service)
                }
            }

            override fun onServiceLost(service: NsdServiceInfo) {
                Log.w(TAG, "service lost: $service")
                removeDevice(service)
            }

            override fun onDiscoveryStopped(serviceType: String) {
                Log.i(TAG, "Discovery stopped: $serviceType")
                isDiscovering = false
            }

            override fun onStartDiscoveryFailed(serviceType: String, errorCode: Int) {
                Log.e(TAG, "Discovery failed: Error code:$errorCode")
                nsdManager.stopServiceDiscovery(this)
            }

            override fun onStopDiscoveryFailed(serviceType: String, errorCode: Int) {
                Log.e(TAG, "Discovery failed: Error code:$errorCode")
                nsdManager.stopServiceDiscovery(this)
            }
        }

        discoveryListener = listener
        nsdManager.discoverServices(SERVICE_TYPE, NsdManager.PROTOCOL_DNS_SD, listener)
    }

    fun stopDiscovery() {
        if (isDiscovering && discoveryListener != null) {
            nsdManager.stopServiceDiscovery(discoveryListener)
            discoveryListener = null
            isDiscovering = false
        }
    }

    private fun addDevice(serviceInfo: NsdServiceInfo) {
        val host = serviceInfo.host ?: return
        val ip = host.hostAddress ?: return
        val port = serviceInfo.port
        val rawName = serviceInfo.serviceName
        // RAOP service names are typically "AABBCCDDEE@DeviceName" — strip MAC prefix
        val atIndex = rawName.indexOf('@')
        val name = if (atIndex >= 0 && atIndex < rawName.length - 1) {
            rawName.substring(atIndex + 1)
        } else {
            rawName
        }

        val device = AirPlayDevice(name, ip, port)

        synchronized(devices) {
            val exists = devices.any { it.ip == ip && it.port == port }
            if (!exists) {
                devices.add(device)
                _devices.postValue(ArrayList(devices))
            }
        }
    }

    private fun enqueueResolve(service: NsdServiceInfo) {
        synchronized(resolveQueue) {
            resolveQueue.add(service)
            if (!isResolving) {
                resolveNext()
            }
        }
    }

    private fun resolveNext() {
        val next: NsdServiceInfo?
        synchronized(resolveQueue) {
            next = resolveQueue.poll()
            if (next == null) {
                isResolving = false
                return
            }
            isResolving = true
        }
        nsdManager.resolveService(next, object : NsdManager.ResolveListener {
            override fun onResolveFailed(serviceInfo: NsdServiceInfo, errorCode: Int) {
                Log.e(TAG, "Resolve failed: $errorCode for ${serviceInfo.serviceName}")
                resolveNext()
            }

            override fun onServiceResolved(serviceInfo: NsdServiceInfo) {
                Log.d(TAG, "Resolve Succeeded. $serviceInfo")
                addDevice(serviceInfo)
                resolveNext()
            }
        })
    }

    private fun removeDevice(serviceInfo: NsdServiceInfo) {
        val rawName = serviceInfo.serviceName
        val atIndex = rawName.indexOf('@')
        val deviceName = if (atIndex >= 0 && atIndex < rawName.length - 1) {
            rawName.substring(atIndex + 1)
        } else {
            rawName
        }

        synchronized(devices) {
            val removed = devices.removeAll { it.name == deviceName }
            if (removed) {
                _devices.postValue(ArrayList(devices))
            }
        }
    }

    companion object {
        private const val TAG = "AirPlayDiscoveryManager"
        private const val SERVICE_TYPE = "_raop._tcp."
    }
}
