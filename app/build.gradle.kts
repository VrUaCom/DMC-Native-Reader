plugins {
    id("com.android.application")
}

android {
    namespace = "com.dmcrengine.nativeviewer"
    compileSdk = 36
    ndkVersion = "30.0.16248370"

    buildFeatures {
        buildConfig = true
    }

    // Canonical modular APK: keep the single mmap-ready JNI DSO inside the APK.
    // DMCNativeReader::Core and DMCRengine::ReaderCore are static link-time
    // dependencies of libdmcviewer.so and must never be packaged as duplicate
    // runtime libraries.
    packaging {
        jniLibs {
            useLegacyPackaging = false
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
        versionCode = 52
        versionName = "1.0.25"

        // Do not set -std= or other semantic C++ flags here. Gradle's CMake
        // cppFlags are global to the external native build. Language mode is
        // owned by CMake targets: every target Native Reader compiles, the
        // vendored Rengine ReaderCore included, is set to C++23 there.
        ndk {
            abiFilters += listOf("arm64-v8a")
        }
    }

    buildTypes {
        debug {
            // Device-test APKs use source/host regressions for diagnostics; the
            // installed JNI image itself should remain stripped and compact.
            isJniDebuggable = false
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
