package nibm.iot.socketman.viewmodel

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.net.NetworkRequest
import android.net.wifi.WifiManager
import android.net.wifi.WifiNetworkSpecifier
import android.os.PatternMatcher
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
    data class Connected(val ssid: String, val ip: String, val clients: Int, val mac: String) : AppState()
}

data class EnergyData(
    val currentA: Float = 0f,
    val powerW: Float = 0f,
    val voltageV: Float = 0f, // Added trailing comma logic internally in my brain, but here it's fine without as it's the last element. Wait, warning said "Missing trailing comma", which implies it is recommended. Let's add it.
)

// --- ViewModel ---
class SocketViewModel : ViewModel() {
    var state by mutableStateOf<AppState>(AppState.Connecting)
        private set

    var energyData by mutableStateOf(EnergyData())
        private set

    var totalUnits by mutableDoubleStateOf(0.0) // 1 unit = 1 kWh
        private set

    var scannedNetworks by mutableStateOf<List<String>>(emptyList())
        private set

    var isScanning by mutableStateOf(false)
        private set

    private var networkCallback: ConnectivityManager.NetworkCallback? = null
    private var pollingJob: Job? = null

    fun initCheck() {
        if (state is AppState.Connected) return
        state = AppState.Connecting
        viewModelScope.launch(Dispatchers.IO) {
            try {
                val url = URL("http://192.168.4.1/api/wifi/status")
                val conn = url.openConnection() as HttpURLConnection
                conn.connectTimeout = 2000
                conn.readTimeout = 2000
                val response = conn.inputStream.bufferedReader().use { it.readText() }

                val json = JSONObject(response)
                val ssid = json.optString("ssid", "SmartSocket")
                val ip = json.optString("ip", "192.168.4.1")
                val clients = json.optInt("clients", 1)
                val mac = json.optString("mac", json.optString("mc", "Unknown"))

                withContext(Dispatchers.Main) {
                    state = AppState.Connected(ssid, ip, clients, mac)
                    startPolling()
                }
            } catch (ignored: Exception) {
                withContext(Dispatchers.Main) {
                    state = AppState.Disconnected
                }
            }
        }
    }

    fun startScan(context: Context) {
        if (isScanning) return
        isScanning = true
        
        val wifiManager = context.applicationContext.getSystemService(Context.WIFI_SERVICE) as WifiManager
        
        val intentFilter = IntentFilter(WifiManager.SCAN_RESULTS_AVAILABLE_ACTION)
        val receiver = object : BroadcastReceiver() {
            override fun onReceive(c: Context, intent: Intent) {
                try {
                    val results = wifiManager.scanResults
                    scannedNetworks = results
                        .mapNotNull { it.SSID }
                        .filter { it.startsWith("SmartSocket") && it.isNotBlank() }
                        .distinct()
                } catch (e: SecurityException) {
                    // Location permission might be missing
                }
                isScanning = false
                try {
                    c.unregisterReceiver(this)
                } catch (e: Exception) {}
            }
        }
        
        context.registerReceiver(receiver, intentFilter)
        
        val success = wifiManager.startScan()
        if (!success) {
            // Throttled or failed. Just read cached results.
            try {
                val results = wifiManager.scanResults
                scannedNetworks = results
                    .mapNotNull { it.SSID }
                    .filter { it.startsWith("SmartSocket") && it.isNotBlank() }
                    .distinct()
            } catch (e: SecurityException) {
            }
            isScanning = false
            try {
                context.unregisterReceiver(receiver)
            } catch (e: Exception) {}
        }
    }

    fun connect(context: Context, ssid: String, password: String) {
        if (state is AppState.Connecting || state is AppState.Connected) return
        state = AppState.Connecting

        val specifier = WifiNetworkSpecifier.Builder()
            .setSsid(ssid)
            .setWpa2Passphrase(password)
            .build()

        val request = NetworkRequest.Builder()
            .addTransportType(NetworkCapabilities.TRANSPORT_WIFI)
            .removeCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
            .setNetworkSpecifier(specifier)
            .build()

        val connectivityManager = context.getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager

        networkCallback = object : ConnectivityManager.NetworkCallback() {
            override fun onAvailable(network: Network) {
                connectivityManager.bindProcessToNetwork(network)
                viewModelScope.launch(Dispatchers.IO) {
                    try {
                        val url = URL("http://192.168.4.1/api/wifi/status")
                        val conn = url.openConnection() as HttpURLConnection
                        conn.connectTimeout = 3000
                        conn.readTimeout = 3000
                        val response = conn.inputStream.bufferedReader().use { it.readText() }

                        val json = JSONObject(response)
                        val ssid = json.optString("ssid", "Unknown")
                        val ip = json.optString("ip", "192.168.4.1")
                        val clients = json.optInt("clients", 1)
                        val mac = json.optString("mac", json.optString("mc", "Unknown"))

                        withContext(Dispatchers.Main) {
                            state = AppState.Connected(ssid, ip, clients, mac)
                            startPolling()
                        }
                    } catch (e: Exception) {
                        withContext(Dispatchers.Main) {
                            disconnect(context, "Failed to reach Socket API: ${e.message}")
                        }
                    }
                }
            }

            override fun onUnavailable() {
                viewModelScope.launch(Dispatchers.Main) {
                    disconnect(context, "Connection failed or user cancelled.")
                }
            }

            override fun onLost(network: Network) {
                viewModelScope.launch(Dispatchers.Main) {
                    disconnect(context, "Wi-Fi Connection lost.")
                }
            }
        }

        try {
            connectivityManager.requestNetwork(request, networkCallback!!)
        } catch (e: SecurityException) {
            disconnect(context, "Location permission is required for Wi-Fi scanning.")
        }
    }

    private fun startPolling() {
        pollingJob?.cancel()
        pollingJob = viewModelScope.launch(Dispatchers.IO) {
            var errorCount = 0
            while (isActive && (state is AppState.Connected)) {
                try {
                    val url = URL("http://192.168.4.1/api/energy")
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
                } catch (ignored: Exception) {
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
        val connectivityManager = context.getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
        networkCallback?.let {
            try {
                connectivityManager.unregisterNetworkCallback(it)
            } catch (ignored: Exception) {
                // Ignore if not registered
            }
        }
        networkCallback = null
        try {
            connectivityManager.bindProcessToNetwork(null)
        } catch (ignored: Exception) {
            // Ignore
        }

        pollingJob?.cancel()
        state = AppState.Disconnected

        reason?.let {
            Toast.makeText(context, it, Toast.LENGTH_SHORT).show()
        }
    }
}
