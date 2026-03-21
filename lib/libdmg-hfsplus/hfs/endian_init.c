/*
 * Provides the 'endianness' global required by libdmg-hfsplus common.h.
 * The original definition is in hfs.c which also contains main(), so we
 * provide it separately here.
 */
#include "common.h"

char endianness;
