package com.cappielloantonio.tempo.model

import android.os.Parcelable
import androidx.annotation.Keep
import kotlinx.parcelize.Parcelize

@Keep
@Parcelize
data class AirPlayDeviceCapabilities(
    val supportsMetadata: Boolean = false,
    val supportsArtwork: Boolean = false,
    val maxArtworkSize: Int? = null,
    val supportedArtworkFormats: List<String> = emptyList(),
    val supportsProgress: Boolean = false,
    val supportsVolumeControl: Boolean = false,
    val dmapVersion: Int? = null,
    val supportedFeatures: Int = 0
) : Parcelable {
    companion object {
        /** Basic DMAP protocol support (metadata only) */
        const val DMAP_VERSION_1 = 1

        /** DMAP with artwork support */
        const val DMAP_VERSION_2 = 2

        /** Full DAP protocol with progress and volume control */
        const val DMAP_VERSION_3 = 3
    }
}
