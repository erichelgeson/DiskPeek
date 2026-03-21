/*
 * HFS+ operations wrapper around libdmg-hfsplus
 */

#include "hfsplus_ops.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <setjmp.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wregister"
extern "C" {
#include "common.h"
#include "hfs/hfsplus.h"
#include "hfs/hfslib.h"
#include "abstractfile.h"
}
#pragma GCC diagnostic pop

// --- Endianness init (normally in hfs.c main, which we don't link) ---

extern "C" char endianness;

static void init_endianness() {
    short int word = 0x0001;
    char *byte = (char *)&word;
    endianness = byte[0] ? IS_LITTLE_ENDIAN : IS_BIG_ENDIAN;
}

// --- hfs_panic override ---
// The default hfs_panic() in utility.c calls exit(1). We override it via
// a longjmp so the GUI doesn't crash. We use thread-local storage for safety.

static thread_local jmp_buf s_panic_jmp;
static thread_local bool s_panic_armed = false;
static thread_local char s_panic_msg[256];

extern "C" void hfs_panic(const char* panicString) {
    fprintf(stderr, "hfsplus: panic: %s\n", panicString);
    if (s_panic_armed) {
        snprintf(s_panic_msg, sizeof(s_panic_msg), "%s", panicString);
        longjmp(s_panic_jmp, 1);
    }
    // If not armed (shouldn't happen), just log and return
}

// Helper to run a libdmg-hfsplus operation with panic protection.
// Returns true if the operation completed without panic.
#define PANIC_PROTECT_BEGIN() \
    s_panic_msg[0] = '\0'; \
    s_panic_armed = true; \
    if (setjmp(s_panic_jmp) != 0) { \
        s_panic_armed = false;

#define PANIC_PROTECT_END() \
    }

// --- Offset io_func wrapper ---
// Wraps another io_func adding a byte offset to all reads/writes.

struct OffsetIOData {
    io_func* inner;
    off_t offset;
};

static int offsetRead(io_func* io, off_t location, size_t size, void* buffer) {
    OffsetIOData* d = (OffsetIOData*)io->data;
    return d->inner->read(d->inner, d->offset + location, size, buffer);
}

static int offsetWrite(io_func* io, off_t location, size_t size, void* buffer) {
    OffsetIOData* d = (OffsetIOData*)io->data;
    return d->inner->write(d->inner, d->offset + location, size, buffer);
}

static void offsetClose(io_func* io) {
    OffsetIOData* d = (OffsetIOData*)io->data;
    d->inner->close(d->inner);
    free(d);
    free(io);
}

static io_func* openOffsetIO(io_func* inner, off_t offset) {
    if (offset == 0) return inner;

    OffsetIOData* d = (OffsetIOData*)malloc(sizeof(OffsetIOData));
    d->inner = inner;
    d->offset = offset;

    io_func* io = (io_func*)malloc(sizeof(io_func));
    io->data = d;
    io->read = offsetRead;
    io->write = offsetWrite;
    io->close = offsetClose;
    return io;
}

// --- HFSPlusVolume struct ---

struct HFSPlusVolume {
    io_func* io;        // underlying file io (may be offset wrapper)
    Volume* volume;     // libdmg-hfsplus Volume
    char vol_name[256]; // cached volume name (from catalog root)
    bool readonly;
};

// --- Path conversion ---
// Our UI uses Mac-style "Volume:folder:file" paths.
// libdmg-hfsplus uses Unix-style "/folder/file" paths.

// Convert Mac-style path to Unix-style for libdmg-hfsplus.
// Input: "VolumeName:folder:file" -> "/folder/file"
// Input: "VolumeName:" -> "/"
static std::string mac_to_unix_path(const char* mac_path, const char* vol_name) {
    // Skip volume name prefix
    const char* p = mac_path;
    size_t vlen = strlen(vol_name);
    if (strncmp(p, vol_name, vlen) == 0)
        p += vlen;
    if (*p == ':')
        p++;

    if (*p == '\0')
        return "/";

    std::string result = "/";
    while (*p) {
        if (*p == ':')
            result += '/';
        else
            result += *p;
        p++;
    }
    return result;
}

