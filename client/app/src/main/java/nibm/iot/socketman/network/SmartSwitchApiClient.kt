package nibm.iot.socketman.network

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import nibm.iot.socketman.data.ClaimResponse
import nibm.iot.socketman.data.DeviceInfoResponse
import nibm.iot.socketman.data.HeartbeatResponse
import nibm.iot.socketman.data.WifiStatusResponse
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL

class SmartSwitchApiClient {

    suspend fun getInfo(ip: String): DeviceInfoResponse? = withContext(Dispatchers.IO) {
        try {
            val json = getRequest("http://$ip/api/info") ?: return@withContext null
            val relaysMap = mutableMapOf<String, Boolean>()
            val relaysObj = json.optJSONObject("relays")
            if (relaysObj != null) {
                val keys = relaysObj.keys()
                while (keys.hasNext()) {
                    val key = keys.next()
                    relaysMap[key] = relaysObj.optBoolean(key, false)
                }
            }
            DeviceInfoResponse(
                id = json.optString("id", "Unknown"),
                type = json.optString("type", "node"),
                switches = json.optInt("switches", 2),
                energyMonitoring = json.optBoolean("energy_monitoring", false),
                claimed = json.optBoolean("claimed", false),
                transport = json.optString("transport", "lan"),
                relays = relaysMap,
            )
        } catch (_: Exception) {
            null
        }
    }

    suspend fun getHeartbeat(ip: String): HeartbeatResponse? = withContext(Dispatchers.IO) {
        try {
            val json = getRequest("http://$ip/api/heartbeat") ?: return@withContext null
            val relaysMap = mutableMapOf<String, Boolean>()
            val relaysObj = json.optJSONObject("relays")
            if (relaysObj != null) {
                val keys = relaysObj.keys()
                while (keys.hasNext()) {
                    val key = keys.next()
                    relaysMap[key] = relaysObj.optBoolean(key, false)
                }
            }
            HeartbeatResponse(
                ack = json.optBoolean("ack", false),
                id = json.optString("id", "Unknown"),
                claimed = json.optBoolean("claimed", false),
                uptimeSeconds = json.optLong("uptime_s", 0L),
                relays = relaysMap,
            )
        } catch (_: Exception) {
            null
        }
    }

    suspend fun getWifiStatus(ip: String): WifiStatusResponse? = withContext(Dispatchers.IO) {
        try {
            val json = getRequest("http://$ip/api/wifi/status") ?: return@withContext null
            WifiStatusResponse(
                connected = json.optBoolean("connected", true),
                ssid = json.optString("ssid", "Unknown"),
                ip = json.optString("ip", ip),
                mac = json.optString("mac", json.optString("mc", "Unknown")),
                rssi = json.optInt("rssi", 0),
                claimed = json.optBoolean("claimed", false),
            )
        } catch (_: Exception) {
            null
        }
    }

    suspend fun claimDevice(ip: String, pop: String, masterToken: String): ClaimResponse = withContext(Dispatchers.IO) {
        try {
            val body = JSONObject().apply {
                put("pop", pop)
                put("master_token", masterToken)
            }.toString()

            val (code, responseStr) = postRequest("http://$ip/api/claim", body, null)
            val json = if (responseStr.isNotBlank()) JSONObject(responseStr) else JSONObject()

            when (code) {
                200 -> ClaimResponse(
                    isSuccess = true,
                    status = json.optString("status", "claimed"),
                    message = json.optString("message", "Device successfully claimed"),
                )
                403 -> ClaimResponse(
                    isSuccess = false,
                    error = json.optString("error", "Invalid Proof of Possession (PoP)"),
                )
                409 -> ClaimResponse(
                    isSuccess = false,
                    error = json.optString("error", "Device already claimed"),
                )
                else -> ClaimResponse(
                    isSuccess = false,
                    error = json.optString("error", "Claim failed (HTTP $code)"),
                )
            }
        } catch (e: Exception) {
            ClaimResponse(isSuccess = false, error = e.message ?: "Network error during claim")
        }
    }

    suspend fun verifyToken(ip: String, masterToken: String): Boolean = withContext(Dispatchers.IO) {
        try {
            val (code, responseStr) = getRequestWithHeaders("http://$ip/api/verify", masterToken)
            if (code == 200) {
                val json = JSONObject(responseStr)
                json.optBoolean("verified", false)
            } else {
                false
            }
        } catch (_: Exception) {
            false
        }
    }

