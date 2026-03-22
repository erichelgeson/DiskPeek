/*
 * HFS Browser Library - Volume implementation (HFS + HFS+ backends)
 */

#include "hfsbrowse.h"
#include "encoding.h"
#include "icon.h"

#include <cstdio>
#include <cstring>
#include <functional>

extern "C" {
#include "hfs.h"
}
#include "hfsplus_ops.h"

namespace hfsbrowse {

// ============================================================
// HFS Volume
// ============================================================

class HFSVolumeImpl : public Volume {
public:
    HFSVolumeImpl(hfsvol* vol, bool ro) : vol_(vol), readonly_(ro) {
        hfsvolent vstat;
        if (hfs_vstat(vol_, &vstat) == 0) {
            name_ = vstat.name;
            total_ = vstat.totbytes;
            free_ = vstat.freebytes;
            blessed_ = vstat.blessed;
            numfiles_ = vstat.numfiles;
            numdirs_ = vstat.numdirs;
        }
    }

    ~HFSVolumeImpl() override { if (vol_) hfs_umount(vol_); }

    VolumeType type() const override { return VolumeType::HFS; }
    std::string name() const override { return name_; }
    uint64_t total_bytes() const override { return total_; }
    uint64_t free_bytes() const override { return free_; }
    uint32_t blessed_folder() const override { return blessed_; }
    bool is_readonly() const override { return readonly_; }

    void refresh_stats() override {
        hfsvolent vstat;
        if (hfs_vstat(vol_, &vstat) == 0) {
            total_ = vstat.totbytes;
            free_ = vstat.freebytes;
            blessed_ = vstat.blessed;
        }
    }

    // HFS doesn't support CNID-based listing
    std::vector<HBEntry> list_dir(uint32_t) override { return {}; }

    std::vector<HBEntry> list_dir_by_path(const std::string& path) override {
        std::vector<HBEntry> result;
        hfsdir* dir = hfs_opendir(vol_, path.c_str());
        if (!dir) return result;
        hfsdirent ent;
        while (hfs_readdir(dir, &ent) == 0) {
            HBEntry e;
            e.name = ent.name;
            e.is_dir = (ent.flags & HFS_ISDIR) != 0;
            e.cnid = ent.cnid;
            e.fdflags = ent.fdflags;
            if (e.is_dir) {
                e.data_size = 0; e.rsrc_size = 0;
                memset(e.type, 0, 5); memset(e.creator, 0, 5);
            } else {
                e.data_size = ent.u.file.dsize;
                e.rsrc_size = ent.u.file.rsize;
                memcpy(e.type, ent.u.file.type, 5);
                memcpy(e.creator, ent.u.file.creator, 5);
            }
            result.push_back(std::move(e));
        }
        hfs_closedir(dir);
        return result;
    }

    // HFS doesn't support CNID-based reading
    std::vector<uint8_t> read_fork(uint32_t, uint32_t, int) override { return {}; }

    std::vector<uint8_t> read_fork_by_path(const std::string& path, int fork) override {
        hfsfile* f = hfs_open(vol_, path.c_str());
        if (!f) return {};
        if (fork == 1) hfs_setfork(f, 1);
        unsigned long size = hfs_seek(f, 0, HFS_SEEK_END);
        if (size == 0 || size == (unsigned long)-1) { hfs_close(f); return {}; }
        hfs_seek(f, 0, HFS_SEEK_SET);
        std::vector<uint8_t> data(size);
        unsigned long total = 0;
        while (total < size) {
            unsigned long n = hfs_read(f, data.data() + total, size - total);
            if (n == 0 || n == (unsigned long)-1) break;
            total += n;
        }
        hfs_close(f);
        data.resize(total);
        return data;
    }

    int write_file(const std::string& path, const uint8_t* data, size_t size) override {
        return create_file(path, "????", "????", data, size);
    }

    int write_rsrc_fork(const std::string& path, const uint8_t* data, size_t size) override {
        hfsfile* f = hfs_open(vol_, path.c_str());
        if (!f) return -1;
        hfs_setfork(f, 1);
        hfs_write(f, data, (unsigned long)size);
        hfs_close(f);
        return 0;
    }

    int create_file(const std::string& path, const char* t, const char* c,
                    const uint8_t* data, size_t size) override {
        hfsfile* f = hfs_create(vol_, path.c_str(), t, c);
        if (!f) return -1;
        if (size > 0) hfs_write(f, data, (unsigned long)size);
        hfs_close(f);
        return 0;
    }

    int mkdir(const std::string& path) override { return hfs_mkdir(vol_, path.c_str()); }
    int rmdir(const std::string& path) override { return hfs_rmdir(vol_, path.c_str()); }
    int delete_file(const std::string& path) override { return hfs_delete(vol_, path.c_str()); }
    int rename(const std::string& old_p, const std::string& new_p) override {
        return hfs_rename(vol_, old_p.c_str(), new_p.c_str());
    }
    int chdir(const std::string& path) override { return hfs_chdir(vol_, path.c_str()); }

