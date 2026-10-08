package nibm.iot.socketman.network

import android.content.Context
import android.net.nsd.NsdManager
import android.net.nsd.NsdServiceInfo
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import nibm.iot.socketman.data.MdnsDevice

class MdnsDiscoveryManager(context: Context) {
    private val nsdManager = context.getSystemService(Context.NSD_SERVICE) as NsdManager
    private var discoveryListener: NsdManager.DiscoveryListener? = null

    private val _devices = MutableStateFlow<List<MdnsDevice>>(emptyList())
    val devices: StateFlow<List<MdnsDevice>> = _devices.asStateFlow()

    private val _isDiscovering = MutableStateFlow(false)
    val isDiscovering: StateFlow<Boolean> = _isDiscovering.asStateFlow()

    fun startDiscovery() {
        if (_isDiscovering.value) return

        _devices.value = emptyList()
        _isDiscovering.value = true

        discoveryListener = object : NsdManager.DiscoveryListener {
            override fun onDiscoveryStarted(regType: String) {
                _isDiscovering.value = true
            }

            override fun onServiceFound(service: NsdServiceInfo) {
                // Filter for smartswitch/smartsocket services
                val serviceName = service.serviceName ?: ""
                if (serviceName.contains("smartsocket", ignoreCase = true) ||
                    serviceName.contains("node", ignoreCase = true) ||
                    serviceName.contains("relay", ignoreCase = true)
                ) {
                    resolveService(service)
                }
            }

            override fun onServiceLost(service: NsdServiceInfo) {
                val lostName = service.serviceName ?: return
                _devices.value = _devices.value.filterNot { it.name == lostName }
            }

            override fun onDiscoveryStopped(serviceType: String) {
                _isDiscovering.value = false
            }

            override fun onStartDiscoveryFailed(serviceType: String, errorCode: Int) {
                _isDiscovering.value = false
                stopDiscovery()
            }

            override fun onStopDiscoveryFailed(serviceType: String, errorCode: Int) {
                _isDiscovering.value = false
            }
        }

        try {
            nsdManager.discoverServices("_http._tcp.", NsdManager.PROTOCOL_DNS_SD, discoveryListener)
        } catch (e: Exception) {
            e.printStackTrace()
            _isDiscovering.value = false
        }
    }

    @Suppress("DEPRECATION")
    private fun resolveService(service: NsdServiceInfo) {
        nsdManager.resolveService(service, object : NsdManager.ResolveListener {
            override fun onResolveFailed(serviceInfo: NsdServiceInfo, errorCode: Int) {
                // Resolve failure ignored
            }

            override fun onServiceResolved(serviceInfo: NsdServiceInfo) {
                val host = serviceInfo.host ?: return
                val ip = host.hostAddress ?: return
                if (ip.isBlank()) return

                val name = serviceInfo.serviceName ?: "Unknown Switch"
                val port = if (serviceInfo.port > 0) serviceInfo.port else 80

                // Extract TXT records
                val attributes = serviceInfo.attributes ?: emptyMap()
                
                val idStr = attributes["id"]?.let { String(it, Charsets.UTF_8) } ?: name
                val switchesCount = attributes["switches"]?.let { String(it, Charsets.UTF_8).toIntOrNull() } ?: 2
                val energyMonitoring = attributes["energy"]?.let { String(it, Charsets.UTF_8) == "1" } ?: false
                val transportType = attributes["transport"]?.let { String(it, Charsets.UTF_8) } ?: "lan"
                val isClaimed = attributes["claimed"]?.let { String(it, Charsets.UTF_8) == "1" } ?: false

                val mdnsDevice = MdnsDevice(
                    id = idStr,
                    name = name,
                    ip = ip,
                    port = port,
                    switches = switchesCount,
                    energy = energyMonitoring,
                    transport = transportType,
                    claimed = isClaimed
                )

                val current = _devices.value.toMutableList()
                current.removeAll { it.id == mdnsDevice.id || it.ip == mdnsDevice.ip }
                current.add(mdnsDevice)
                _devices.value = current
            }
        })
    }

    fun stopDiscovery() {
        discoveryListener?.let {
            try {
                nsdManager.stopServiceDiscovery(it)
            } catch (ignored: Exception) {
                // Ignore if not discovering
            }
        }
        discoveryListener = null
        _isDiscovering.value = false
    }
}
