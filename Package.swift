// swift-tools-version:5.10
//
// libdivejson for Apple hosts, with the libdivecomputer reader built in. The reader compiles
// against the headers of the libdivecomputer this repository vendors, and compiles none of
// its sources: the dc_* symbols come from whichever libdivecomputer the app already links for
// its downloads, which must be the commit LIBDIVECOMPUTER_COMMIT names.
import PackageDescription

let package = Package(
  name: "divejson-c",
  products: [
    .library(name: "LibDiveJSON", targets: ["LibDiveJSON"])
  ],
  targets: [
    .target(
      name: "LibDiveJSON",
      path: ".",
      sources: ["src"],
      publicHeadersPath: "include",
      cSettings: [
        .headerSearchPath("vendor/libdivecomputer/include")
      ]
    )
  ],
  cLanguageStandard: .c11
)
