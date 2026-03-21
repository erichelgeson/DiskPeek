/*
 * Cross-platform struct packing macro.
 *
 * Usage:
 *   struct Foo {
 *       uint16_t a;
 *       uint32_t b;
 *   } PACKED;
 */

#ifndef PACKED_H
#define PACKED_H

#ifdef _MSC_VER
#  define PACKED
#  define PACKED_BEGIN __pragma(pack(push, 1))
#  define PACKED_END   __pragma(pack(pop))
#else
#  define PACKED __attribute__((__packed__))
#  define PACKED_BEGIN
#  define PACKED_END
#endif

#endif /* PACKED_H */
