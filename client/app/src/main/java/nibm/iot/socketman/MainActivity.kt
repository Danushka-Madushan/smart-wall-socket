package nibm.iot.socketman

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Scaffold
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import nibm.iot.socketman.data.MdnsDevice
import nibm.iot.socketman.ui.screens.*
import nibm.iot.socketman.ui.theme.SocketManTheme
import nibm.iot.socketman.viewmodel.MdnsSwitchViewModel

sealed class Screen {
    object Home : Screen()
    object Discovery : Screen()
    data class PoPClaim(val device: MdnsDevice) : Screen()
    object DeviceControl : Screen()
}

class MainActivity : ComponentActivity() {
    private lateinit var mdnsViewModel: MdnsSwitchViewModel

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()

        mdnsViewModel = MdnsSwitchViewModel(this)

        setContent {
            SocketManTheme {
                var currentScreen by remember { mutableStateOf<Screen>(Screen.Home) }

                Scaffold(modifier = Modifier.fillMaxSize()) { innerPadding ->
                    when (val screen = currentScreen) {
                        is Screen.Home -> {
                            MdnsHomeScreen(
                                viewModel = mdnsViewModel,
                                onNavigateToDiscovery = { currentScreen = Screen.Discovery },
                                onNavigateToControl = { currentScreen = Screen.DeviceControl },
                                modifier = Modifier.padding(innerPadding)
                            )
                        }

                        is Screen.Discovery -> {
                            MdnsDiscoveryScreen(
                                viewModel = mdnsViewModel,
                                onNavigateBack = { currentScreen = Screen.Home },
                                onNavigateToClaim = { mdnsDevice ->
                                    currentScreen = Screen.PoPClaim(mdnsDevice)
                                },
                                modifier = Modifier.padding(innerPadding)
                            )
                        }

                        is Screen.PoPClaim -> {
                            PoPClaimScreen(
                                targetDevice = screen.device,
                                viewModel = mdnsViewModel,
                                onNavigateBack = { currentScreen = Screen.Discovery },
                                onClaimSuccess = { currentScreen = Screen.DeviceControl },
                                modifier = Modifier.padding(innerPadding)
                            )
                        }

                        is Screen.DeviceControl -> {
                            DeviceControlScreen(
                                viewModel = mdnsViewModel,
                                onNavigateBack = { currentScreen = Screen.Home },
                                modifier = Modifier.padding(innerPadding)
                            )
                        }
                    }
                }
            }
        }
    }
}
