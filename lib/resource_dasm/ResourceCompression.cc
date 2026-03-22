#include "ResourceCompression.hh"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

#include <exception>
#include <phosg/Encoding.hh>
#include <stdexcept>
#include <string>
#include <vector>

#include "ResourceDecompressors/System.hh"
#include "SystemDecompressors.hh"

using namespace std;
using namespace phosg;

namespace ResourceDASM {
using Resource = ResourceFile::Resource;

struct DecompressorImplementation {
  typedef string (*decompress_fn)(
      const CompressedResourceHeader& header,
      const void* source,
      size_t size);
  decompress_fn decompress;

  DecompressorImplementation(decompress_fn fn)
      : decompress(fn) {}
};

static vector<DecompressorImplementation> get_candidate_decompressors(
    int16_t dcmp_id, uint64_t decompress_flags) {
  vector<DecompressorImplementation> ret;

  // Native implementations only (emulators not vendored)
  if (!(decompress_flags & DecompressionFlag::SKIP_NATIVE)) {
    if (dcmp_id == 0) {
      ret.emplace_back(decompress_system0);
    } else if (dcmp_id == 1) {
      ret.emplace_back(decompress_system1);
    } else if (dcmp_id == 2) {
      ret.emplace_back(decompress_system2);
    } else if (dcmp_id == 3) {
      ret.emplace_back(decompress_system3);
    }
  }

  return ret;
}

shared_ptr<Resource> decompress_resource(
    shared_ptr<const Resource> res,
    uint64_t decompress_flags,
    const ResourceFile* context_rf) {
  if (res->data.size() < sizeof(CompressedResourceHeader)) {
    throw runtime_error("resource marked as compressed but is too small");
  }

  auto result = make_shared<ResourceFile::Resource>();
  result->type = res->type;
  result->id = res->id;
  result->flags = res->flags;
  result->name = res->name;

  const auto& header = *reinterpret_cast<const CompressedResourceHeader*>(
      res->data.data());
  if (header.magic != 0xA89F6572) {
    result->flags &= ~ResourceFlag::FLAG_COMPRESSED;
    result->data = res->data;
    return result;
  }

  if (!(header.attributes & 0x01)) {
    throw runtime_error("resource marked as compressed but does not have compression attribute set");
  }

  int16_t dcmp_resource_id;
  if (header.header_version == 9) {
    dcmp_resource_id = header.version.v9.dcmp_resource_id;
  } else if (header.header_version == 8) {
    dcmp_resource_id = header.version.v8.dcmp_resource_id;
  } else {
    throw runtime_error("compressed resource header version is not 8 or 9");
  }

  auto decompressors = get_candidate_decompressors(dcmp_resource_id, decompress_flags);
  if (decompressors.empty()) {
    throw runtime_error("no native decompressors available for this resource (emulator-based decompression not supported)");
  }

  for (size_t z = 0; z < decompressors.size(); z++) {
    const auto& decompressor = decompressors[z];
    try {
      string decompressed_data = decompressor.decompress(
          header,
          res->data.data() + sizeof(CompressedResourceHeader),
          res->data.size() - sizeof(CompressedResourceHeader));
      if (decompressed_data.size() != header.decompressed_size) {
        throw runtime_error(std::format(
            "internal decompressor produced the wrong amount of data ({} bytes expected, {} bytes received)",
            header.decompressed_size, decompressed_data.size()));
      }
      result->data = std::move(decompressed_data);
      result->flags = (res->flags & ~ResourceFlag::FLAG_COMPRESSED) | ResourceFlag::FLAG_DECOMPRESSED;
      return result;
    } catch (const exception&) {
      // Try next decompressor
    }
  }

  throw runtime_error("no decompressor succeeded");
}

} // namespace ResourceDASM
