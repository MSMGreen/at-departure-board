import java.util.Properties

plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.compose)
}

// android/local.properties is gitignored. It holds the AT key - as
// src/secrets.h does for the board - and where the signing keystore lives.
// Both phones need builds signed with the same key, or an update will not
// install over the last one (widget spec 12).
val local = Properties().apply {
    rootProject.file("local.properties").takeIf { it.exists() }?.inputStream()?.use { load(it) }
}

android {
    namespace = "io.github.msmgreen.nextdeparture"
    compileSdk {
        version = release(37)
    }

    defaultConfig {
        applicationId = "io.github.msmgreen.nextdeparture"
        minSdk = 34
        targetSdk = 36
        versionCode = 1
        versionName = "1.0"
        // Empty when local.properties has no key; the setup check says so.
        buildConfigField("String", "AT_API_KEY", "\"${local.getProperty("AT_API_KEY", "")}\"")
    }

    signingConfigs {
        local.getProperty("SIGNING_STORE_FILE")?.let { path ->
            create("phones") {
                storeFile = file(path)
                storePassword = local.getProperty("SIGNING_STORE_PASSWORD")
                keyAlias = local.getProperty("SIGNING_KEY_ALIAS")
                keyPassword = local.getProperty("SIGNING_KEY_PASSWORD")
            }
        }
    }

    buildTypes {
        // Debug builds are what go on the phones: debuggable, logging, and
        // signed with the shared key whenever local.properties names one.
        // CI and a fresh clone fall back to the machine's debug key.
        debug {
            signingConfigs.findByName("phones")?.let { signingConfig = it }
        }
        release {
            optimization {
                enable = true
                packageScope = setOf("androidx.**", "kotlin.**", "kotlinx.**")
            }
        }
    }

    // core is compiled for Java 17; match it.
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    buildFeatures {
        buildConfig = true
        compose = true
    }
}

dependencies {
    implementation(project(":core"))
    implementation(libs.okhttp)
    implementation(libs.kotlinx.coroutines.android)
    implementation(libs.androidx.work.runtime.ktx)
    implementation(libs.androidx.datastore.preferences)
    implementation(platform(libs.androidx.compose.bom))
    implementation(libs.androidx.compose.ui)
    implementation(libs.androidx.compose.foundation)
    implementation(libs.androidx.compose.material3)
    implementation(libs.androidx.activity.compose)
}
