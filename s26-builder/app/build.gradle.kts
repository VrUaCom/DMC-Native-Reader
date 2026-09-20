plugins {
    id("com.android.application")
}

android {
    namespace = "com.vruacom.s26scannerlab"
    compileSdk = 36

    // Public test signer. It exists only so that successive alpha builds can
    // update each other in place on a device. It is not a release authority.
    signingConfigs {
        create("stableDebug") {
            storeFile = file("../keys/s26-scanner-lab-test.jks")
            storePassword = "android"
            keyAlias = "s26-scanner-lab-test"
            keyPassword = "android"
            enableV1Signing = true
            enableV2Signing = true
            enableV3Signing = true
        }
    }

    defaultConfig {
        applicationId = "com.vruacom.s26scannerlab"
        minSdk = 31
        targetSdk = 36
        versionCode = 7
        versionName = "0.1.0-alpha07"
    }

    buildTypes {
        debug {
            signingConfig = signingConfigs.getByName("stableDebug")
        }
        release {
            isMinifyEnabled = false
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}
