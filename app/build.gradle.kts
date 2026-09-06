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

    defaultConfig {
        applicationId = "com.dmcrengine.nativereader"
        minSdk = 26
        targetSdk = 36
        versionCode = 15
        versionName = "1.1.3-debug-v1-hardening"

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
            // Use the Android debug signer only for development/device tests.
        }
        release {
            isMinifyEnabled = false
            // Intentionally unsigned here. Production v1.0 signing belongs to
            // a dedicated release pipeline with an external protected key.
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }
}
