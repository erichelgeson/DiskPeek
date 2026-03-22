/*
 * HFS Browser Library - MacRoman/UTF-8 encoding conversion and filename sanitization
 */

#ifndef HFSBROWSE_ENCODING_H
#define HFSBROWSE_ENCODING_H

#include <string>

namespace hfsbrowse {

std::string macroman_to_utf8(const std::string& macroman);
std::string utf8_to_macroman(const std::string& utf8);

// Sanitize a filename for the target filesystem.
// Truncates to max length, replaces ':' with '-'.
std::string sanitize_hfs_name(const std::string& name);    // max 31 chars, UTF-8→MacRoman
std::string sanitize_hfsplus_name(const std::string& name); // max 255 chars, colons replaced

} // namespace hfsbrowse

#endif // HFSBROWSE_ENCODING_H
