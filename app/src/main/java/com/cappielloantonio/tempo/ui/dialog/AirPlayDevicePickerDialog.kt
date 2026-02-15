package com.cappielloantonio.tempo.ui.dialog

import android.app.Dialog
import android.os.Bundle
import android.view.View
import android.widget.SeekBar
import androidx.fragment.app.DialogFragment
import androidx.media3.common.util.UnstableApi
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import com.cappielloantonio.tempo.R
import com.cappielloantonio.tempo.service.AirPlayDiscoveryManager
import com.cappielloantonio.tempo.service.AirPlaySessionManager
import com.cappielloantonio.tempo.ui.adapter.AirPlayDeviceAdapter
import com.google.android.material.dialog.MaterialAlertDialogBuilder

@UnstableApi
class AirPlayDevicePickerDialog : DialogFragment() {

    private lateinit var discoveryManager: AirPlayDiscoveryManager
    private lateinit var adapter: AirPlayDeviceAdapter

    override fun onCreateDialog(savedInstanceState: Bundle?): Dialog {
        val view = layoutInflater.inflate(R.layout.dialog_airplay_device_picker, null)

        val recyclerView = view.findViewById<RecyclerView>(R.id.rv_devices)
        recyclerView.layoutManager = LinearLayoutManager(context)

        val sessionManager = AirPlaySessionManager.getInstance()

        adapter = AirPlayDeviceAdapter { device ->
            val current = sessionManager.currentDevice
            val isCurrentDevice = current != null
                    && current.ip == device.ip
                    && current.port == device.port

            val state = sessionManager.state.value
            if (isCurrentDevice && state != null && state != AirPlaySessionManager.STATE_DISCONNECTED) {
                sessionManager.stop()
            } else {
                if (sessionManager.isActive) {
                    sessionManager.stop()
                }
                sessionManager.connect(device)
            }
            dismiss()
        }
        recyclerView.adapter = adapter

        discoveryManager = AirPlayDiscoveryManager(requireContext())

        val volumeContainer = view.findViewById<View>(R.id.volume_container)
        val volumeSeekBar = view.findViewById<SeekBar>(R.id.seekbar_volume)
        volumeSeekBar.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(seekBar: SeekBar, progress: Int, fromUser: Boolean) {
                if (fromUser) sessionManager.setVolume(progress)
            }
            override fun onStartTrackingTouch(seekBar: SeekBar) {}
            override fun onStopTrackingTouch(seekBar: SeekBar) {}
        })

        sessionManager.state.observe(this) { state ->
            adapter.setConnectionInfo(sessionManager.currentDevice, state)
            val connected = state != null && state != AirPlaySessionManager.STATE_DISCONNECTED
            volumeContainer.visibility = if (connected) View.VISIBLE else View.GONE
            if (connected) {
                volumeSeekBar.progress = sessionManager.volumePct
            }
        }

        discoveryManager.devicesLiveData.observe(this) { devices ->
            adapter.setDevices(devices)
        }

        return MaterialAlertDialogBuilder(requireContext())
            .setTitle(R.string.airplay_dialog_title)
            .setView(view)
            .create()
    }

    override fun onStart() {
        super.onStart()
        discoveryManager.startDiscovery()
    }

    override fun onStop() {
        super.onStop()
        discoveryManager.stopDiscovery()
    }
}
