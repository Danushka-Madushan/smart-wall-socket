package nibm.iot.socketman.viewmodel

import android.content.Context
import android.net.nsd.NsdManager
import android.net.nsd.NsdServiceInfo
import android.widget.Toast
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableDoubleStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL

// --- Models ---
sealed class AppState {
    object Disconnected : AppState()
    object Connecting : AppState()
    data class Connected(val ssid: String, val ip: String, val rssi: Int, val mac: String) : AppState()
}

data class EnergyData(
    val currentA: Float = 0f,
    val powerW: Float = 0f,
    val voltageV: Float = 0f,
)

data class SmartDevice(val name: String, val ip: String)

// --- ViewModel ---
class SocketViewModel : ViewModel() {
    var state by mutableStateOf<AppState>(AppState.Connecting)
        private set

    var energyData by mutableStateOf(EnergyData())
        private set

    var totalUnits by mutableDoubleStateOf(0.0) // 1 unit = 1 kWh
        private set

    var scannedNetworks by mutableStateOf<List<SmartDevice>>(emptyList())
        private set

    var isScanning by mutableStateOf(false)
        private set

    private var pollingJob: Job? = null
    
    private var currentSocketIp = "192.168.4.1"
    private var nsdManager: NsdManager? = null
    private var discoveryListener: NsdManager.DiscoveryListener? = null

    fun initCheck(context: Context) {
        if (state is AppState.Connected) return
        state = AppState.Connecting
        viewModelScope.launch(Dispatchers.IO) {
            try {
                val url = URL("http://$currentSocketIp/api/wifi/status")
                val conn = url.openConnection() as HttpURLConnection
                conn.connectTimeout = 2000
                conn.readTimeout = 2000
                val response = conn.inputStream.bufferedReader().use { it.readText() }

                val json = JSONObject(response)
                val ssid = json.optString("ssid", "SmartSocket")
                val ip = json.optString("ip", currentSocketIp)
                val rssi = json.optInt("rssi", 1)
                val mac = json.optString("mac", json.optString("mc", "Unknown"))

                withContext(Dispatchers.Main) {
                    state = AppState.Connected(ssid, ip, rssi, mac)
                    startPolling()
                }
            } catch (e: Exception) {
                withContext(Dispatchers.Main) {
                    state = AppState.Disconnected
                    startScan(context)
                }
            }
        }
    }

    fun startScan(context: Context) {
        if (isScanning) return
        isScanning = true
        scannedNetworks = emptyList()

        // 1. Fallback: Always blindly ping the default AP IP just in case mDNS fails
        viewModelScope.launch(Dispatchers.IO) {
            try {
                val url = URL("http://192.168.4.1/api/heartbeat")
                val conn = url.openConnection() as HttpURLConnection
                conn.connectTimeout = 2000
                conn.readTimeout = 2000
                val response = conn.inputStream.bufferedReader().use { it.readText() }
                
                val json = JSONObject(response)
                if (json.optBoolean("ack", false)) {
                    withContext(Dispatchers.Main) {
                        val fallbackDevice = SmartDevice("SmartSocket (Hotspot)", "192.168.4.1")
                        if (scannedNetworks.none { it.ip == fallbackDevice.ip }) {
                            scannedNetworks = scannedNetworks + fallbackDevice
                        }
                    }
                }
            } catch (ignored: Exception) {}
        }

        // 2. mDNS Service Discovery
        nsdManager = context.getSystemService(Context.NSD_SERVICE) as NsdManager
        discoveryListener = object : NsdManager.DiscoveryListener {
            override fun onDiscoveryStarted(regType: String) {}

            override fun onServiceFound(service: NsdServiceInfo) {
                // The device advertises "smartsocket"
                if (service.serviceName.contains("smartsocket", ignoreCase = true)) {
                    nsdManager?.resolveService(service, object : NsdManager.ResolveListener {
                        override fun onResolveFailed(serviceInfo: NsdServiceInfo, errorCode: Int) {}

                        override fun onServiceResolved(serviceInfo: NsdServiceInfo) {
                            val ip = serviceInfo.host?.hostAddress ?: return
                            val name = serviceInfo.serviceName
                            val device = SmartDevice(name, ip)
                            
                            viewModelScope.launch(Dispatchers.Main) {
                                if (scannedNetworks.none { it.ip == ip }) {
                                    scannedNetworks = scannedNetworks + device
                                }
                            }
                        }
                    })
                }
            }

            override fun onServiceLost(service: NsdServiceInfo) {}
            override fun onDiscoveryStopped(serviceType: String) {}
            override fun onStartDiscoveryFailed(serviceType: String, errorCode: Int) {
                try { nsdManager?.stopServiceDiscovery(this) } catch (ignored: Exception) {}
            }
            override fun onStopDiscoveryFailed(serviceType: String, errorCode: Int) {}
        }

        try {
            nsdManager?.discoverServices("_http._tcp.", NsdManager.PROTOCOL_DNS_SD, discoveryListener)
        } catch (e: Exception) {
            isScanning = false
            return
        }

        // Stop scanning after 5 seconds to prevent battery drain
        viewModelScope.launch {
            delay(5000)
            stopScan()
        }
    }

