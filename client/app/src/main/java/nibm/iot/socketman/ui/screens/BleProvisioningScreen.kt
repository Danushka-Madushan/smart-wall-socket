package nibm.iot.socketman.ui.screens

import android.Manifest
import android.content.pm.PackageManager
import android.widget.Toast
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.BorderStroke
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
import nibm.iot.socketman.data.BleDiscoveredDevice
import nibm.iot.socketman.viewmodel.MdnsSwitchViewModel
import org.json.JSONObject

@Composable
fun BleProvisioningScreen(
    bleDevice: BleDiscoveredDevice,
    viewModel: MdnsSwitchViewModel,
    onNavigateBack: () -> Unit,
    onProvisionSuccess: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val context = LocalContext.current

    var popPinInput by remember { mutableStateOf("S3_SEC_9821") }
    var wifiSsidInput by remember { mutableStateOf("") }
    var wifiPassInput by remember { mutableStateOf("") }

    var hasCameraPermission by remember {
        mutableStateOf(
            ContextCompat.checkSelfPermission(context, Manifest.permission.CAMERA) == PackageManager.PERMISSION_GRANTED
        )
    }

    val permissionLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.RequestPermission()
    ) { isGranted ->
        hasCameraPermission = isGranted
    }

    LaunchedEffect(Unit) {
        if (!hasCameraPermission) {
            permissionLauncher.launch(Manifest.permission.CAMERA)
        }
    }

    Column(
        modifier = modifier
            .fillMaxSize()
            .padding(24.dp),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        Spacer(modifier = Modifier.height(16.dp))

        // Navigation Header
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            IconButton(onClick = onNavigateBack) {
                Text("←", fontSize = 24.sp)
            }

            Text(
                text = "ESP32-S BLE Setup",
                style = MaterialTheme.typography.titleLarge,
                fontWeight = FontWeight.Bold,
                color = MaterialTheme.colorScheme.onSurface
            )

            Spacer(modifier = Modifier.width(48.dp))
        }

        Spacer(modifier = Modifier.height(16.dp))

        // Target Device Box
        Surface(
            shape = RoundedCornerShape(16.dp),
            color = MaterialTheme.colorScheme.surface,
            border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant),
            modifier = Modifier.fillMaxWidth()
        ) {
            Column(modifier = Modifier.padding(16.dp)) {
                Text(
                    text = "Target Device: ${bleDevice.name}",
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.Bold
                )
                Text(
                    text = "Connection: Bluetooth (Secure Setup)",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        }

        Spacer(modifier = Modifier.height(16.dp))

        // Camera QR Preview if needed
        if (hasCameraPermission) {
            Surface(
                shape = RoundedCornerShape(16.dp),
                border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant),
                modifier = Modifier
                    .fillMaxWidth()
                    .height(140.dp)
            ) {
                QrCameraPreview(
                    onQrScanned = { rawPayload ->
                        val qrInfo = parseBleQr(rawPayload)
                        if (qrInfo.popSecret.isNotBlank()) {
                            popPinInput = qrInfo.popSecret
                            if (qrInfo.deviceId != null && !bleDevice.name.contains(qrInfo.deviceId, ignoreCase = true) && !qrInfo.deviceId.contains(bleDevice.name, ignoreCase = true)) {
                                Toast.makeText(
                                    context,
                                    "Warning: Scanned QR ID (${qrInfo.deviceId}) differs from target BLE device (${bleDevice.name})",
                                    Toast.LENGTH_LONG
                                 ).show()
                            } else {
                                Toast.makeText(context, "Setup PIN Extracted from QR Code!", Toast.LENGTH_SHORT).show()
                            }
                        }
                    }
                )
            }
        }

        Spacer(modifier = Modifier.height(16.dp))

        // Inputs Form
        OutlinedTextField(
            value = popPinInput,
            onValueChange = { popPinInput = it },
            label = { Text("Device Setup PIN (PoP)") },
            singleLine = true,
            modifier = Modifier.fillMaxWidth()
        )

        Spacer(modifier = Modifier.height(8.dp))

        OutlinedTextField(
            value = wifiSsidInput,
            onValueChange = { wifiSsidInput = it },
            label = { Text("Target Wi-Fi SSID") },
            placeholder = { Text("e.g. HomeWiFi") },
            singleLine = true,
            modifier = Modifier.fillMaxWidth()
        )

        Spacer(modifier = Modifier.height(8.dp))

        OutlinedTextField(
            value = wifiPassInput,
            onValueChange = { wifiPassInput = it },
            label = { Text("Target Wi-Fi Password") },
            visualTransformation = PasswordVisualTransformation(),
            singleLine = true,
            modifier = Modifier.fillMaxWidth()
        )

        viewModel.provisioningStatusText?.let { status ->
            Spacer(modifier = Modifier.height(12.dp))
            Text(
                text = status,
                style = MaterialTheme.typography.bodySmall,
                fontWeight = FontWeight.SemiBold,
                color = MaterialTheme.colorScheme.primary
            )
        }

        Spacer(modifier = Modifier.height(24.dp))

        Button(
            onClick = {
                viewModel.provisionAndClaimBleDevice(
                    deviceName = bleDevice.name,
                    popPin = popPinInput.trim(),
                    ssid = wifiSsidInput.trim(),
                    pass = wifiPassInput.trim(),
                    targetIpOverride = null
                ) { success, msg ->
                    Toast.makeText(context, msg, Toast.LENGTH_LONG).show()
                    if (success) {
                        onProvisionSuccess()
                    }
                }
            },
            enabled = popPinInput.isNotBlank() && wifiSsidInput.isNotBlank() && viewModel.provisioningStatusText == null,
            modifier = Modifier
                .fillMaxWidth()
                .height(52.dp),
            shape = RoundedCornerShape(12.dp)
        ) {
            Text("Connect Wi-Fi & Pair Socket")
        }
    }
}

private data class BleQrInfo(
    val deviceId: String? = null,
    val popSecret: String = "",
    val transport: String? = null
)

private fun parseBleQr(rawPayload: String): BleQrInfo {
    val trimmed = rawPayload.trim()
    // Compact format: v1;PROV_A1B2C3;S3_SEC_9821;ble
    if (trimmed.startsWith("v1;")) {
        val parts = trimmed.split(";")
        if (parts.size >= 3) {
            val devId = parts[1].ifBlank { null }
            val pop = parts[2]
            val tr = if (parts.size >= 4) parts[3].ifBlank { null } else null
            return BleQrInfo(deviceId = devId, popSecret = pop, transport = tr)
        }
    }
    // Minified JSON format
    if (trimmed.startsWith("{") && trimmed.endsWith("}")) {
        try {
            val json = JSONObject(trimmed)
            val devId = json.optString("id", json.optString("name", "")).ifBlank { null }
            val pop = json.optString("pop", json.optString("POP_SECRET", ""))
            val tr = json.optString("transport", "").ifBlank { null }
            return BleQrInfo(deviceId = devId, popSecret = pop, transport = tr)
        } catch (_: Exception) {}
    }
    return BleQrInfo(popSecret = trimmed)
}
