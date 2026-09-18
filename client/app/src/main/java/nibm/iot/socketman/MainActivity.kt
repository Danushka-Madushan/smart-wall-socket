package nibm.iot.socketman

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.net.NetworkRequest
import android.net.wifi.WifiNetworkSpecifier
import android.os.Bundle
import android.os.PatternMatcher
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape



import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier

import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.core.content.ContextCompat
import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.*
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL
import nibm.iot.socketman.ui.theme.SocketManTheme

// --- Models ---
sealed class AppState {
    object Disconnected : AppState()
    object Connecting : AppState()
    data class Connected(val ssid: String, val ip: String, val clients: Int, val mac: String) : AppState()
}

data class EnergyData(
    val currentA: Float = 0f,
    val powerW: Float = 0f,
    val voltageV: Float = 0f
)

// --- ViewModel ---
class SocketViewModel : ViewModel() {
    var state by mutableStateOf<AppState>(AppState.Connecting)
        private set

    var energyData by mutableStateOf(EnergyData())
        private set

    var totalUnits by mutableStateOf(0.0) // 1 unit = 1 kWh
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
            } catch (e: Exception) {
                withContext(Dispatchers.Main) {
                    state = AppState.Disconnected
                }
            }
        }
    }

    fun connect(context: Context, password: String) {
        if (state is AppState.Connecting || state is AppState.Connected) return
        state = AppState.Connecting

        val specifier = WifiNetworkSpecifier.Builder()
            .setSsidPattern(PatternMatcher("SmartSocket", PatternMatcher.PATTERN_PREFIX))
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
            while (isActive && state is AppState.Connected) {
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
                } catch (e: Exception) {
                    errorCount++
                    if (errorCount >= 3) {
                        withContext(Dispatchers.Main) {
                            state = AppState.Disconnected
                        }
                        break
                    }
                }
                delay(1000)
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
        } catch (e: Exception) {
            // Ignore
        }

        pollingJob?.cancel()
        state = AppState.Disconnected

        reason?.let {
            Toast.makeText(context, it, Toast.LENGTH_SHORT).show()
        }
    }
}

// --- UI ---
class MainActivity : ComponentActivity() {
    private val viewModel: SocketViewModel by lazy {
        ViewModelProvider(this)[SocketViewModel::class.java]
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        viewModel.initCheck()
        
        setContent {
            SocketManTheme {
                Scaffold(modifier = Modifier.fillMaxSize()) { innerPadding ->
                    MainScreen(
                        viewModel = viewModel,
                        modifier = Modifier.padding(innerPadding)
                    )
                }
            }
        }
    }
}

@Composable
fun MainScreen(viewModel: SocketViewModel, modifier: Modifier = Modifier) {
    val state = viewModel.state
    val context = LocalContext.current

    val permissionsLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.RequestMultiplePermissions()
    ) { permissions ->
        val fineLocationGranted = permissions[Manifest.permission.ACCESS_FINE_LOCATION] == true
        if (!fineLocationGranted) {
            Toast.makeText(context, "Location permission is required to find the Socket.", Toast.LENGTH_LONG).show()
        }
    }

    LaunchedEffect(Unit) {
        if (ContextCompat.checkSelfPermission(context, Manifest.permission.ACCESS_FINE_LOCATION) != PackageManager.PERMISSION_GRANTED) {
            permissionsLauncher.launch(arrayOf(Manifest.permission.ACCESS_FINE_LOCATION, Manifest.permission.ACCESS_COARSE_LOCATION))
        }
    }

    when (state) {
        is AppState.Disconnected, is AppState.Connecting -> {
            ConnectionScreen(
                isConnecting = state is AppState.Connecting,
                onConnect = { password -> viewModel.connect(context, password) },
                modifier = modifier
            )
        }
        is AppState.Connected -> {
            MonitoringScreen(
                info = state,
                energyData = viewModel.energyData,
                totalUnits = viewModel.totalUnits,
                onDisconnect = { viewModel.disconnect(context, "Disconnected by user") },
                modifier = modifier
            )
        }
    }
}