    private fun stopScan() {
        if (!isScanning) return
        try {
            discoveryListener?.let { nsdManager?.stopServiceDiscovery(it) }
        } catch (ignored: Exception) {}
        discoveryListener = null
        isScanning = false
    }

    fun connect(context: Context, deviceIp: String) {
        if (state is AppState.Connecting || state is AppState.Connected) return
        state = AppState.Connecting
        currentSocketIp = deviceIp

        viewModelScope.launch(Dispatchers.IO) {
            try {
                val url = URL("http://$currentSocketIp/api/wifi/status")
                val conn = url.openConnection() as HttpURLConnection
                conn.connectTimeout = 3000
                conn.readTimeout = 3000
                val response = conn.inputStream.bufferedReader().use { it.readText() }

                val json = JSONObject(response)
                val ssid = json.optString("ssid", "SmartSocket")
                val ip = json.optString("ip", currentSocketIp)
                val rssi = json.optInt("rssi", 1)
                val mac = json.optString("mac", json.optString("mc", "Unknown"))

                withContext(Dispatchers.Main) {
                    state = AppState.Connected(ssid, ip, rssi, mac)
                    startPolling()
                }
            } catch (e: Exception) {
                withContext(Dispatchers.Main) {
                    disconnect(context, "Failed to connect to device.")
                }
            }
        }
    }

    private fun startPolling() {
        pollingJob?.cancel()
        pollingJob = viewModelScope.launch(Dispatchers.IO) {
            var errorCount = 0
            while (isActive && (state is AppState.Connected)) {
                try {
                    val url = URL("http://$currentSocketIp/api/energy")
                    val conn = url.openConnection() as HttpURLConnection
                    conn.connectTimeout = 2000
                    conn.readTimeout = 2000
                    val response = conn.inputStream.bufferedReader().use { it.readText() }

                    val json = JSONObject(response)
                    val currentA = json.optDouble("current_A", 0.0).toFloat()
                    val powerW = json.optDouble("power_W", 0.0).toFloat()
                    val voltageV = json.optDouble("voltage_V", 0.0).toFloat()

                    withContext(Dispatchers.Main) {
                        energyData = EnergyData(currentA, powerW, voltageV)
                        // Accumulate units: power_W is Joules per second. 1 Unit = 3,600,000 Joules
                        totalUnits += (powerW / 3600000.0)
                        errorCount = 0
                    }
                } catch (e: Exception) {
                    errorCount++
                    if (errorCount >= 3) {
                        withContext(Dispatchers.Main) {
                            state = AppState.Disconnected
                        }
                        break
                    }
                }
                delay(1000L)
            }
        }
    }

    fun disconnect(context: Context, reason: String? = null) {
        pollingJob?.cancel()
        state = AppState.Disconnected
        scannedNetworks = emptyList()
        stopScan()

        reason?.let {
            Toast.makeText(context, it, Toast.LENGTH_SHORT).show()
        }
    }

    override fun onCleared() {
        super.onCleared()
        stopScan()
        pollingJob?.cancel()
    }
}
