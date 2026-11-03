// swift-tools-version:5.10
//
// Proves the link an Apple host makes: libdivejson from the root manifest, its dc_* symbols
// from libdc-swift's libdivecomputer, in one executable that reads the fixtures. CI runs it
// from a checkout named divejson-c, the identity the path dependency below takes.
import PackageDescription

let package = Package(
  name: "libdc-swift-link",
  platforms: [.macOS(.v13)],
  dependencies: [
    .package(path: "../.."),
    // Moves with LIBDIVECOMPUTER_COMMIT: CI checks this release's submodule is that commit.
    .package(url: "https://github.com/deepsealabs/libdc-swift.git", exact: "1.20.0"),
  ],
  targets: [
    .executableTarget(
      name: "ReadFixtures",
      dependencies: [
        .product(name: "LibDiveJSON", package: "divejson-c"),
        .product(name: "LibDCSwift", package: "libdc-swift"),
      ],
      path: "Sources/ReadFixtures"
    )
  ]
)
