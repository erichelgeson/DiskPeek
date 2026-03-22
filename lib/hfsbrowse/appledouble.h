/*
 * HFS Browser Library - AppleDouble file format read/write
 */

#ifndef HFSBROWSE_APPLEDOUBLE_H
#define HFSBROWSE_APPLEDOUBLE_H

#include <string>
#include <vector>
#include <cstdint>

namespace hfsbrowse {

// Write an AppleDouble (._filename) sidecar file containing Finder info and resource fork.
bool write_appledouble(const std::string& host_path,
                       const char* type, const char* creator,
                       int16_t fdflags,
                       const std::vector<uint8_t>& rsrc_data);

// Write resource fork + Finder info using native xattrs if supported,
// falling back to AppleDouble.
void write_forkinfo(const std::string& host_path,
                    const char* type, const char* creator,
                    int16_t fdflags,
                    const std::vector<uint8_t>& rsrc_data);

} // namespace hfsbrowse

#endif // HFSBROWSE_APPLEDOUBLE_H