    int set_type_creator(const std::string& path, const char* t, const char* c) override {
        hfsdirent ent;
        if (hfs_stat(vol_, path.c_str(), &ent) != 0) return -1;
        memcpy(ent.u.file.type, t, 5);
        memcpy(ent.u.file.creator, c, 5);
        return hfs_setattr(vol_, path.c_str(), &ent);
    }

    int set_finder_flags(const std::string& path, int16_t flags) override {
        hfsdirent ent;
        if (hfs_stat(vol_, path.c_str(), &ent) != 0) return -1;
        ent.fdflags = flags;
        return hfs_setattr(vol_, path.c_str(), &ent);
    }

    int set_blessed(uint32_t cnid) override {
        hfsvolent vstat;
        if (hfs_vstat(vol_, &vstat) != 0) return -1;
        vstat.blessed = cnid;
        int rc = hfs_vsetattr(vol_, &vstat);
        if (rc == 0) blessed_ = cnid;
        return rc;
    }

    int get_finder_info(const std::string& path, char* t, char* c, int16_t* flags) override {
        hfsdirent ent;
        if (hfs_stat(vol_, path.c_str(), &ent) != 0) return -1;
        if (t) memcpy(t, ent.u.file.type, 5);
        if (c) memcpy(c, ent.u.file.creator, 5);
        if (flags) *flags = ent.fdflags;
        return 0;
    }

    std::string check() override {
        std::string log;
        hfsvolent vstat;
        if (hfs_vstat(vol_, &vstat) != 0) return "ERROR: Cannot read volume info\n";
        log += "=== HFS Volume Check ===\n";
        log += "Volume: " + std::string(vstat.name) + "\n";
        log += "Files: " + std::to_string(vstat.numfiles) + "  Dirs: " + std::to_string(vstat.numdirs) + "\n\n";
        log += "Checking catalog tree...\n";
        int fc = 0, dc = 0, errors = 0;
        std::function<void(const std::string&)> walk;
        walk = [&](const std::string& p) {
            hfsdir* dir = hfs_opendir(vol_, p.c_str());
            if (!dir) { errors++; log += "  ERROR: " + p + "\n"; return; }
            hfsdirent ent;
            while (hfs_readdir(dir, &ent) == 0) {
                if (ent.flags & HFS_ISDIR) { dc++; walk(p + ent.name + ":"); }
                else fc++;
            }
            hfs_closedir(dir);
        };
        walk(std::string(vstat.name) + ":");
        log += "  " + std::to_string(fc) + " files, " + std::to_string(dc) + " dirs\n";
        if ((unsigned long)fc != vstat.numfiles) log += "  WARNING: file count mismatch\n";
        if ((unsigned long)dc != vstat.numdirs) log += "  WARNING: dir count mismatch\n";
        log += errors ? "\n" + std::to_string(errors) + " error(s)\n" : "\nOK\n";
        return log;
    }

    hfsvol* raw() { return vol_; }

private:
    hfsvol* vol_;
    bool readonly_;
    std::string name_;
    uint64_t total_ = 0, free_ = 0;
    uint32_t blessed_ = 0;
    unsigned long numfiles_ = 0, numdirs_ = 0;
};

// ============================================================
// HFS+ Volume
// ============================================================

class HFSPlusVolImpl : public Volume {
public:
    HFSPlusVolImpl(HFSPlusVolume* vol, bool ro) : vol_(vol), readonly_(ro) {
        name_ = hfsplus_volume_name(vol_);
        total_ = hfsplus_total_bytes(vol_);
        free_ = hfsplus_free_bytes(vol_);
        blessed_ = hfsplus_get_blessed(vol_);
    }

    ~HFSPlusVolImpl() override { if (vol_) hfsplus_close(vol_); }

    VolumeType type() const override { return VolumeType::HFSPLUS; }
    std::string name() const override { return name_; }
    uint64_t total_bytes() const override { return total_; }
    uint64_t free_bytes() const override { return free_; }
    uint32_t blessed_folder() const override { return blessed_; }
    bool is_readonly() const override { return readonly_; }

    void refresh_stats() override {
        total_ = hfsplus_total_bytes(vol_);
        free_ = hfsplus_free_bytes(vol_);
        blessed_ = hfsplus_get_blessed(vol_);
    }

    std::vector<HBEntry> list_dir(uint32_t folder_cnid) override {
        std::vector<HBEntry> result;
        HFSPlusDirEntry* ents = nullptr;
        int cnt = 0;
        if (hfsplus_list_dir_by_cnid(vol_, folder_cnid, &ents, &cnt) != 0)
            return result;
        for (int i = 0; i < cnt; i++) {
            HBEntry e;
            e.name = ents[i].name;
            e.is_dir = ents[i].is_dir;
            e.cnid = ents[i].cnid;
            e.parent_cnid = ents[i].parent_cnid;
            e.fdflags = ents[i].finder_flags;
            e.data_size = ents[i].data_size;
            e.rsrc_size = ents[i].rsrc_size;
            memcpy(e.type, ents[i].type, 5);
            memcpy(e.creator, ents[i].creator, 5);
            result.push_back(std::move(e));
        }
        hfsplus_free_entries(ents);
        return result;
    }

