import java.util.Properties

plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.compose)
}

val keystorePropertiesFile = rootProject.file("keystore.properties")
val keystoreProperties = Properties()
if (keystorePropertiesFile.exists()) {
    keystorePropertiesFile.inputStream().use { keystoreProperties.load(it) }
}

fun resolveKeystoreFile(rawPath: String?): File? {
    if (rawPath.isNullOrBlank()) return null
    val f = file(rawPath)
    if (f.exists()) return f
    val rootF = rootProject.file(rawPath)
    if (rootF.exists()) return rootF
    return null
}

val releaseStorePath: String? = System.getenv("KEYSTORE_FILE") ?: keystoreProperties.getProperty("storeFile")
val releaseStorePass: String? = System.getenv("KEYSTORE_PASSWORD") ?: keystoreProperties.getProperty("storePassword")
val releaseKeyAlias: String? = System.getenv("KEY_ALIAS") ?: keystoreProperties.getProperty("keyAlias")
val releaseKeyPass: String? = System.getenv("KEY_PASSWORD") ?: keystoreProperties.getProperty("keyPassword")

val releaseStoreFile = resolveKeystoreFile(releaseStorePath)
val hasReleaseSigning = releaseStoreFile != null &&
        !releaseStorePass.isNullOrBlank() &&
        !releaseKeyAlias.isNullOrBlank() &&
        !releaseKeyPass.isNullOrBlank()

android {
    signingConfigs {
        if (hasReleaseSigning) {
            create("release") {
                storeFile = releaseStoreFile
                storePassword = releaseStorePass
                keyAlias = releaseKeyAlias
                keyPassword = releaseKeyPass
                enableV1Signing = true
                enableV2Signing = true
            }
        }
    }
    namespace = "nibm.iot.socketman"
    compileSdk {
        version = release(37)
    }

    defaultConfig {
        applicationId = "nibm.iot.socketman"
        minSdk = 29
        targetSdk = 37
        versionCode = 1
        versionName = "1.0"

        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
    }

    buildTypes {
        release {
            optimization {
                enable = false
            }
            if (hasReleaseSigning) {
                signingConfig = signingConfigs.getByName("release")
            } else {
                signingConfig = signingConfigs.getByName("debug")
            }
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_11
        targetCompatibility = JavaVersion.VERSION_11
    }
    buildFeatures {
        compose = true
    }
}

dependencies {
    implementation(platform(libs.androidx.compose.bom))
    implementation(libs.androidx.activity.compose)
    implementation(libs.androidx.compose.material3)
    implementation(libs.androidx.compose.ui)
    implementation(libs.androidx.compose.ui.graphics)
    implementation(libs.androidx.compose.ui.tooling.preview)
    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.lifecycle.runtime.ktx)
    
    // CameraX & MLKit Barcode Scanning for PoP QR Claiming
    implementation(libs.androidx.camera.camera2)
    implementation(libs.androidx.camera.lifecycle)
    implementation(libs.androidx.camera.view)
    implementation(libs.mlkit.barcode.scanning)

    testImplementation(libs.junit)
    androidTestImplementation(platform(libs.androidx.compose.bom))
    androidTestImplementation(libs.androidx.compose.ui.test.junit4)
    androidTestImplementation(libs.androidx.espresso.core)
    androidTestImplementation(libs.androidx.junit)
    debugImplementation(libs.androidx.compose.ui.test.manifest)
    debugImplementation(libs.androidx.compose.ui.tooling)
}