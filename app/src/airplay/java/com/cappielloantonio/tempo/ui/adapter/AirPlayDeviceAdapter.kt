package com.cappielloantonio.tempo.ui.adapter

import android.view.LayoutInflater
import android.view.ViewGroup
import androidx.recyclerview.widget.DiffUtil
import androidx.recyclerview.widget.ListAdapter
import androidx.recyclerview.widget.RecyclerView
import com.cappielloantonio.tempo.databinding.ItemAirplayDeviceBinding
import com.cappielloantonio.tempo.model.AirPlayDevice

class AirPlayDeviceAdapter(
    private val onDeviceClick: (AirPlayDevice) -> Unit
) : ListAdapter<AirPlayDevice, AirPlayDeviceAdapter.ViewHolder>(DeviceDiffCallback()) {

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): ViewHolder {
        val binding = ItemAirplayDeviceBinding.inflate(
            LayoutInflater.from(parent.context),
            parent,
            false
        )
        return ViewHolder(binding)
    }

    override fun onBindViewHolder(holder: ViewHolder, position: Int) {
        holder.bind(getItem(position))
    }

    inner class ViewHolder(
        private val binding: ItemAirplayDeviceBinding
    ) : RecyclerView.ViewHolder(binding.root) {

        fun bind(device: AirPlayDevice) {
            binding.deviceName.text = device.name
            binding.deviceModel.text = device.model
            binding.encryptionIcon.visibility = if (device.supportsEncryption) {
                android.view.View.VISIBLE
            } else {
                android.view.View.GONE
            }

            binding.root.setOnClickListener {
                onDeviceClick(device)
            }
        }
    }

    class DeviceDiffCallback : DiffUtil.ItemCallback<AirPlayDevice>() {
        override fun areItemsTheSame(oldItem: AirPlayDevice, newItem: AirPlayDevice): Boolean {
            return oldItem.deviceId == newItem.deviceId
        }

        override fun areContentsTheSame(oldItem: AirPlayDevice, newItem: AirPlayDevice): Boolean {
            return oldItem == newItem
        }
    }
}
