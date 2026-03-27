// swift-tools-version: 6.2
import PackageDescription

let package = Package(
    name: "hoshidicts",
    platforms: [.iOS(.v18), .macOS(.v15)],
    products: [
        .library(name: "CHoshiDicts", targets: ["CHoshiDicts"]),
    ],
    dependencies: [
        .package(url: "https://github.com/facebook/zstd.git", from: "1.5.7"),
    ],
    targets: [
        .target(
            name: "CHoshiDicts",
            dependencies: [
                .product(name: "libzstd", package: "zstd"),
            ],
            path: ".",
            sources: [
                "src",
                "external/libdeflate/lib",
            ],
            publicHeadersPath: "include",
            cxxSettings: [
                .headerSearchPath("include"),
                .headerSearchPath("external/libdeflate"),
                .headerSearchPath("external/libdeflate/lib"),
                .headerSearchPath("external/utfcpp/source"),
                .headerSearchPath("external/glaze/include"),
                .headerSearchPath("external/xxHash"),
                .headerSearchPath("external/unordered_dense/include"),
                .unsafeFlags(["-Wno-missing-braces"]),
            ],
            swiftSettings: [
                .interoperabilityMode(.Cxx)
            ]
        ),
    ],
    cxxLanguageStandard: .cxx2b
)
