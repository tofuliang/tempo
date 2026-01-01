package com.cappielloantonio.tempo.ui.dialog

import android.app.Dialog
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import androidx.fragment.app.DialogFragment
import androidx.lifecycle.ViewModelProvider
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import com.cappielloantonio.tempo.R
import com.cappielloantonio.tempo.model.AirPlayDevice
import com.cappielloantonio.tempo.ui.adapter.AirPlayDeviceAdapter
import com.cappielloantonio.tempo.viewmodel.PlayerBottomSheetViewModel
import com.google.android.material.dialog.MaterialAlertDialogBuilder

class AirPlayDeviceDialog : DialogFragment() {

    private lateinit var viewModel: PlayerBottomSheetViewModel
    private lateinit var recyclerView: RecyclerView
    private lateinit var disconnectButton: Button
    private lateinit var adapter: AirPlayDeviceAdapter

    override fun onCreateDialog(savedInstanceState: Bundle?): Dialog {
        viewModel = ViewModelProvider(requireActivity()).get(PlayerBottomSheetViewModel::class.java)

        val view = LayoutInflater.from(requireContext())
            .inflate(R.layout.dialog_airplay_device_list, null, false)

        return MaterialAlertDialogBuilder(requireContext())
            .setTitle("AirPlay")
            .setView(view)
            .create()
    }

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?
    ): View? {
        return inflater.inflate(R.layout.dialog_airplay_device_list, container, false)
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)

        recyclerView = view.findViewById(R.id.airplay_device_recycler)
        disconnectButton = view.findViewById(R.id.disconnect_button)

        setupRecyclerView()
        observeViewModel()
    }

    private fun setupRecyclerView() {
        adapter = AirPlayDeviceAdapter { device ->
            viewModel.connectToAirPlayDevice(device)
            dismiss()
        }

        recyclerView.apply {
            layoutManager = LinearLayoutManager(requireContext())
            adapter = this@AirPlayDeviceDialog.adapter
        }
    }

    private fun observeViewModel() {
        viewModel.airPlayDevices.observe(viewLifecycleOwner) { devices ->
            adapter.submitList(devices)
        }

        viewModel.currentAirPlayDevice.observe(viewLifecycleOwner) { device ->
            disconnectButton.visibility = if (device != null) View.VISIBLE else View.GONE
        }

        disconnectButton.setOnClickListener {
            viewModel.disconnectFromAirPlay()
            dismiss()
        }
    }
}
