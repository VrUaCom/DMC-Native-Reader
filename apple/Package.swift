// swift-tools-version:5.9
// DMC Native Reader for macOS: a SwiftUI shell over the reader's C API
// (app/src/main/cpp/capi/include/dmc_reader.h). The C++ core is built first
// by build-mac.sh into .build/core-libs; this package links those archives.
import Foundation
import PackageDescription

let here = URL(fileURLWithPath: #filePath).deletingLastPathComponent().path
let coreLibs = "\(here)/.build/core-libs"

let package = Package(
    name: "DMCNativeReader",
    platforms: [.macOS(.v13)],
    targets: [
        // The C API as a Swift module; its module map names the archives.
        .systemLibrary(name: "CDMCReader", path: "Sources/CDMCReader"),
        .executableTarget(
            name: "DMCNativeReader",
            dependencies: ["CDMCReader"],
            path: "Sources/DMCNativeReader",
            linkerSettings: [
                .unsafeFlags(["-L\(coreLibs)"]),
                .linkedLibrary("c++"),
            ]
        ),
    ]
)
