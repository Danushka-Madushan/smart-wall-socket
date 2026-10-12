package nibm.iot.socketman.data

data class MdnsDevice(
    val id: String,
    val name: String,
    val ip: String,
    val port: Int = 80,
    val switches: Int = 2,
    val energy: Boolean = false,
    val transport: String = "lan",
    val claimed: Boolean = false,
)

data class BleDiscoveredDevice(
    val name: String,
    val pop: String = "",
    val address: String = "",
)

data class ClaimResponse(
    val isSuccess: Boolean,
    val status: String? = null,
    val message: String? = null,
    val error: String? = null,
)

fun formatUptime(seconds: Long): String {
    if (seconds <= 0) return "0s"
    val hrs = seconds / 3600
    val mins = (seconds % 3600) / 60
    val secs = seconds % 60
    return if (hrs > 0) {
        "${hrs}h ${mins}m ${secs}s"
    } else if (mins > 0) {
        "${mins}m ${secs}s"
    } else {
        "${secs}s"
    }
}

data class DeviceInfoResponse(
    val id: String,
    val type: String,
    val switches: Int,
    val energyMonitoring: Boolean,
    val claimed: Boolean,
    val transport: String,
    val relays: Map<String, Boolean>,
)

data class HeartbeatResponse(
    val ack: Boolean,
    val id: String,
    val claimed: Boolean,
    val uptimeSeconds: Long,
    val relays: Map<String, Boolean>,
)

data class WifiStatusResponse(
    val connected: Boolean,
    val ssid: String,
    val ip: String,
    val mac: String,
    val rssi: Int,
    val claimed: Boolean,
)

data class EnergyData(
    val currentA: Float = 0f,
    val powerW: Float = 0f,
    val voltageV: Float = 0f,
    val apparentVa: Float = 0f,
    val powerFactor: Float = 1.0f,
    val relay: Boolean = true,
)

data class SavedDevice(
    val id: String,
    val ip: String,
    val masterToken: String,
    val pop: String,
    val switches: Int = 2,
    val hasEnergy: Boolean = false,
    val transport: String = "lan",
    val claimedAt: Long = System.currentTimeMillis(),
)
