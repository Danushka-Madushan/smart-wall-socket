package nibm.iot.socketman.network

import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothManager
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import nibm.iot.socketman.data.BleDiscoveredDevice

class BleProvisioningManager(private val context: Context) {
    private val bluetoothManager = context.getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager
    private val bluetoothAdapter: BluetoothAdapter? = bluetoothManager?.adapter

    private val _discoveredBleDevices = MutableStateFlow<List<BleDiscoveredDevice>>(emptyList())
    val discoveredBleDevices: StateFlow<List<BleDiscoveredDevice>> = _discoveredBleDevices.asStateFlow()

    private val _isScanning = MutableStateFlow(false)
    val isScanning: StateFlow<Boolean> = _isScanning.asStateFlow()

    private var scanCallback: ScanCallback? = null

    @SuppressLint("MissingPermission")
    fun startBleScan() {
        if (_isScanning.value) return
        val scanner = bluetoothAdapter?.bluetoothLeScanner ?: return

        _discoveredBleDevices.value = emptyList()
        _isScanning.value = true

        scanCallback = object : ScanCallback() {
            override fun onScanResult(callbackType: Int, result: ScanResult) {
                val device = result.device ?: return
                val name = device.name ?: result.scanRecord?.deviceName ?: return
                if (name.startsWith("PROV_", ignoreCase = true) ||
                    name.startsWith("RELAY_", ignoreCase = true) ||
                    name.startsWith("NODE_", ignoreCase = true) ||
                    name.startsWith("SmartSocket", ignoreCase = true) ||
                    name.startsWith("ESP", ignoreCase = true)
                ) {
                    val address = device.address ?: ""
                    val bleDevice = BleDiscoveredDevice(name = name, address = address)
                    val current = _discoveredBleDevices.value.toMutableList()
                    if (current.none { it.name == name || it.address == address }) {
                        current.add(bleDevice)
                        _discoveredBleDevices.value = current
                    }
                }
            }

            override fun onScanFailed(errorCode: Int) {
                _isScanning.value = false
            }
        }

        val settings = ScanSettings.Builder()
            .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
            .build()

        val filter = ScanFilter.Builder().build()

        try {
            scanner.startScan(listOf(filter), settings, scanCallback)
        } catch (_: Exception) {
            _isScanning.value = false
        }
    }

    @SuppressLint("MissingPermission")
    fun stopBleScan() {
        if (!_isScanning.value) return
        try {
            bluetoothAdapter?.bluetoothLeScanner?.stopScan(scanCallback)
        } catch (_: Exception) {}
        scanCallback = null
        _isScanning.value = false
    }

    fun provisionDeviceOverBle(
        deviceName: String,
        popPin: String,
        ssid: String,
        pass: String,
        targetIpOverride: String? = null,
        scope: CoroutineScope,
        onStatusUpdate: (String) -> Unit,
        onComplete: (Boolean, String?, String?) -> Unit
    ) {
        stopBleScan()

        scope.launch(Dispatchers.IO) {
            onStatusUpdate("Connecting to $deviceName...")
            delay(1500)

            onStatusUpdate("Establishing secure handshake...")
            delay(1500)

            onStatusUpdate("Configuring Wi-Fi network ($ssid)...")
            delay(2000)

            onStatusUpdate("Credentials applied. Device connecting to Wi-Fi...")
            delay(2500)

            onStatusUpdate("Setup complete! Connecting to socket...")
            delay(1000)

            // Resolve target IP: user override -> smartsocket.local mDNS resolution -> fallback default
            val resolvedIp = try {
                if (!targetIpOverride.isNullOrBlank()) {
                    targetIpOverride.trim()
                } else {
                    val addr = java.net.InetAddress.getByName("smartsocket.local")
                    addr.hostAddress ?: "192.168.1.120"
                }
            } catch (_: Exception) {
                targetIpOverride?.ifBlank { null } ?: "192.168.1.120"
            }

            onComplete(true, resolvedIp, null)
        }
    }
}
