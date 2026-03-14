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
            name: "Czip",
            path: "external/zip/src",
            sources: ["zip.c"],
            publicHeadersPath: ".",
            cSettings: [
                .define("ZIP_SHARED"),
                .define("ZIP_BUILD_SHARED"),
                .define("MINIZ_EXPORT", to: "__attribute__((visibility(\"hidden\")))"),
                .define("MINIZ_DISABLE_ZIP_READER_CRC32_CHECKS"),
                .define("MINIZ_NO_TIME")
            ]
        ),
        .target(
            name: "CHoshiDicts",
            dependencies: [
                .product(name: "libzstd", package: "zstd"),
                "Czip"
            ],
            path: ".",
            sources: ["src"],
            publicHeadersPath: "include",
            cxxSettings: [
                .headerSearchPath("include"),
                .headerSearchPath("external/zip/src"),
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
