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

    // Native Reader 1.0 recovery policy: package JNI libraries in the
    // install-compatible legacy mode so Package Manager extracts them instead
    // of requiring mmap-ready ZIP alignment from a manually recovered shell.
    // This is a packaging decision only; native C++ module ownership and ABI
    // remain unchanged.
    packaging {
        jniLibs {
            useLegacyPackaging = true
        }
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
        versionCode = 23
        versionName = "1.0"

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
            // Intentionally no signingConfig here. Production signing authority
            // is injected only by the external release-signing workflow.
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }
}