// --- Get volume name from catalog root folder ---
//
// The volume name in HFS+ is stored in the catalog B-tree as the nodeName
// in the thread record for the root folder (CNID 2). To find it:
//   1. Search catalog for key (parentID=2, nodeName.length=0) → thread record
//   2. The thread record's nodeName IS the volume name
//
// We can't use getRecordByCNID() because it does TWO searches — it finds the
// thread record, extracts the key, then searches again for the actual folder
// record and returns THAT, discarding the thread with the name.

static void get_volume_name(Volume* volume, char* out, size_t out_size) {
    out[0] = '\0';

    // Search catalog B-tree directly for the root folder's thread record
    HFSPlusCatalogKey key;
    key.keyLength = sizeof(key.parentID) + sizeof(key.nodeName.length);
    key.parentID = kHFSRootFolderID;  // CNID 2
    key.nodeName.length = 0;

    int exact = 0;
    HFSPlusCatalogThread* thread = (HFSPlusCatalogThread*)
        search(volume->catalogTree, (BTKey*)(&key), &exact, NULL, NULL);

    if (thread && exact) {
        // thread->nodeName contains the volume name in Unicode
        char* name = unicodeToAscii(&thread->nodeName);
        if (name && name[0] != '\0') {
            snprintf(out, out_size, "%s", name);
            free(name);
        }
        free(thread);
    } else {
        free(thread);
    }

    if (out[0] == '\0')
        snprintf(out, out_size, "HFS+ Volume");
}

// --- Public API ---

HFSPlusVolume* hfsplus_open(const char* path, uint64_t partition_offset, bool readonly) {
    static bool endian_inited = false;
    if (!endian_inited) {
        init_endianness();
        endian_inited = true;
    }

    hfs_setsilence(1);

    io_func* raw_io;
    if (readonly)
        raw_io = openFlatFileRO(path);
    else
        raw_io = openFlatFile(path);

    if (!raw_io) return nullptr;

    io_func* volatile io = openOffsetIO(raw_io, (off_t)partition_offset);

    PANIC_PROTECT_BEGIN()
        // Panic during openVolume — clean up and return null
        if (io != raw_io) {
            // offsetClose will close inner
            io->close(io);
        } else {
            raw_io->close(raw_io);
        }
        return nullptr;
    PANIC_PROTECT_END()

    Volume* vol = openVolume(io);
    s_panic_armed = false;

    if (!vol) {
        if (io != raw_io) {
            io->close(io);
        } else {
            raw_io->close(raw_io);
        }
        return nullptr;
    }

    // Verify this is actually an HFS+ volume (signature 'H+' = 0x482B or 'HX' = 0x4858)
    uint16_t sig = vol->volumeHeader->signature;
    if (sig != 0x482B && sig != 0x4858) {
        closeVolume(vol);
        // Don't close io — closeVolume doesn't close it, but we need to
        // Actually closeVolume doesn't close the io_func, so we must
        io->close(io);
        return nullptr;
    }

    HFSPlusVolume* volatile hv = new HFSPlusVolume();
    hv->io = io;
    hv->volume = vol;
    hv->readonly = readonly;

    PANIC_PROTECT_BEGIN()
        // Panic getting volume name — use default
        snprintf(hv->vol_name, sizeof(hv->vol_name), "HFS+ Volume");
        return hv;
    PANIC_PROTECT_END()

    get_volume_name(vol, hv->vol_name, sizeof(hv->vol_name));
    s_panic_armed = false;

    return hv;
}

void hfsplus_close(HFSPlusVolume* vol) {
    if (!vol) return;

    if (vol->volume) {
        if (!vol->readonly) {
            PANIC_PROTECT_BEGIN()
                // Ignore panic during volume update
                goto skip_update;
            PANIC_PROTECT_END()
            updateVolume(vol->volume);
            s_panic_armed = false;
        }
skip_update:
        closeVolume(vol->volume);
    }
    if (vol->io) {
        CLOSE(vol->io);
    }
    delete vol;
}

const char* hfsplus_volume_name(HFSPlusVolume* vol) {
    return vol ? vol->vol_name : "";
}

