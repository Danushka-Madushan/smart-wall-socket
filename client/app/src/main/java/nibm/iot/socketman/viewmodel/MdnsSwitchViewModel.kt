package nibm.iot.socketman.viewmodel

import android.content.Context
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableDoubleStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import nibm.iot.socketman.data.*
import nibm.iot.socketman.network.BleProvisioningManager
import nibm.iot.socketman.network.MdnsDiscoveryManager
import nibm.iot.socketman.network.SmartSwitchApiClient
import java.security.SecureRandom
import kotlin.math.min
import kotlin.time.Duration.Companion.seconds

class MdnsSwitchViewModel(context: Context) : ViewModel() {
    private val storage = DeviceStorage(context)
    private val discoveryManager = MdnsDiscoveryManager(context)
    private val bleManager = BleProvisioningManager(context)
    private val apiClient = SmartSwitchApiClient()

    private val _savedDevices = MutableStateFlow<List<SavedDevice>>(emptyList())
    val savedDevices: StateFlow<List<SavedDevice>> = _savedDevices.asStateFlow()

    val discoveredDevices: StateFlow<List<MdnsDevice>> = discoveryManager.devices
    val isDiscovering: StateFlow<Boolean> = discoveryManager.isDiscovering

    val bleDiscoveredDevices: StateFlow<List<BleDiscoveredDevice>> = bleManager.discoveredBleDevices
    val isBleScanning: StateFlow<Boolean> = bleManager.isScanning

    var deviceOnlineMap by mutableStateOf<Map<String, Boolean>>(emptyMap())
        private set

    var deviceRelayMap by mutableStateOf<Map<String, Map<String, Boolean>>>(emptyMap())
        private set

    var selectedSavedDevice by mutableStateOf<SavedDevice?>(null)
        private set

    var selectedDeviceInfo by mutableStateOf<DeviceInfoResponse?>(null)
        private set

    var selectedWifiStatus by mutableStateOf<WifiStatusResponse?>(null)
        private set

    var selectedHeartbeat by mutableStateOf<HeartbeatResponse?>(null)
        private set

    var activeRelayStates by mutableStateOf<Map<String, Boolean>>(emptyMap())
        private set

    var activeEnergyData by mutableStateOf(EnergyData())
        private set

    var totalUnits by mutableDoubleStateOf(0.0)
        private set

    var isClaiming by mutableStateOf(false)
        private set

    var claimErrorMessage by mutableStateOf<String?>(null)
        private set

    var provisioningStatusText by mutableStateOf<String?>(null)
        private set

    private var heartbeatJob: Job? = null
    private var detailPollingJob: Job? = null

    init {
        loadSavedDevices()
        startHeartbeatPolling()

        viewModelScope.launch {
            discoveredDevices.collect { discoveredList ->
                val saved = _savedDevices.value
                var updated = false
                for (discovered in discoveredList) {
                    val match = saved.find { it.id == discovered.id }
                    if (match != null && match.ip != discovered.ip) {
                        storage.updateDeviceIp(discovered.id, discovered.ip)
                        updated = true
                    }
                }
                if (updated) {
                    loadSavedDevices()
                }
            }
        }
    }

    fun loadSavedDevices() {
        _savedDevices.value = storage.getSavedDevices()
    }

    fun startDiscovery() {
        discoveryManager.startDiscovery()
    }

    fun stopDiscovery() {
        discoveryManager.stopDiscovery()
    }

    fun startBleScan() {
        bleManager.startBleScan()
    }

    fun stopBleScan() {
        bleManager.stopBleScan()
    }

    fun generateMasterToken(): String {
        val randomBytes = ByteArray(32) // 256 bits of entropy
        SecureRandom().nextBytes(randomBytes)
        return randomBytes.joinToString("") { "%02x".format(it) }
    }

