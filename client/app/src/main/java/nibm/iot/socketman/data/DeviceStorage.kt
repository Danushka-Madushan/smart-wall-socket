package nibm.iot.socketman.data

import android.content.Context
import android.content.SharedPreferences
import org.json.JSONArray
import org.json.JSONObject

class DeviceStorage(context: Context) {
    private val prefs: SharedPreferences = context.getSharedPreferences("saved_smart_switches", Context.MODE_PRIVATE)

    fun getSavedDevices(): List<SavedDevice> {
        val jsonString = prefs.getString("devices_json", null) ?: return emptyList()
        val list = mutableListOf<SavedDevice>()
        try {
            val jsonArray = JSONArray(jsonString)
            for (i in 0 until jsonArray.length()) {
                val obj = jsonArray.getJSONObject(i)
                list.add(
                    SavedDevice(
                        id = obj.getString("id"),
                        ip = obj.getString("ip"),
                        masterToken = obj.getString("masterToken"),
                        pop = obj.optString("pop", ""),
                        switches = obj.optInt("switches", 2),
                        claimedAt = obj.optLong("claimedAt", System.currentTimeMillis())
                    )
                )
            }
        } catch (e: Exception) {
            e.printStackTrace()
        }
        return list
    }

    fun saveDevice(device: SavedDevice) {
        val currentList = getSavedDevices().toMutableList()
        currentList.removeAll { it.id == device.id }
        currentList.add(device)
        persistList(currentList)
    }

    fun removeDevice(deviceId: String) {
        val currentList = getSavedDevices().toMutableList()
        currentList.removeAll { it.id == deviceId }
        persistList(currentList)
    }

    fun updateDeviceIp(deviceId: String, newIp: String) {
        val currentList = getSavedDevices().toMutableList()
        val index = currentList.indexOfFirst { it.id == deviceId }
        if (index != -1) {
            val existing = currentList[index]
            currentList[index] = existing.copy(ip = newIp)
            persistList(currentList)
        }
    }

    private fun persistList(list: List<SavedDevice>) {
        val jsonArray = JSONArray()
        for (device in list) {
            val obj = JSONObject().apply {
                put("id", device.id)
                put("ip", device.ip)
                put("masterToken", device.masterToken)
                put("pop", device.pop)
                put("switches", device.switches)
                put("claimedAt", device.claimedAt)
            }
            jsonArray.put(obj)
        }
        prefs.edit().putString("devices_json", jsonArray.toString()).apply()
    }
}
