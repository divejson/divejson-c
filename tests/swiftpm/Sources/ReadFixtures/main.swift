// read-fixtures VENDOR PRODUCT MODEL INPUT OUTPUT: one dive through libdivejson, linked
// against libdc-swift's libdivecomputer, written as divejson-raw writes it.
import Foundation
import LibDiveJSON

let arguments = CommandLine.arguments
guard arguments.count == 6, let model = UInt32(arguments[3]) else {
  FileHandle.standardError.write(Data("usage: ReadFixtures VENDOR PRODUCT MODEL INPUT OUTPUT\n".utf8))
  exit(2)
}
let bytes = try Data(contentsOf: URL(fileURLWithPath: arguments[4]))
var error = [CChar](repeating: 0, count: 512)

let dive: OpaquePointer? = bytes.withUnsafeBytes { buffer in
  arguments[1].withCString { vendor in
    arguments[2].withCString { product in
      var input = divejson_libdivecomputer_input(
        data: buffer.bindMemory(to: UInt8.self).baseAddress, size: buffer.count,
        vendor: vendor, product: product, model: model, fallback_start: nil, uuid: nil)
      return divejson_libdivecomputer_read(&input, &error, error.count)
    }
  }
}
guard let dive else {
  FileHandle.standardError.write(Data((String(cString: error) + "\n").utf8))
  exit(1)
}
defer { divejson_dive_free(dive) }

let document: UnsafeMutablePointer<CChar>? = "read-fixtures".withCString { name in
  "2026-11-03T00:00:00+00:00".withCString { exportedAt in
    var options = divejson_document_options(
      generator_name: name, generator_version: nil, exported_at: exportedAt)
    return divejson_document(dive, &options, &error, error.count)
  }
}
guard let document else {
  FileHandle.standardError.write(Data((String(cString: error) + "\n").utf8))
  exit(1)
}
defer { divejson_free(document) }
try String(cString: document).write(
  to: URL(fileURLWithPath: arguments[5]), atomically: true, encoding: .utf8)