    fun claimDevice(
        ip: String,
        pop: String,
        targetDeviceId: String,
        switches: Int = 2,
        hasEnergy: Boolean = false,
        transport: String = "lan",
        onResult: (Boolean, String) -> Unit
    ) {
        if (isClaiming) return
        isClaiming = true
        claimErrorMessage = null

        viewModelScope.launch(Dispatchers.IO) {
            val generatedToken = generateMasterToken()
            val response = apiClient.claimDevice(ip, pop, generatedToken)
            val info = if (response.isSuccess) apiClient.getInfo(ip) else null

            withContext(Dispatchers.Main) {
                isClaiming = false
                if (response.isSuccess) {
                    val finalSwitches = info?.switches ?: switches
                    val finalHasEnergy = if (info != null) info.energyMonitoring else hasEnergy
                    val finalTransport = info?.transport ?: transport
                    val finalId = if (info != null && info.id.isNotBlank() && info.id != "Unknown") info.id else targetDeviceId

                    val newDevice = SavedDevice(
                        id = finalId,
                        ip = ip,
                        masterToken = generatedToken,
                        pop = pop,
                        switches = finalSwitches,
                        hasEnergy = finalHasEnergy,
                        transport = finalTransport
                    )
                    storage.saveDevice(newDevice)
                    loadSavedDevices()
                    onResult(true, response.message ?: "Device claimed successfully!")
                } else {
                    val err = response.error ?: "Claim rejected by device."
                    claimErrorMessage = err
                    onResult(false, err)
                }
            }
        }
    }

    fun provisionAndClaimBleDevice(
        deviceName: String,
        popPin: String,
        ssid: String,
        pass: String,
        targetIpOverride: String? = null,
        onResult: (Boolean, String) -> Unit
    ) {
        provisioningStatusText = "Connecting over BLE..."
        bleManager.provisionDeviceOverBle(
            deviceName = deviceName,
            popPin = popPin,
            ssid = ssid,
            pass = pass,
            targetIpOverride = targetIpOverride,
            scope = viewModelScope,
            onStatusUpdate = { status ->
                provisioningStatusText = status
            },
            onComplete = { success, assignedIp, errorMsg ->
                if (success && assignedIp != null) {
                    provisioningStatusText = "BLE Provisioned! Executing PoP Claim..."
                    claimDevice(
                        ip = assignedIp,
                        pop = popPin,
                        targetDeviceId = deviceName,
                        switches = 1,
                        hasEnergy = true,
                        transport = "ble"
                    ) { claimOk, claimMsg ->
                        provisioningStatusText = null
                        onResult(claimOk, claimMsg)
                    }
                } else {
                    provisioningStatusText = null
                    onResult(false, errorMsg ?: "Provisioning failed")
                }
            }
        )
    }

    fun selectDeviceForControl(device: SavedDevice) {
        selectedSavedDevice = device
        val cachedRelays = deviceRelayMap[device.id] ?: emptyMap()
        activeRelayStates = cachedRelays
        activeEnergyData = EnergyData(relay = cachedRelays["1"] ?: true)
        totalUnits = 0.0
        selectedDeviceInfo = null
        selectedWifiStatus = null
        selectedHeartbeat = null
        startDetailPolling(device)
    }

    fun clearSelectedDevice() {
        detailPollingJob?.cancel()
        selectedSavedDevice = null
    }

    fun toggleRelay(channel: Int, targetState: Boolean) {
        val device = selectedSavedDevice ?: return
        viewModelScope.launch(Dispatchers.IO) {
            val updated = apiClient.setRelay(device.ip, device.masterToken, channel, targetState)
            if (updated != null) {
                withContext(Dispatchers.Main) {
                    activeRelayStates = updated
                    if (channel == 1 && device.hasEnergy) {
                        activeEnergyData = activeEnergyData.copy(relay = updated["1"] ?: targetState)
                    }
                }
            }
        }
    }

