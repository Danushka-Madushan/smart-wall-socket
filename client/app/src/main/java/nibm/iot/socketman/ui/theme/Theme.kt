package nibm.iot.socketman.ui.theme

import android.app.Activity
import android.os.Build
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.SideEffect
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.platform.LocalView
import androidx.core.view.WindowCompat

private val StrictWhiteColorScheme = lightColorScheme(
    primary = PureBlack,
    onPrimary = PureWhite,
    primaryContainer = OffWhite,
    onPrimaryContainer = PureBlack,
    
    secondary = DarkGray,
    onSecondary = PureWhite,
    secondaryContainer = LightGray,
    onSecondaryContainer = PureBlack,
    
    tertiary = AccentGreen,
    onTertiary = PureWhite,
    
    background = PureWhite,
    onBackground = PureBlack,
    
    surface = PureWhite,
    onSurface = PureBlack,
    
    surfaceVariant = LightGray,
    onSurfaceVariant = DarkGray,
    
    outline = MidGray,
    
    error = ErrorRed,
    onError = PureWhite,
    errorContainer = Color(0xFFFFDAD6),
    onErrorContainer = Color(0xFF410002)
)

@Composable
fun SocketManTheme(
    // We intentionally ignore system dark mode and dynamic colors to enforce the strict white theme
    darkTheme: Boolean = false, 
    dynamicColor: Boolean = false,
    content: @Composable () -> Unit
) {
    val colorScheme = StrictWhiteColorScheme
    
    val view = LocalView.current
    if (!view.isInEditMode) {
        SideEffect {
            val window = (view.context as Activity).window
            window.statusBarColor = colorScheme.background.toArgb()
            window.navigationBarColor = colorScheme.background.toArgb()
            WindowCompat.getInsetsController(window, view).isAppearanceLightStatusBars = true
            WindowCompat.getInsetsController(window, view).isAppearanceLightNavigationBars = true
        }
    }

    MaterialTheme(
        colorScheme = colorScheme,
        typography = Typography,
        content = content
    )
}
