/*
 * HFS Browser Library - Fix-A-Fork type/creator detection
 */

#ifndef HFSBROWSE_FAF_H
#define HFSBROWSE_FAF_H

#include <cstdint>
#include <cstddef>

namespace hfsbrowse {

struct TypeCreatorResult { char type[5]; char creator[5]; };

// Detect type/creator from file magic bytes (first 1024 bytes)
bool detect_type_creator_magic(const uint8_t* data, size_t len, TypeCreatorResult* out);

// Detect type/creator from file extension (FAF table)
bool detect_type_creator_ext(const char* filename, TypeCreatorResult* out);

} // namespace hfsbrowse

#endif // HFSBROWSE_FAF_H
