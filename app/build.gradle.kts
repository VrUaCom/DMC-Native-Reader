plugins {
    id("com.android.application")
}

android {
    namespace = "com.dmcrengine.nativeviewer"
    compileSdk = 36
    ndkVersion = "28.2.13676358"

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
        // v2/v3 used an older development signer whose private key is no longer
        // available. Android correctly rejects an in-place update signed by the
        // current canonical key. v6 therefore moves to the permanent test package
        // identity below so it can install alongside the legacy test package.
        applicationId = "com.dmcrengine.nativereader"
        minSdk = 26
        targetSdk = 36
        versionCode = 6
        versionName = "0.6.0-install-identity"

        externalNativeBuild {
            cmake {
                cppFlags += listOf("-std=c++17", "-Wall", "-Wextra", "-Wpedantic")
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
            signingConfig = signingConfigs.getByName("stableDebug")
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }
}
