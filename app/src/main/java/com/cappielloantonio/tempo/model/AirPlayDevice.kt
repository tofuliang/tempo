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
