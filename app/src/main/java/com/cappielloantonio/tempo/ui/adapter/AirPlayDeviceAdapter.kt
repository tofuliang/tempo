package com.cappielloantonio.tempo.ui.adapter

import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.TextView
import androidx.recyclerview.widget.RecyclerView
import com.cappielloantonio.tempo.R
import com.cappielloantonio.tempo.model.AirPlayDevice
import com.cappielloantonio.tempo.service.AirPlaySessionManager

class AirPlayDeviceAdapter(
    private val listener: (AirPlayDevice) -> Unit
) : RecyclerView.Adapter<AirPlayDeviceAdapter.ViewHolder>() {

    private var devices: List<AirPlayDevice> = emptyList()
    private var connectedDevice: AirPlayDevice? = null
    private var connectionState = AirPlaySessionManager.STATE_DISCONNECTED

    fun setDevices(devices: List<AirPlayDevice>) {
        this.devices = devices
        notifyDataSetChanged()
    }

    fun setConnectionInfo(device: AirPlayDevice?, state: Int) {
        this.connectedDevice = device
        this.connectionState = state
        notifyDataSetChanged()
    }

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): ViewHolder {
        val view = LayoutInflater.from(parent.context)
            .inflate(R.layout.item_airplay_device, parent, false)
        return ViewHolder(view)
    }

    override fun onBindViewHolder(holder: ViewHolder, position: Int) {
        val device = devices[position]
        holder.name.text = device.name
        holder.itemView.setOnClickListener { listener(device) }

        val isConnected = connectedDevice?.let {
            it.ip == device.ip && it.port == device.port
        } ?: false

        if (isConnected && connectionState != AirPlaySessionManager.STATE_DISCONNECTED) {
            holder.status.visibility = View.VISIBLE
            val ctx = holder.itemView.context
            holder.status.text = when (connectionState) {
                AirPlaySessionManager.STATE_CONNECTING -> ctx.getString(R.string.airplay_status_connecting)
                AirPlaySessionManager.STATE_CONNECTED -> ctx.getString(R.string.airplay_status_connected)
                AirPlaySessionManager.STATE_PLAYING -> ctx.getString(R.string.airplay_status_playing)
                AirPlaySessionManager.STATE_PAUSED -> ctx.getString(R.string.airplay_status_paused)
                AirPlaySessionManager.STATE_BUFFERING -> ctx.getString(R.string.airplay_status_buffering)
                AirPlaySessionManager.STATE_STOPPED -> ctx.getString(R.string.airplay_status_stopped)
                else -> {
                    holder.status.visibility = View.GONE
                    null
                }
            }
        } else {
            holder.status.visibility = View.GONE
        }
    }

    override fun getItemCount(): Int = devices.size

    class ViewHolder(itemView: View) : RecyclerView.ViewHolder(itemView) {
        val name: TextView = itemView.findViewById(R.id.tv_device_name)
        val status: TextView = itemView.findViewById(R.id.tv_device_status)
    }
}