uint64_t hfsplus_total_bytes(HFSPlusVolume* vol) {
    if (!vol || !vol->volume) return 0;
    HFSPlusVolumeHeader* vh = vol->volume->volumeHeader;
    return (uint64_t)vh->totalBlocks * (uint64_t)vh->blockSize;
}

uint64_t hfsplus_free_bytes(HFSPlusVolume* vol) {
    if (!vol || !vol->volume) return 0;
    HFSPlusVolumeHeader* vh = vol->volume->volumeHeader;
    return (uint64_t)vh->freeBlocks * (uint64_t)vh->blockSize;
}

// Internal: list folder contents given a folder CNID
static int list_dir_impl(HFSPlusVolume* vol, HFSCatalogNodeID folderID,
                          HFSPlusDirEntry** out_entries, int* out_count) {
    *out_entries = nullptr;
    *out_count = 0;

    PANIC_PROTECT_BEGIN()
        return -1;
    PANIC_PROTECT_END()

    CatalogRecordList* list = getFolderContents(folderID, vol->volume);
    s_panic_armed = false;

    if (!list) return 0;

    // Count entries
    int count = 0;
    CatalogRecordList* cur = list;
    while (cur) {
        char* name = unicodeToAscii(&cur->name);
        if (name) {
            if (name[0] != '\0' &&
                strncmp(name, ".HFS+ Private Directory Data", 28) != 0) {
                count++;
            }
            free(name);
        }
        cur = cur->next;
    }

    if (count == 0) {
        releaseCatalogRecordList(list);
        return 0;
    }

    HFSPlusDirEntry* entries = (HFSPlusDirEntry*)calloc(count, sizeof(HFSPlusDirEntry));
    int idx = 0;

    cur = list;
    while (cur && idx < count) {
        char* name = unicodeToAscii(&cur->name);
        if (!name) { cur = cur->next; continue; }
        if (name[0] == '\0' ||
            strncmp(name, ".HFS+ Private Directory Data", 28) == 0) {
            free(name);
            cur = cur->next;
            continue;
        }

        HFSPlusDirEntry* e = &entries[idx];
        snprintf(e->name, sizeof(e->name), "%s", name);
        e->parent_cnid = folderID;

        free(name);

        if (cur->record->recordType == kHFSPlusFolderRecord) {
            HFSPlusCatalogFolder* f = (HFSPlusCatalogFolder*)cur->record;
            e->is_dir = true;
            e->cnid = f->folderID;
            e->data_size = 0;
            e->rsrc_size = 0;
            e->finder_flags = f->userInfo.finderFlags;
            memset(e->type, 0, 5);
            memset(e->creator, 0, 5);
        } else if (cur->record->recordType == kHFSPlusFileRecord) {
            HFSPlusCatalogFile* f = (HFSPlusCatalogFile*)cur->record;
            e->is_dir = false;
            e->cnid = f->fileID;
            e->data_size = f->dataFork.logicalSize;
            e->rsrc_size = f->resourceFork.logicalSize;
            e->finder_flags = f->userInfo.finderFlags;

            uint32_t ft = f->userInfo.fileType;
            uint32_t fc = f->userInfo.fileCreator;
            e->type[0] = (ft >> 24) & 0xFF;
            e->type[1] = (ft >> 16) & 0xFF;
            e->type[2] = (ft >> 8) & 0xFF;
            e->type[3] = ft & 0xFF;
            e->type[4] = '\0';
            e->creator[0] = (fc >> 24) & 0xFF;
            e->creator[1] = (fc >> 16) & 0xFF;
            e->creator[2] = (fc >> 8) & 0xFF;
            e->creator[3] = fc & 0xFF;
            e->creator[4] = '\0';
        }

        idx++;
        cur = cur->next;
    }

    releaseCatalogRecordList(list);

    *out_entries = entries;
    *out_count = idx;
    return 0;
}