@Composable
fun ConnectionScreen(
    isConnecting: Boolean,
    onConnect: (String) -> Unit,
    modifier: Modifier = Modifier
) {
    var password by remember { mutableStateOf("") }

    Column(
        modifier = modifier
            .fillMaxSize()
            .padding(24.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center
    ) {
        Text(
            text = "⚡",
            fontSize = 80.sp
        )
        Spacer(modifier = Modifier.height(16.dp))
        Text(
            text = "SocketMan",
            style = MaterialTheme.typography.headlineLarge,
            fontWeight = FontWeight.Bold
        )
        Text(
            text = "Connect to your Smart Wall Socket",
            style = MaterialTheme.typography.bodyLarge,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )

        Spacer(modifier = Modifier.height(48.dp))

        OutlinedTextField(
            value = password,
            onValueChange = { password = it },
            label = { Text("Wi-Fi Password") },
            visualTransformation = PasswordVisualTransformation(),
            singleLine = true,
            modifier = Modifier.fillMaxWidth()
        )

        Spacer(modifier = Modifier.height(24.dp))

        Button(
            onClick = { onConnect(password) },
            enabled = password.isNotBlank() && !isConnecting,
            modifier = Modifier
                .fillMaxWidth()
                .height(56.dp),
            shape = RoundedCornerShape(12.dp)
        ) {
            if (isConnecting) {
                CircularProgressIndicator(
                    color = MaterialTheme.colorScheme.onPrimary,
                    modifier = Modifier.size(24.dp),
                    strokeWidth = 2.dp
                )
                Spacer(modifier = Modifier.width(12.dp))
                Text("Connecting...", fontSize = 18.sp)
            } else {
                Text("Connect", fontSize = 18.sp)
            }
        }
    }
}

@Composable
fun MonitoringScreen(
    info: AppState.Connected,
    energyData: EnergyData,
    totalUnits: Double,
    onDisconnect: () -> Unit,
    modifier: Modifier = Modifier
) {
    Column(
        modifier = modifier
            .fillMaxSize()
            .padding(24.dp),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        // Header
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("✅", fontSize = 24.sp)
                Spacer(modifier = Modifier.width(8.dp))
                Text(
                    text = info.ssid,
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.SemiBold
                )
            }
            IconButton(onClick = onDisconnect) {
                Text("❌", fontSize = 24.sp)
            }
        }

        Spacer(modifier = Modifier.height(32.dp))

        // Main Power Display
        Surface(
            shape = RoundedCornerShape(24.dp),
            color = MaterialTheme.colorScheme.primaryContainer,
            modifier = Modifier
                .fillMaxWidth()
                .aspectRatio(1f)
        ) {
            Column(
                modifier = Modifier.fillMaxSize(),
                verticalArrangement = Arrangement.Center,
                horizontalAlignment = Alignment.CenterHorizontally
            ) {
                Text(
                    text = "Current Power",
                    style = MaterialTheme.typography.titleMedium,
                    color = MaterialTheme.colorScheme.onPrimaryContainer
                )
                Text(
                    text = "${"%.1f".format(energyData.powerW)} W",
                    style = MaterialTheme.typography.displayLarge,
                    fontWeight = FontWeight.Bold,
                    color = MaterialTheme.colorScheme.onPrimaryContainer,
                    fontSize = 64.sp
                )
            }
        }

        Spacer(modifier = Modifier.height(24.dp))

        // Metrics Row
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(16.dp)
        ) {
            MetricCard(
                title = "Voltage",
                value = "${"%.1f".format(energyData.voltageV)} V",
                modifier = Modifier.weight(1f)
            )
            MetricCard(
                title = "Current",
                value = "${"%.2f".format(energyData.currentA)} A",
                modifier = Modifier.weight(1f)
            )
        }

        Spacer(modifier = Modifier.height(16.dp))

        // Total Energy
        Surface(
            shape = RoundedCornerShape(16.dp),
            color = MaterialTheme.colorScheme.secondaryContainer,
            modifier = Modifier.fillMaxWidth()
        ) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(20.dp),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    text = "Used Electricity",
                    style = MaterialTheme.typography.titleMedium,
                    color = MaterialTheme.colorScheme.onSecondaryContainer
                )
                Text(
                    text = "${"%.5f".format(totalUnits)} Units",
                    style = MaterialTheme.typography.titleLarge,
                    fontWeight = FontWeight.Bold,
                    color = MaterialTheme.colorScheme.onSecondaryContainer
                )
            }
        }

        Spacer(modifier = Modifier.weight(1f))

        Text(
            text = "IP: ${info.ip} | MAC: ${info.mac} | Clients: ${info.clients}",
            style = MaterialTheme.typography.labelSmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
    }
}

@Composable
fun MetricCard(title: String, value: String, modifier: Modifier = Modifier) {
    Surface(
        shape = RoundedCornerShape(16.dp),
        color = MaterialTheme.colorScheme.surfaceVariant,
        modifier = modifier
    ) {
        Column(
            modifier = Modifier.padding(16.dp),
            horizontalAlignment = Alignment.CenterHorizontally
        ) {
            Text(
                text = title,
                style = MaterialTheme.typography.labelLarge,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            Spacer(modifier = Modifier.height(8.dp))
            Text(
                text = value,
                style = MaterialTheme.typography.titleLarge,
                fontWeight = FontWeight.Bold,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
    }
}
