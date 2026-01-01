package com.cappielloantonio.tempo.ui.adapter

import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.TextView
import androidx.recyclerview.widget.DiffUtil
import androidx.recyclerview.widget.ListAdapter
import androidx.recyclerview.widget.RecyclerView
import com.cappielloantonio.tempo.R
import com.cappielloantonio.tempo.model.AirPlayDevice

/**
 * Stub adapter for AirPlay device list.
 * This is a temporary implementation - the full adapter will be created in Task 11.
 */
class AirPlayDeviceAdapter(
    private val onDeviceClick: (AirPlayDevice) -> Unit
) : ListAdapter<AirPlayDevice, AirPlayDeviceAdapter.ViewHolder>(DiffCallback()) {

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): ViewHolder {
        // Using simple_list_item_1 as a stub layout
        val view = LayoutInflater.from(parent.context)
            .inflate(android.R.layout.simple_list_item_1, parent, false)
        return ViewHolder(view)
    }

    override fun onBindViewHolder(holder: ViewHolder, position: Int) {
        val device = getItem(position)
        holder.bind(device, onDeviceClick)
    }

    class ViewHolder(itemView: View) : RecyclerView.ViewHolder(itemView) {
        private val textView: TextView = itemView.findViewById(android.R.id.text1)

        fun bind(device: AirPlayDevice, onDeviceClick: (AirPlayDevice) -> Unit) {
            textView.text = device.name
            itemView.setOnClickListener {
                onDeviceClick(device)
            }
        }
    }

    private class DiffCallback : DiffUtil.ItemCallback<AirPlayDevice>() {
        override fun areItemsTheSame(oldItem: AirPlayDevice, newItem: AirPlayDevice): Boolean {
            return oldItem.name == newItem.name && oldItem.host == newItem.host
        }

        override fun areContentsTheSame(oldItem: AirPlayDevice, newItem: AirPlayDevice): Boolean {
            return oldItem == newItem
        }
    }
}