    suspend fun getRelayStates(ip: String, masterToken: String): Map<String, Boolean>? = withContext(Dispatchers.IO) {
        try {
            val (code, responseStr) = getRequestWithHeaders("http://$ip/api/relay", masterToken)
            if (code == 200) {
                val json = JSONObject(responseStr)
                val map = mutableMapOf<String, Boolean>()
                val keys = json.keys()
                while (keys.hasNext()) {
                    val key = keys.next()
                    map[key] = json.optBoolean(key, false)
                }
                map
            } else {
                null
            }
        } catch (_: Exception) {
            null
        }
    }

    suspend fun setRelay(ip: String, masterToken: String, channel: Int, state: Boolean): Map<String, Boolean>? = withContext(Dispatchers.IO) {
        try {
            val body = JSONObject().apply {
                put("relay", channel)
                put("state", state)
            }.toString()

            val (code, responseStr) = postRequest("http://$ip/api/relay", body, masterToken)
            if (code == 200) {
                parseRelayStateResponse(responseStr)
            } else {
                null
            }
        } catch (_: Exception) {
            null
        }
    }

    suspend fun setRelayAll(ip: String, masterToken: String, state: Boolean): Map<String, Boolean>? = withContext(Dispatchers.IO) {
        try {
            val body = JSONObject().apply {
                put("state", state)
            }.toString()

            val (code, responseStr) = postRequest("http://$ip/api/relay/all", body, masterToken)
            if (code == 200) {
                parseRelayStateResponse(responseStr)
            } else {
                null
            }
        } catch (_: Exception) {
            null
        }
    }

    suspend fun unclaimDevice(ip: String, masterToken: String?, pop: String?): Boolean = withContext(Dispatchers.IO) {
        try {
            val body = if (!pop.isNullOrBlank()) {
                JSONObject().apply { put("pop", pop) }.toString()
            } else {
                "{}"
            }
            val (code, _) = postRequest("http://$ip/api/unclaim", body, masterToken)
            code == 200
        } catch (_: Exception) {
            false
        }
    }

    private fun parseRelayStateResponse(responseStr: String): Map<String, Boolean> {
        val map = mutableMapOf<String, Boolean>()
        try {
            val json = JSONObject(responseStr)
            val relaysObj = json.optJSONObject("relays") ?: json
            val keys = relaysObj.keys()
            while (keys.hasNext()) {
                val key = keys.next()
                if (key != "success") {
                    map[key] = relaysObj.optBoolean(key, false)
                }
            }
        } catch (e: Exception) {
            e.printStackTrace()
        }
        return map
    }

    private fun getRequest(urlStr: String): JSONObject? {
        val url = URL(urlStr)
        val conn = url.openConnection() as HttpURLConnection
        conn.connectTimeout = 3000
        conn.readTimeout = 3000
        return if (conn.responseCode == 200) {
            val text = conn.inputStream.bufferedReader().use { it.readText() }
            JSONObject(text)
        } else {
            null
        }
    }

    private fun getRequestWithHeaders(urlStr: String, token: String): Pair<Int, String> {
        val url = URL(urlStr)
        val conn = url.openConnection() as HttpURLConnection
        conn.connectTimeout = 3000
        conn.readTimeout = 3000
        conn.setRequestProperty("Authorization", "Bearer $token")
        conn.setRequestProperty("X-Master-Token", token)
        val code = conn.responseCode
        val stream = if (code in 200..299) conn.inputStream else conn.errorStream
        val text = stream?.bufferedReader()?.use { it.readText() } ?: ""
        return Pair(code, text)
    }

    private fun postRequest(urlStr: String, jsonBody: String, token: String?): Pair<Int, String> {
        val url = URL(urlStr)
        val conn = url.openConnection() as HttpURLConnection
        conn.requestMethod = "POST"
        conn.connectTimeout = 4000
        conn.readTimeout = 4000
        conn.setRequestProperty("Content-Type", "application/json")
        if (!token.isNullOrBlank()) {
            conn.setRequestProperty("Authorization", "Bearer $token")
            conn.setRequestProperty("X-Master-Token", token)
        }
        conn.doOutput = true
        conn.outputStream.use { os ->
            os.write(jsonBody.toByteArray(Charsets.UTF_8))
        }

        val code = conn.responseCode
        val stream = if (code in 200..299) conn.inputStream else conn.errorStream
        val text = stream?.bufferedReader()?.use { it.readText() } ?: ""
        return Pair(code, text)
    }
}
