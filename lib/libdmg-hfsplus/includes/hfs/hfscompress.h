#ifndef HFSCOMPRESS_H
#define HFSCOMPRESS_H

#include <stdint.h>
#include "common.h"

#ifdef _MSC_VER
#  pragma pack(push, 1)
#  define ATTRIBUTE_PACKED_CMP
#else
#  define ATTRIBUTE_PACKED_CMP __attribute__ ((packed))
#endif

#define CMPFS_MAGIC 0x636D7066

typedef struct HFSPlusDecmpfs {
	uint32_t magic;
	uint32_t flags;
	uint64_t size;
	uint8_t data[0];
} ATTRIBUTE_PACKED_CMP HFSPlusDecmpfs;

typedef struct HFSPlusCmpfRsrcHead {
	uint32_t headerSize;
	uint32_t totalSize;
	uint32_t dataSize;
	uint32_t flags;
} ATTRIBUTE_PACKED_CMP HFSPlusCmpfRsrcHead;

typedef struct HFSPlusCmpfRsrcBlock {
	uint32_t offset;
	uint32_t size;
} ATTRIBUTE_PACKED_CMP HFSPlusCmpfRsrcBlock;

typedef struct HFSPlusCmpfRsrcBlockHead {
	uint32_t dataSize;
	uint32_t numBlocks;
	HFSPlusCmpfRsrcBlock blocks[0];
} ATTRIBUTE_PACKED_CMP HFSPlusCmpfRsrcBlockHead;

typedef struct HFSPlusCmpfEnd {
	uint32_t pad[6];
	uint16_t unk1;
	uint16_t unk2;
	uint16_t unk3;
	uint32_t magic;
	uint32_t flags;
	uint64_t size;
	uint32_t unk4;
} ATTRIBUTE_PACKED_CMP HFSPlusCmpfEnd;

typedef struct HFSPlusCompressed {
	Volume* volume;
	HFSPlusCatalogFile* file;
	io_func* io;
	size_t decmpfsSize;
	HFSPlusDecmpfs* decmpfs;

	HFSPlusCmpfRsrcHead rsrcHead;
	HFSPlusCmpfRsrcBlockHead* blocks;

	int dirty;

	uint8_t* cached;
	uint32_t cachedStart;
	uint32_t cachedEnd;
} HFSPlusCompressed;

#ifdef __cplusplus
extern "C" {
#endif
	void flipHFSPlusDecmpfs(HFSPlusDecmpfs* compressData);
	io_func* openHFSPlusCompressed(Volume* volume, HFSPlusCatalogFile* file);
#ifdef __cplusplus
}
#endif

#ifdef _MSC_VER
#  pragma pack(pop)
#endif
#undef ATTRIBUTE_PACKED_CMP

#endif
