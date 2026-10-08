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

data class QrPayload(
    val type: String,
    val id: String,
    val pop: String,
    val transport: String,
)

data class ClaimRequest(
    val pop: String,
    val masterToken: String,
)

data class ClaimResponse(
    val isSuccess: Boolean,
    val status: String? = null,
    val message: String? = null,
    val error: String? = null,
)

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

data class SavedDevice(
    val id: String,
    val ip: String,
    val masterToken: String,
    val pop: String,
    val switches: Int = 2,
    val claimedAt: Long = System.currentTimeMillis(),
)