int hfsplus_list_dir(HFSPlusVolume* vol, const char* path,
                     HFSPlusDirEntry** out_entries, int* out_count) {
    if (!vol || !vol->volume || !out_entries || !out_count) return -1;

    *out_entries = nullptr;
    *out_count = 0;

    std::string unix_path = mac_to_unix_path(path, vol->vol_name);

    PANIC_PROTECT_BEGIN()
        return -1;
    PANIC_PROTECT_END()

    HFSPlusCatalogRecord* rec = getRecordFromPath(unix_path.c_str(), vol->volume, NULL, NULL);
    s_panic_armed = false;
    if (!rec) return -1;

    if (rec->recordType != kHFSPlusFolderRecord) {
        free(rec);
        return -1;
    }

    HFSCatalogNodeID folderID = ((HFSPlusCatalogFolder*)rec)->folderID;
    free(rec);

    return list_dir_impl(vol, folderID, out_entries, out_count);
}

int hfsplus_list_dir_by_cnid(HFSPlusVolume* vol, uint32_t folder_cnid,
                              HFSPlusDirEntry** out_entries, int* out_count) {
    if (!vol || !vol->volume || !out_entries || !out_count) return -1;

    *out_entries = nullptr;
    *out_count = 0;

    return list_dir_impl(vol, (HFSCatalogNodeID)folder_cnid, out_entries, out_count);
}

void hfsplus_free_entries(HFSPlusDirEntry* entries) {
    free(entries);
}

int hfsplus_read_file(HFSPlusVolume* vol, const char* path,
                      uint8_t** out_data, size_t* out_size, int fork) {
    if (!vol || !vol->volume || !out_data || !out_size) return -1;

    *out_data = nullptr;
    *out_size = 0;

    std::string unix_path = mac_to_unix_path(path, vol->vol_name);

    PANIC_PROTECT_BEGIN()
        return -1;
    PANIC_PROTECT_END()

    HFSPlusCatalogRecord* rec = getRecordFromPath(unix_path.c_str(), vol->volume, NULL, NULL);
    s_panic_armed = false;
    if (!rec || rec->recordType != kHFSPlusFileRecord) {
        free(rec);
        return -1;
    }

    HFSPlusCatalogFile* file = (HFSPlusCatalogFile*)rec;

    HFSPlusForkData* forkData;
    if (fork == 1)
        forkData = &file->resourceFork;
    else
        forkData = &file->dataFork;

    uint64_t logical_size = forkData->logicalSize;
    if (logical_size == 0) {
        free(rec);
        return 0;
    }

    PANIC_PROTECT_BEGIN()
        free(rec);
        return -1;
    PANIC_PROTECT_END()

    io_func* io = openRawFile(file->fileID, forkData, (HFSPlusCatalogRecord*)file, vol->volume);
    s_panic_armed = false;

    if (!io) {
        free(rec);
        return -1;
    }

    uint8_t* data = (uint8_t*)malloc((size_t)logical_size);
    if (!data) {
        CLOSE(io);
        free(rec);
        return -1;
    }

    PANIC_PROTECT_BEGIN()
        free(data);
        CLOSE(io);
        free(rec);
        return -1;
    PANIC_PROTECT_END()

    int ok = READ(io, 0, (size_t)logical_size, data);
    s_panic_armed = false;

    CLOSE(io);
    free(rec);

    if (!ok) {
        free(data);
        return -1;
    }

    *out_data = data;
    *out_size = (size_t)logical_size;
    return 0;
}

