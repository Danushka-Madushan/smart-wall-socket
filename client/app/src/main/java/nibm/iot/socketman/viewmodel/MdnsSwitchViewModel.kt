package nibm.iot.socketman.viewmodel

import android.content.Context
import androidx.compose.runtime.getValue
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
import nibm.iot.socketman.network.MdnsDiscoveryManager
import nibm.iot.socketman.network.SmartSwitchApiClient
import java.security.SecureRandom
import kotlin.time.Duration.Companion.seconds

class MdnsSwitchViewModel(context: Context) : ViewModel() {
    private val storage = DeviceStorage(context)
    private val discoveryManager = MdnsDiscoveryManager(context)
    private val apiClient = SmartSwitchApiClient()

    private val _savedDevices = MutableStateFlow<List<SavedDevice>>(emptyList())
    val savedDevices: StateFlow<List<SavedDevice>> = _savedDevices.asStateFlow()

    val discoveredDevices: StateFlow<List<MdnsDevice>> = discoveryManager.devices
    val isDiscovering: StateFlow<Boolean> = discoveryManager.isDiscovering

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

    var isClaiming by mutableStateOf(false)
        private set

    var claimErrorMessage by mutableStateOf<String?>(null)
        private set

    private var heartbeatJob: Job? = null
    private var detailPollingJob: Job? = null

    init {
        loadSavedDevices()
        startHeartbeatPolling()
        
        // Auto IP-Recovery listener: Whenever mDNS discovers a device whose IP changed, sync storage
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

    fun generateMasterToken(): String {
        val randomBytes = ByteArray(32) // 256 bits of entropy
        SecureRandom().nextBytes(randomBytes)
        return randomBytes.joinToString("") { "%02x".format(it) }
    }

    fun claimDevice(ip: String, pop: String, targetDeviceId: String, onResult: (Boolean, String) -> Unit) {
        if (isClaiming) return
        isClaiming = true
        claimErrorMessage = null

        viewModelScope.launch(Dispatchers.IO) {
            val generatedToken = generateMasterToken()
            val response = apiClient.claimDevice(ip, pop, generatedToken)

            withContext(Dispatchers.Main) {
                isClaiming = false
                if (response.isSuccess) {
                    val newDevice = SavedDevice(
                        id = targetDeviceId,
                        ip = ip,
                        masterToken = generatedToken,
                        pop = pop,
                        switches = 2
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

    fun selectDeviceForControl(device: SavedDevice) {
        selectedSavedDevice = device
        activeRelayStates = emptyMap()
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

                withContext(Dispatchers.Main) {
                    if (hb != null) selectedHeartbeat = hb
                    if (info != null) selectedDeviceInfo = info
                    if (wifi != null) selectedWifiStatus = wifi
                    if (relays != null) activeRelayStates = relays
                }

                delay(2.seconds) // Refresh active device controls every 2s
            }
        }
    }

    override fun onCleared() {
        super.onCleared()
        discoveryManager.stopDiscovery()
        heartbeatJob?.cancel()
        detailPollingJob?.cancel()
    }
}
