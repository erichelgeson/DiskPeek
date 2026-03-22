/*
 * HFS Browser Library - Icon extraction from resource forks
 * Returns raw RGBA pixel data (no OpenGL dependency).
 */

#ifndef HFSBROWSE_ICON_H
#define HFSBROWSE_ICON_H

#include <vector>
#include <cstdint>
#include <string>

namespace hfsbrowse {
namespace icon {

// Extract a 64x64 RGBA icon from a resource fork blob.
// Tries icl8 (256-color), then icl4 (16-color), then ICN# (1-bit).
// Returns empty vector if no icon found.
std::vector<uint8_t> extract_rgba(const std::vector<uint8_t>& rsrc_fork);

// Write a 64x64 RGBA icon as a PNG file.
// Returns true on success.
bool write_png(const std::string& path, const std::vector<uint8_t>& rgba_64x64);

} // namespace icon
} // namespace hfsbrowse

#endif // HFSBROWSE_ICON_H