int hfsplus_read_file_by_cnid(HFSPlusVolume* vol, uint32_t cnid, uint32_t parent_cnid,
                               uint8_t** out_data, size_t* out_size, int fork) {
    if (!vol || !vol->volume || !out_data || !out_size) return -1;

    *out_data = nullptr;
    *out_size = 0;

    // Search parent folder contents to find the file record by CNID.
    // This avoids issues with getRecordByCNID thread record lookups.
    PANIC_PROTECT_BEGIN()
        return -1;
    PANIC_PROTECT_END()

    CatalogRecordList* list = getFolderContents((HFSCatalogNodeID)parent_cnid, vol->volume);
    s_panic_armed = false;

    HFSPlusCatalogRecord* volatile rec = nullptr;
    CatalogRecordList* volatile cur = list;
    while (cur) {
        if (cur->record && cur->record->recordType == kHFSPlusFileRecord) {
            HFSPlusCatalogFile* f = (HFSPlusCatalogFile*)cur->record;
            if (f->fileID == (HFSCatalogNodeID)cnid) {
                // Copy the record since releaseCatalogRecordList will free it
                size_t rec_size = sizeof(HFSPlusCatalogFile);
                rec = (HFSPlusCatalogRecord*)malloc(rec_size);
                memcpy(rec, cur->record, rec_size);
                break;
            }
        }
        cur = cur->next;
    }
    releaseCatalogRecordList(list);

    if (!rec || rec->recordType != kHFSPlusFileRecord) {
        free(rec);
        return -1;
    }

    HFSPlusCatalogFile* file = (HFSPlusCatalogFile*)rec;

    HFSPlusForkData* forkData;
    if (fork == 1)
        forkData = &file->resourceFork;
    else
        forkData = &file->dataFork;

    uint64_t logical_size = forkData->logicalSize;
    if (logical_size == 0) {
        free(rec);
        return 0;
    }

    PANIC_PROTECT_BEGIN()
        free(rec);
        return -1;
    PANIC_PROTECT_END()

    io_func* io = openRawFile(file->fileID, forkData, (HFSPlusCatalogRecord*)file, vol->volume);
    s_panic_armed = false;

    if (!io) {
        free(rec);
        return -1;
    }

    uint8_t* data = (uint8_t*)malloc((size_t)logical_size);
    if (!data) {
        CLOSE(io);
        free(rec);
        return -1;
    }

    PANIC_PROTECT_BEGIN()
        free(data);
        CLOSE(io);
        free(rec);
        return -1;
    PANIC_PROTECT_END()

    int ok = READ(io, 0, (size_t)logical_size, data);
    s_panic_armed = false;

    CLOSE(io);
    free(rec);

    if (!ok) {
        free(data);
        return -1;
    }

    *out_data = data;
    *out_size = (size_t)logical_size;
    return 0;
}

int hfsplus_write_file(HFSPlusVolume* vol, const char* path,
                       const uint8_t* data, size_t size) {
    if (!vol || !vol->volume || vol->readonly) return -1;

    std::string unix_path = mac_to_unix_path(path, vol->vol_name);

    // Use add_hfs which creates the file if needed
    void* buf = malloc(size);
    if (!buf && size > 0) return -1;
    if (size > 0)
        memcpy(buf, data, size);

    AbstractFile* inFile = createAbstractFileFromMemory(&buf, size);
    if (!inFile) {
        free(buf);
        return -1;
    }

    PANIC_PROTECT_BEGIN()
        free(buf);
        return -1;
    PANIC_PROTECT_END()

    int ret = add_hfs(vol->volume, inFile, unix_path.c_str());
    s_panic_armed = false;

    // add_hfs closes inFile
    free(buf);

    return ret ? 0 : -1;
}

int hfsplus_write_rsrc_fork(HFSPlusVolume* vol, const char* path,
                            const uint8_t* data, size_t size) {
    if (!vol || !vol->volume || vol->readonly) return -1;
    if (size == 0) return 0;

    std::string unix_path = mac_to_unix_path(path, vol->vol_name);

    PANIC_PROTECT_BEGIN()
        return -1;
    PANIC_PROTECT_END()

    HFSPlusCatalogRecord* rec = getRecordFromPath(unix_path.c_str(), vol->volume, NULL, NULL);
    s_panic_armed = false;

    if (!rec || rec->recordType != kHFSPlusFileRecord) {
        free(rec);
        return -1;
    }

    HFSPlusCatalogFile* file = (HFSPlusCatalogFile*)rec;

    PANIC_PROTECT_BEGIN()
        free(rec);
        return -1;
    PANIC_PROTECT_END()

    io_func* io = openRawFile(file->fileID, &file->resourceFork,
                               (HFSPlusCatalogRecord*)file, vol->volume);
    s_panic_armed = false;

    if (!io) {
        free(rec);
        return -1;
    }

    PANIC_PROTECT_BEGIN()
        CLOSE(io);
        free(rec);
        return -1;
    PANIC_PROTECT_END()

    // Allocate space and write
    allocate((RawFile*)io->data, (off_t)size);
    int ok = WRITE(io, 0, size, (void*)data);
    s_panic_armed = false;

    CLOSE(io);
    free(rec);

    return ok ? 0 : -1;
}

