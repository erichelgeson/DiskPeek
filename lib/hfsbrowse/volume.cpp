/*
 * HFS Browser Library - Volume implementation (HFS + HFS+ backends)
 */

#include "volume.h"
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

// --- HFS Volume ---

class HFSVolume : public Volume {
public:
    HFSVolume(hfsvol* vol, bool ro) : vol_(vol), readonly_(ro) {
        hfsvolent vstat;
        if (hfs_vstat(vol_, &vstat) == 0) {
            name_ = vstat.name;
            total_ = vstat.totbytes;
            free_ = vstat.freebytes;
            blessed_ = vstat.blessed;
        }
    }

    ~HFSVolume() override {
        if (vol_) hfs_umount(vol_);
    }

    VolumeType type() const override { return VolumeType::HFS; }
    std::string name() const override { return name_; }
    uint64_t total_bytes() const override { return total_; }
    uint64_t free_bytes() const override { return free_; }
    uint32_t blessed_folder() const override { return blessed_; }
    bool is_readonly() const override { return readonly_; }

    std::vector<HBEntry> list_dir(uint32_t /*folder_cnid*/) override {
        // HFS uses path-based listing via hfs_opendir with current_path
        // This is called from the GUI which manages current_path
        // For the library API, we need the path. This is a limitation
        // that will be resolved when we track paths internally.
        return {};
    }

    // Path-based listing for HFS (since HFS doesn't have CNID-based listing)
    std::vector<HBEntry> list_dir_by_path(const std::string& path) {
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

    std::vector<uint8_t> read_fork(uint32_t /*cnid*/, uint32_t /*parent_cnid*/, int /*fork*/) override {
        // HFS reads are path-based — see read_fork_by_path
        return {};
    }

    std::vector<uint8_t> read_fork_by_path(const std::string& path, int fork) {
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
        hfsfile* f = hfs_create(vol_, path.c_str(), "????", "????");
        if (!f) return -1;
        unsigned long written = hfs_write(f, data, (unsigned long)size);
        hfs_close(f);
        return (written == (unsigned long)size) ? 0 : -1;
    }

    int write_rsrc_fork(const std::string& path, const uint8_t* data, size_t size) override {
        hfsfile* f = hfs_open(vol_, path.c_str());
        if (!f) return -1;
        hfs_setfork(f, 1);
        hfs_write(f, data, (unsigned long)size);
        hfs_close(f);
        return 0;
    }

    int mkdir(const std::string& path) override { return hfs_mkdir(vol_, path.c_str()); }
    int delete_entry(const std::string& path) override { return hfs_delete(vol_, path.c_str()); }

    int rename(const std::string& old_path, const std::string& new_path) override {
        return hfs_rename(vol_, old_path.c_str(), new_path.c_str());
    }

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

    int set_blessed(uint32_t folder_cnid) override {
        hfsvolent vstat;
        if (hfs_vstat(vol_, &vstat) != 0) return -1;
        vstat.blessed = folder_cnid;
        int rc = hfs_vsetattr(vol_, &vstat);
        if (rc == 0) blessed_ = folder_cnid;
        return rc;
    }

    int chdir(const std::string& path) { return hfs_chdir(vol_, path.c_str()); }

    int rmdir(const std::string& path) { return hfs_rmdir(vol_, path.c_str()); }

    hfsfile* open_file(const std::string& path) { return hfs_open(vol_, path.c_str()); }

    hfsfile* create_file(const std::string& path, const char* t, const char* c) {
        return hfs_create(vol_, path.c_str(), t, c);
    }

    std::string check() override {
        std::string log;
        hfsvolent vstat;
        if (hfs_vstat(vol_, &vstat) != 0) return "ERROR: Cannot read volume info\n";

        log += "=== HFS Volume Check ===\n";
        log += "Volume: " + std::string(vstat.name) + "\n";
        log += "Files: " + std::to_string(vstat.numfiles) + "  Dirs: " + std::to_string(vstat.numdirs) + "\n\n";
        log += "Checking catalog tree...\n";

        int file_count = 0, dir_count = 0, errors = 0;
        std::function<void(const std::string&)> walk;
        walk = [&](const std::string& path) {
            hfsdir* dir = hfs_opendir(vol_, path.c_str());
            if (!dir) { errors++; log += "  ERROR: cannot open " + path + "\n"; return; }
            hfsdirent ent;
            while (hfs_readdir(dir, &ent) == 0) {
                if (ent.flags & HFS_ISDIR) { dir_count++; walk(path + ent.name + ":"); }
                else { file_count++; }
            }
            hfs_closedir(dir);
        };
        walk(std::string(vstat.name) + ":");

        log += "  Found " + std::to_string(file_count) + " files, " + std::to_string(dir_count) + " dirs\n";
        if ((unsigned long)file_count != vstat.numfiles)
            log += "  WARNING: file count mismatch (MDB says " + std::to_string(vstat.numfiles) + ")\n";
        if ((unsigned long)dir_count != vstat.numdirs)
            log += "  WARNING: dir count mismatch (MDB says " + std::to_string(vstat.numdirs) + ")\n";
        log += (errors == 0) ? "\nVolume appears OK.\n" : "\n" + std::to_string(errors) + " error(s) found.\n";
        return log;
    }

    void refresh_stats() {
        hfsvolent vstat;
        if (hfs_vstat(vol_, &vstat) == 0) {
            total_ = vstat.totbytes;
            free_ = vstat.freebytes;
            blessed_ = vstat.blessed;
        }
    }

    hfsvol* raw() { return vol_; }

private:
    hfsvol* vol_;
    bool readonly_;
    std::string name_;
    uint64_t total_ = 0, free_ = 0;
    uint32_t blessed_ = 0;
};

// --- HFS+ Volume ---

class HFSPlusVol : public Volume {
public:
    HFSPlusVol(HFSPlusVolume* vol, bool ro) : vol_(vol), readonly_(ro) {
        name_ = hfsplus_volume_name(vol_);
        total_ = hfsplus_total_bytes(vol_);
        free_ = hfsplus_free_bytes(vol_);
        blessed_ = hfsplus_get_blessed(vol_);
    }

