package nibm.iot.socketman

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Scaffold
import androidx.compose.ui.Modifier
import androidx.lifecycle.ViewModelProvider
import nibm.iot.socketman.ui.screens.MainScreen
import nibm.iot.socketman.ui.theme.SocketManTheme
import nibm.iot.socketman.viewmodel.SocketViewModel

class MainActivity : ComponentActivity() {
    private val viewModel: SocketViewModel by lazy {
        ViewModelProvider(this)[SocketViewModel::class.java]
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        viewModel.initCheck()
        
        setContent {
            SocketManTheme {
                Scaffold(modifier = Modifier.fillMaxSize()) { innerPadding ->
                    MainScreen(
                        viewModel = viewModel,
                        modifier = Modifier.padding(innerPadding)
                    )
                }
            }
        }
    }
}
