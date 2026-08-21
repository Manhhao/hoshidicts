// swift-tools-version: 6.2
import PackageDescription

let package = Package(
    name: "hoshidicts",
    platforms: [.iOS(.v18), .macOS(.v15)],
    products: [
        .library(name: "CHoshiDicts", targets: ["CHoshiDicts"]),
        .executable(name: "hoshidicts", targets: ["hoshidicts"]),
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
                "external/utf8proc/utf8proc.c",
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
                .headerSearchPath("external/utf8proc"),
                .unsafeFlags(["-Wno-missing-braces"]),
            ],
            swiftSettings: [
                .interoperabilityMode(.Cxx)
            ]
        ),
        .executableTarget(
            name: "hoshidicts",
            dependencies: ["CHoshiDicts"],
            path: ".",
            sources: ["cli/main.cpp"],
            cxxSettings: [
                .headerSearchPath("include"),
                .headerSearchPath("external/utfcpp/source"),
            ]
        ),
    ],
    cxxLanguageStandard: .cxx2b
)
