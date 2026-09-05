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
        versionCode = 9
        versionName = "0.9.0-explicit-family-modules"

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