    ~HFSPlusVol() override {
        if (vol_) hfsplus_close(vol_);
    }

    VolumeType type() const override { return VolumeType::HFSPLUS; }
    std::string name() const override { return name_; }
    uint64_t total_bytes() const override { return total_; }
    uint64_t free_bytes() const override { return free_; }
    uint32_t blessed_folder() const override { return blessed_; }
    bool is_readonly() const override { return readonly_; }

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

    int write_file(const std::string& path, const uint8_t* data, size_t size) override {
        return hfsplus_write_file(vol_, path.c_str(), data, size);
    }

    int write_rsrc_fork(const std::string& path, const uint8_t* data, size_t size) override {
        return hfsplus_write_rsrc_fork(vol_, path.c_str(), data, size);
    }

    int mkdir(const std::string& path) override { return hfsplus_mkdir(vol_, path.c_str()); }
    int delete_entry(const std::string& path) override { return hfsplus_delete(vol_, path.c_str()); }
    int rename(const std::string& old_path, const std::string& new_path) override {
        return hfsplus_rename(vol_, old_path.c_str(), new_path.c_str());
    }

    int set_type_creator(const std::string& path, const char* t, const char* c) override {
        return hfsplus_set_type_creator(vol_, path.c_str(), t, c);
    }

    int set_finder_flags(const std::string& /*path*/, int16_t /*flags*/) override {
        // TODO: implement HFS+ finder flags setting
        return -1;
    }

    int set_blessed(uint32_t folder_cnid) override {
        int rc = hfsplus_set_blessed(vol_, folder_cnid);
        if (rc == 0) blessed_ = folder_cnid;
        return rc;
    }

    std::string check() override {
        std::string log;
        log += "=== HFS+ Volume Check ===\n";
        log += "Volume: " + name_ + "\n\n";
        log += "Checking catalog tree...\n";
        int file_count = 0, dir_count = 0, errors = 0;
        std::function<void(uint32_t)> walk;
        walk = [&](uint32_t fcnid) {
            auto entries = list_dir(fcnid);
            for (auto& e : entries) {
                if (e.is_dir) { dir_count++; walk(e.cnid); }
                else { file_count++; }
            }
            if (entries.empty() && fcnid != 2) errors++; // non-root empty = possible error
        };
        walk(2);
        log += "  Found " + std::to_string(file_count) + " files, " + std::to_string(dir_count) + " dirs\n";
        log += (errors == 0) ? "\nVolume appears OK.\n" : "\n" + std::to_string(errors) + " error(s) found.\n";
        return log;
    }

    void refresh_stats() {
        total_ = hfsplus_total_bytes(vol_);
        free_ = hfsplus_free_bytes(vol_);
        blessed_ = hfsplus_get_blessed(vol_);
    }

    HFSPlusVolume* raw() { return vol_; }

private:
    HFSPlusVolume* vol_;
    bool readonly_;
    std::string name_;
    uint64_t total_ = 0, free_ = 0;
    uint32_t blessed_ = 0;
};

// --- Factory ---
// Note: The full open logic (APM detection, HFS wrapper detection) remains in
// gui/app.cpp for now since it's complex and tightly coupled with the UI flow.
// This will be migrated in a future step.

} // namespace hfsbrowse