    fun toggleAllRelays(targetState: Boolean) {
        val device = selectedSavedDevice ?: return
        viewModelScope.launch(Dispatchers.IO) {
            val updated = apiClient.setRelayAll(device.ip, device.masterToken, targetState)
            if (updated != null) {
                withContext(Dispatchers.Main) {
                    activeRelayStates = updated
                }
            }
        }
    }

    fun unclaimCurrentDevice(onComplete: (Boolean) -> Unit) {
        val device = selectedSavedDevice ?: return
        viewModelScope.launch(Dispatchers.IO) {
            val success = apiClient.unclaimDevice(device.ip, device.masterToken, device.pop)
            withContext(Dispatchers.Main) {
                storage.removeDevice(device.id)
                loadSavedDevices()
                clearSelectedDevice()
                onComplete(success)
            }
        }
    }

    fun verifyCurrentToken(onResult: (Boolean) -> Unit) {
        val device = selectedSavedDevice ?: return
        viewModelScope.launch(Dispatchers.IO) {
            val valid = apiClient.verifyToken(device.ip, device.masterToken)
            withContext(Dispatchers.Main) {
                onResult(valid)
            }
        }
    }

    fun calculateCost(units: Double): Double {
        var cost = 0.0
        var remainingUnits = units

        if (remainingUnits > 0) {
            val tier1 = min(remainingUnits, 60.0)
            cost += tier1 * 14.0
            remainingUnits -= tier1
        }
        if (remainingUnits > 0) {
            val tier2 = min(remainingUnits, 30.0)
            cost += tier2 * 20.0
            remainingUnits -= tier2
        }
        if (remainingUnits > 0) {
            cost += remainingUnits * 28.0
        }
        return cost
    }

    private fun startHeartbeatPolling() {
        heartbeatJob?.cancel()
        heartbeatJob = viewModelScope.launch(Dispatchers.IO) {
            while (isActive) {
                val devices = _savedDevices.value
                val newOnlineMap = mutableMapOf<String, Boolean>()
                val newRelayMap = mutableMapOf<String, Map<String, Boolean>>()

                for (device in devices) {
                    val hb = apiClient.getHeartbeat(device.ip)
                    if (hb != null && hb.ack) {
                        newOnlineMap[device.id] = true
                        newRelayMap[device.id] = hb.relays
                    } else {
                        newOnlineMap[device.id] = false
                    }
                }

                withContext(Dispatchers.Main) {
                    deviceOnlineMap = newOnlineMap
                    deviceRelayMap = newRelayMap
                }

                delay(3.seconds) // Poll heartbeats every 3s
            }
        }
    }

    private fun startDetailPolling(device: SavedDevice) {
        detailPollingJob?.cancel()
        detailPollingJob = viewModelScope.launch(Dispatchers.IO) {
            while (isActive) {
                val hb = apiClient.getHeartbeat(device.ip)
                val info = apiClient.getInfo(device.ip)
                val wifi = apiClient.getWifiStatus(device.ip)
                val relays = apiClient.getRelayStates(device.ip, device.masterToken)

                var energy: EnergyData? = null
                if (device.hasEnergy) {
                    energy = apiClient.getEnergy(device.ip)
                }

                withContext(Dispatchers.Main) {
                    if (hb != null) selectedHeartbeat = hb
                    if (info != null) selectedDeviceInfo = info
                    if (wifi != null) selectedWifiStatus = wifi
                    if (relays != null) activeRelayStates = relays
                    if (energy != null) {
                        activeEnergyData = energy
                        totalUnits += (energy.powerW / 3600000.0)
                    }
                }

                delay(1.seconds) // Refresh active device controls and telemetry every 1s
            }
        }
    }

    override fun onCleared() {
        super.onCleared()
        discoveryManager.stopDiscovery()
        bleManager.stopBleScan()
        heartbeatJob?.cancel()
        detailPollingJob?.cancel()
    }
}
