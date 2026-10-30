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
        versionCode = 142
        versionName = "1.0.115"

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

dependencies {
    // ARCore, optional: the camera mode's walking (6DoF). The app runs
    // without it; walking asks for Google Play Services for AR when turned on.
    implementation("com.google.ar:core:1.48.0")
}

// Every activity request code is its own: two equal codes route one picker's
// result to the other's handler (v72-v134 wrote the diagnostics log over a
// file picked for stage effects). The build stops on a repeated code.
val checkRequestCodes by tasks.registering {
    val source = file("src/main/java/com/dmcrengine/nativeviewer/MainActivity.java")
    inputs.file(source)
    doLast {
        val pattern = Regex("""static final int (REQUEST_\w+) = (\d+);""")
        val repeated = pattern.findAll(source.readText())
            .groupBy({ it.groupValues[2] }, { it.groupValues[1] })
            .filterValues { it.size > 1 }
        if (repeated.isNotEmpty()) throw GradleException("Repeated request codes: $repeated")
    }
}
tasks.named("preBuild") { dependsOn(checkRequestCodes) }