    // HFS+ can also list by path (via hfsplus_list_dir)
    std::vector<HBEntry> list_dir_by_path(const std::string& path) override {
        // Use the path-based version (which may fail for special chars)
        std::vector<HBEntry> result;
        HFSPlusDirEntry* ents = nullptr;
        int cnt = 0;
        if (hfsplus_list_dir(vol_, path.c_str(), &ents, &cnt) != 0)
            return result;
        for (int i = 0; i < cnt; i++) {
            HBEntry e;
            e.name = ents[i].name;
            e.is_dir = ents[i].is_dir;
            e.cnid = ents[i].cnid;
            e.parent_cnid = ents[i].parent_cnid;
            e.fdflags = ents[i].finder_flags;
            e.data_size = ents[i].data_size;
            e.rsrc_size = ents[i].rsrc_size;
            memcpy(e.type, ents[i].type, 5);
            memcpy(e.creator, ents[i].creator, 5);
            result.push_back(std::move(e));
        }
        hfsplus_free_entries(ents);
        return result;
    }

    std::vector<uint8_t> read_fork(uint32_t cnid, uint32_t parent_cnid, int fork) override {
        uint8_t* data = nullptr;
        size_t size = 0;
        if (hfsplus_read_file_by_cnid(vol_, cnid, parent_cnid, &data, &size, fork) != 0)
            return {};
        if (!data) return {};
        std::vector<uint8_t> result(data, data + size);
        free(data);
        return result;
    }

    std::vector<uint8_t> read_fork_by_path(const std::string& path, int fork) override {
        uint8_t* data = nullptr;
        size_t size = 0;
        if (hfsplus_read_file(vol_, path.c_str(), &data, &size, fork) != 0)
            return {};
        if (!data) return {};
        std::vector<uint8_t> result(data, data + size);
        free(data);
        return result;
    }

    int write_file(const std::string& path, const uint8_t* data, size_t size) override {
        return hfsplus_write_file(vol_, path.c_str(), data, size);
    }

    int write_rsrc_fork(const std::string& path, const uint8_t* data, size_t size) override {
        return hfsplus_write_rsrc_fork(vol_, path.c_str(), data, size);
    }

    int create_file(const std::string& path, const char*, const char*,
                    const uint8_t* data, size_t size) override {
        // HFS+ doesn't set type/creator at creation time
        return hfsplus_write_file(vol_, path.c_str(), data, size);
    }

    int mkdir(const std::string& path) override { return hfsplus_mkdir(vol_, path.c_str()); }
    int rmdir(const std::string& path) override { return hfsplus_delete(vol_, path.c_str()); }
    int delete_file(const std::string& path) override { return hfsplus_delete(vol_, path.c_str()); }
    int rename(const std::string& old_p, const std::string& new_p) override {
        return hfsplus_rename(vol_, old_p.c_str(), new_p.c_str());
    }
    int chdir(const std::string&) override { return 0; } // no-op for HFS+

    int set_type_creator(const std::string& path, const char* t, const char* c) override {
        return hfsplus_set_type_creator(vol_, path.c_str(), t, c);
    }

    int set_finder_flags(const std::string&, int16_t) override { return -1; } // TODO

    int set_blessed(uint32_t cnid) override {
        int rc = hfsplus_set_blessed(vol_, cnid);
        if (rc == 0) blessed_ = cnid;
        return rc;
    }

    int get_finder_info(const std::string&, char*, char*, int16_t*) override { return -1; } // TODO

    std::string check() override {
        std::string log;
        log += "=== HFS+ Volume Check ===\n";
        log += "Volume: " + name_ + "\n\n";
        log += "Checking catalog tree...\n";
        int fc = 0, dc = 0, errors = 0;
        std::function<void(uint32_t)> walk;
        walk = [&](uint32_t fcnid) {
            auto entries = list_dir(fcnid);
            if (entries.empty() && fcnid != 2) errors++;
            for (auto& e : entries) {
                if (e.is_dir) { dc++; walk(e.cnid); }
                else fc++;
            }
        };
        walk(2);
        log += "  " + std::to_string(fc) + " files, " + std::to_string(dc) + " dirs\n";
        log += errors ? "\n" + std::to_string(errors) + " error(s)\n" : "\nOK\n";
        return log;
    }

    HFSPlusVolume* raw() { return vol_; }

private:
    HFSPlusVolume* vol_;
    bool readonly_;
    std::string name_;
    uint64_t total_ = 0, free_ = 0;
    uint32_t blessed_ = 0;
};

} // namespace hfsbrowse