int hfsplus_delete(HFSPlusVolume* vol, const char* path) {
    if (!vol || !vol->volume || vol->readonly) return -1;

    std::string unix_path = mac_to_unix_path(path, vol->vol_name);

    PANIC_PROTECT_BEGIN()
        return -1;
    PANIC_PROTECT_END()

    int ret = removeFile(unix_path.c_str(), vol->volume);
    s_panic_armed = false;

    return ret ? 0 : -1;
}

int hfsplus_mkdir(HFSPlusVolume* vol, const char* path) {
    if (!vol || !vol->volume || vol->readonly) return -1;

    std::string unix_path = mac_to_unix_path(path, vol->vol_name);

    PANIC_PROTECT_BEGIN()
        return -1;
    PANIC_PROTECT_END()

    HFSCatalogNodeID cnid = newFolder(unix_path.c_str(), vol->volume);
    s_panic_armed = false;

    return (cnid != 0) ? 0 : -1;
}

int hfsplus_rename(HFSPlusVolume* vol, const char* old_path, const char* new_path) {
    if (!vol || !vol->volume || vol->readonly) return -1;

    std::string old_unix = mac_to_unix_path(old_path, vol->vol_name);
    std::string new_unix = mac_to_unix_path(new_path, vol->vol_name);

    PANIC_PROTECT_BEGIN()
        return -1;
    PANIC_PROTECT_END()

    int ret = move(old_unix.c_str(), new_unix.c_str(), vol->volume);
    s_panic_armed = false;

    return ret ? 0 : -1;
}

uint32_t hfsplus_get_blessed(HFSPlusVolume* vol) {
    if (!vol || !vol->volume) return 0;
    // finderInfo[0] = blessed system folder CNID
    return vol->volume->volumeHeader->finderInfo[0];
}

int hfsplus_set_blessed(HFSPlusVolume* vol, uint32_t folder_cnid) {
    if (!vol || !vol->volume || vol->readonly) return -1;
    vol->volume->volumeHeader->finderInfo[0] = folder_cnid;

    PANIC_PROTECT_BEGIN()
        return -1;
    PANIC_PROTECT_END()

    updateVolume(vol->volume);
    s_panic_armed = false;

    return 0;
}

int hfsplus_set_type_creator(HFSPlusVolume* vol, const char* path,
                             const char* type, const char* creator) {
    if (!vol || !vol->volume || vol->readonly) return -1;

    std::string unix_path = mac_to_unix_path(path, vol->vol_name);

    PANIC_PROTECT_BEGIN()
        return -1;
    PANIC_PROTECT_END()

    HFSPlusCatalogRecord* rec = getRecordFromPath(unix_path.c_str(), vol->volume, NULL, NULL);
    s_panic_armed = false;

    if (!rec || rec->recordType != kHFSPlusFileRecord) {
        free(rec);
        return -1;
    }

    HFSPlusCatalogFile* file = (HFSPlusCatalogFile*)rec;

    // Set type and creator as big-endian 32-bit values
    uint32_t ft = ((uint32_t)(uint8_t)type[0] << 24) | ((uint32_t)(uint8_t)type[1] << 16) |
                  ((uint32_t)(uint8_t)type[2] << 8) | (uint8_t)type[3];
    uint32_t fc = ((uint32_t)(uint8_t)creator[0] << 24) | ((uint32_t)(uint8_t)creator[1] << 16) |
                  ((uint32_t)(uint8_t)creator[2] << 8) | (uint8_t)creator[3];

    file->userInfo.fileType = ft;
    file->userInfo.fileCreator = fc;

    PANIC_PROTECT_BEGIN()
        free(rec);
        return -1;
    PANIC_PROTECT_END()

    int ret = updateCatalog(vol->volume, rec);
    s_panic_armed = false;

    free(rec);
    return ret ? 0 : -1;
}
