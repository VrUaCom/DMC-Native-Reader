plugins {
    id("com.android.application")
}

android {
    namespace = "com.dmcrengine.nativeviewer"
    compileSdk = 36
    ndkVersion = "28.2.13676358"

    buildFeatures {
        buildConfig = true
    }

    // Public repository test signer used only to keep debug/device-test APKs
    // upgrade-compatible across CI runs. It is not a production authority.
    signingConfigs {
        create("stableDebug") {
            storeFile = file("../keys/dmc-native-reader-test.jks")
            storePassword = "android"
            keyAlias = "dmc-native-reader-test"
            keyPassword = "android"
            enableV1Signing = true
            enableV2Signing = true
            enableV3Signing = true
        }
    }

    defaultConfig {
        applicationId = "com.dmcrengine.nativereader"
        minSdk = 26
        targetSdk = 36
        versionCode = 16
        versionName = "1.1.4-debug-dds-ptx"

        externalNativeBuild {
            cmake {
                cppFlags += listOf("-std=c++20", "-Wall", "-Wextra", "-Wpedantic")
            }
        }
        ndk {
            abiFilters += listOf("arm64-v8a")
        }
    }

    buildTypes {
        debug {
            isJniDebuggable = true
            signingConfig = signingConfigs.getByName("stableDebug")
        }
        release {
            isMinifyEnabled = false
            // Intentionally no signingConfig here. Production v1.0 signing
            // belongs to a dedicated release pipeline with an external key.
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }
}
