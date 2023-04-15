#include "fat_manager.h"
#include <cstring>
#include <deque>
#include <fstream>
#include <functional>
#include <iostream>
#include <optional>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

namespace cs5250 {

template <typename T> bool IsOneOfVector(T &input, const std::vector<T> &args) {
    for (auto arg : args) {

        if (reinterpret_cast<uint64_t>(input) ==
            reinterpret_cast<uint64_t>(arg)) {
            return true;
        }
    }
    return false;
}

std::pair<std::string, bool>
NameOfLongNameEntry(const LongNameDirectory &entry) {
    std::string ret = "";
    auto [name1, end1] = LongNameDirectory::GetName(entry.LDIR_Name1);
    ret = ret + name1;
    if (end1)
        return {ret, true};

    auto [name2, end2] = LongNameDirectory::GetName(entry.LDIR_Name2);

    ret = ret + name2;
    if (end2)
        return {ret, true};

    auto [name3, end3] = LongNameDirectory::GetName(entry.LDIR_Name3);
    ret = ret + name3;
    if (end3)
        return {ret, true};
    return {ret, false};
}

static std::string ShortNameOf(const char name[11]) {
    std::string ret = "";
    for (auto i = 0; i < 8; ++i) {
        if (name[i] == ' ') {
            break;
        }
        ret = ret + name[i];
    }
    if (name[8] != ' ') {
        ret = ret + ".";
        for (auto i = 8; i < 11; ++i) {
            if (name[i] == ' ') {
                break;
            }
            ret = ret + name[i];
        }
    }
    return ret;
}

void FATManager::Ck() { std::cout << Info() << std::endl; }

void FATManager::InitBPB(const BPB &bpb) {
    auto root_dir_sector_count =
        ((bpb.BPB_RootEntCnt * 32) + (bpb.BPB_BytsPerSec - 1)) /
        bpb.BPB_BytsPerSec;

    // the number of sectors occupied by the FAT
    auto fat_size = 0;

    if (bpb.BPB_FATSz16 != 0)
        fat_size = bpb.BPB_FATSz16;
    else
        fat_size = bpb.fat32.BPB_FATSz32;
    this->sector_count_per_fat_ = fat_size;

    auto total_sector_count = 0;

    if (bpb.BPB_TotSec16 != 0)
        total_sector_count = bpb.BPB_TotSec16;
    else
        total_sector_count = bpb.BPB_TotSec32;

    auto data_sector_count =
        total_sector_count -
        (bpb.BPB_RsvdSecCnt + (bpb.BPB_NumFATs * fat_size) +
         root_dir_sector_count);

    auto count_of_clusters = data_sector_count / bpb.BPB_SecPerClus;

    this->count_of_clusters_ = count_of_clusters;

    if (count_of_clusters < 4085) {
        fat_type_ = FATType::FAT12;
    } else if (count_of_clusters < 65525) {
        fat_type_ = FATType::FAT16;
    } else {
        fat_type_ = FATType::FAT32;
        this->root_cluster_number_ = bpb.fat32.BPB_RootClus;
        ASSERT_EQ(root_dir_sector_count, 0);
    }

    bytes_per_sector_ = bpb.BPB_BytsPerSec;
    sectors_per_cluster_ = bpb.BPB_SecPerClus;
    reserved_sector_count_ = bpb.BPB_RsvdSecCnt;
    fat_sector_count_ = bpb.BPB_NumFATs * fat_size;
    this->root_dir_sector_count_ = root_dir_sector_count;
    this->root_dir_ = SimpleStruct{"", this->root_cluster_number_, true};
    this->data_sector_count_ = data_sector_count;
    this->number_of_fats_ = bpb.BPB_NumFATs;

    if (fat_type_ != FATType::FAT32) {
        return;
    }

    // use all the FATs
    std::vector<uint32_t *> fat_start_addresses;
    for (auto i = 0; i < bpb.BPB_NumFATs; ++i) {
        fat_start_addresses.push_back(reinterpret_cast<uint32_t *>(
            reinterpret_cast<uint64_t>(image_) +
            (bpb.BPB_RsvdSecCnt * bytes_per_sector_) +
            (i * fat_size * bytes_per_sector_)));
    }
    this->fat_map_ = std::make_unique<FATMap>(this->number_of_fats_,
                                              this->sector_count_per_fat_ *
                                                  bytes_per_sector_ / 4,
                                              std::move(fat_start_addresses));

    auto files_under_dir =
        [this](const SimpleStruct &file,
               const SimpleStruct &parent) -> std::vector<SimpleStruct> {
        std::vector<SimpleStruct> ret;
        auto seen_long_name = false;
        std::string long_name = "";
        std::vector<const LongNameDirectory *> long_name_dirs;

        auto sector_function = [this, &ret, &seen_long_name, &long_name, &file,
                                &parent,
                                &long_name_dirs](auto sector_data_address) {
            auto entry_parser = [this, &ret, &seen_long_name, &long_name, &file,
                                 &parent,
                                 &long_name_dirs](const FATDirectory *dir) {
                if (dir->DIR_Attr == ToIntegral(FATDirectory::Attr::LongName)) {
                    if (!seen_long_name) {
                        seen_long_name = true;
                        long_name = "";
                    }
                    const LongNameDirectory *long_dir =
                        reinterpret_cast<const LongNameDirectory *>(dir);

                    auto [name, end] = NameOfLongNameEntry(*long_dir);
                    long_name = name + long_name;
                    long_name_dirs.push_back(long_dir);
                } else {
                    std::string name;
                    std::vector<const LongNameDirectory *> this_long_name_dirs;
                    if (!seen_long_name) {
                        name = ShortNameOf(
                            reinterpret_cast<const char *>(dir->DIR_Name.name));
                    } else {
                        name = std::move(long_name);
                        seen_long_name = false;
                        this_long_name_dirs = std::move(long_name_dirs);
                    }
                    bool is_dir = false;

                    if (dir->DIR_Attr ==
                        ToIntegral(FATDirectory::Attr::Directory)) {
                        is_dir = true;
                    } else {
                        is_dir = false;
                    }
                    uint32_t cluster =
                        dir->DIR_FstClusLO | (dir->DIR_FstClusHI << 16);
                    if (cluster == file.first_cluster ||
                        cluster == parent.first_cluster || cluster == 0) {
                    } else {
                        if (this_long_name_dirs.size() > 0)
                            ret.push_back(
                                {name, cluster, is_dir, this_long_name_dirs});
                        else
                            ret.push_back({name, cluster, is_dir});
                    }
                }
            };
            ForEveryDirEntryInDirSector(sector_data_address, entry_parser);
        };

        ForEverySectorOfFile(file, sector_function);
        return ret;
    };

    std::deque<std::pair<SimpleStruct, SimpleStruct>> q;
    q.push_back({root_dir_, root_dir_});

    while (!q.empty()) {
        auto cur = q.front();
        q.pop_front();

        if (cur.first.is_dir) {
            auto sub_lists = files_under_dir(cur.first, cur.second);
            for (auto &list : sub_lists) {
                q.push_back({list, cur.first});
            }
            dir_map_[cur.first] = std::move(sub_lists);
        }
    }

    auto fs_info_sector_number = bpb.fat32.BPB_FSInfo;
    this->fs_info_manager_ =
        std::make_unique<FSInfoManager>(reinterpret_cast<uint8_t *>(
            this->image_ + fs_info_sector_number * bytes_per_sector_));
}

void FATManager::Ls() {

    ASSERT(fat_type_ == FATType::FAT32);

    // recursively print the map
    std::function<void(const SimpleStruct &, std::string prefix)> print_map =
        [&](const SimpleStruct &cur, std::string prefix) {
            if (dir_map_.find(cur) != dir_map_.end())
                for (auto &sub : dir_map_[cur])
                    print_map(sub, prefix + cur.name + "/");
            else
                std::cout << prefix << cur.name << std::endl;
        };

    print_map(root_dir_, "");
}

OptionalRef<SimpleStruct> FATManager::FindFile(const std::string &path) {
    auto &current_dir = dir_map_[root_dir_];

    // split the path by '/'
    std::vector<std::string> path_list;
    std::string cur = "";
    for (auto c : path) {
        if (c == '/') {
            if (cur != "") {
                path_list.push_back(cur);
                cur = "";
            }
        } else {
            cur = cur + c;
        }
    }
    if (cur != "") {
        path_list.push_back(cur);
    }

    auto find_dir = [&](std::string name) -> OptionalRef<SimpleStruct> {
        for (auto &dir : current_dir) {
            if (dir.name == name && dir.is_dir) {
                return dir;
            }
        }
        return std::nullopt;
    };

    auto find_file = [&](std::string name) -> OptionalRef<SimpleStruct> {
        for (auto &dir : current_dir) {
            if (dir.name == name && !dir.is_dir) {
                return dir;
            }
        }
        return std::nullopt;
    };

    for (auto &p : path_list) {
        if (p == path_list.back()) {
            auto file = find_file(p);
            if (file) {
                return file;
            } else {
                return std::nullopt;
            }
        } else {
            auto dir = find_dir(p);
            if (dir) {
                current_dir = dir_map_[*dir];
            } else {
                return std::nullopt;
            }
        }
    }

    return std::nullopt;
}

std::optional<std::vector<std::reference_wrapper<SimpleStruct>>>
FATManager::FindFileWithDirs(const std::string &path) {
    auto &current_dir = dir_map_[root_dir_];

    // split the path by '/'
    std::vector<std::string> path_list;
    std::string cur = "";
    for (auto c : path) {
        if (c == '/') {
            if (cur != "") {
                path_list.push_back(cur);
                cur = "";
            }
        } else {
            cur = cur + c;
        }
    }
    if (cur != "") {
        path_list.push_back(cur);
    }

    auto find = [&](std::string name) -> OptionalRef<SimpleStruct> {
        for (auto &dir : current_dir) {
            if (dir.name == name) {
                return dir;
            }
        }
        return std::nullopt;
    };

    std::vector<std::reference_wrapper<SimpleStruct>> ret;
    for (auto &p : path_list) {
        if (auto file = find(p); file) {
            ret.push_back(*file);
            if (p == path_list.back()) {
                return ret;
            } else if (file->get().is_dir) {
                current_dir = dir_map_[*file];
            } else {
                return std::nullopt;
            }
        } else {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

void FATManager::CopyFileTo(const std::string &path, const std::string &dest) {
    auto file = FindFile(path);
    if (!file) {
        std::cerr << "file " << path << " not found" << std::endl;
        std::exit(1);
    }

    // open a file for creating or writing
    auto file_ptr = std::make_unique<std::ofstream>(dest, std::ios::binary);
    if (!file_ptr->is_open()) {
        std::cerr << "failed to open file " << dest << std::endl;
        std::exit(1);
    }

    ForEverySectorOfFile(file->get(), [this, &file_ptr](const uint8_t *data) {
        // write the data to the file
        file_ptr->write(reinterpret_cast<const char *>(data),
                        bytes_per_sector_);
    });
}

void FATManager::Delete(const std::string &path) {
    auto detailed_file_option = FindFileWithDirs(path);
    if (!detailed_file_option) {
        std::cerr << "file " << path << " not found" << std::endl;
        std::exit(1);
    }

    auto &detailed_file = detailed_file_option.value();
    auto file = detailed_file.back().get();
    auto is_dir = file.is_dir;

    if (is_dir) {
        DeleteSingleDir(file);
    } else {
        DeleteSingleFile(file);
    }

    auto is_under_root = detailed_file.size() == 1;
    if (is_under_root) {
        RemoveEntryInDir(root_dir_, file);
    } else {
        auto parent = detailed_file.at(detailed_file.size() - 2).get();
        RemoveEntryInDir(parent, file);
    }
}

#define UNIMPLEMENTED()                                                        \
    {                                                                          \
        std::cerr << "unimplemented" << std::endl;                             \
        std::exit(1);                                                          \
    }

void FATManager::DeleteSingleDir(const SimpleStruct &dir) {
    auto inner_files = dir_map_[dir];
    for (auto &file : inner_files) {
        if (file.is_dir) {
            DeleteSingleDir(file);
        } else {
            DeleteSingleFile(file);
        }
    }
    DeleteSingleFile(dir);
}

void FATManager::DeleteSingleFile(const SimpleStruct &file) {
    auto cluster_entries = ClustersOfFile(file);

    for (auto cluster : cluster_entries) {
        this->fat_map_->SetFree(cluster);
        this->IncreaseFreeClusterCount(1);
    }
}

void FATManager::RemoveEntryInDir(const SimpleStruct &dir,
                                  const SimpleStruct &file) {

    ForEverySectorOfFile(dir, [this, &file](const uint8_t *sector_address) {
        ForEveryDirEntryInDirSector(
            sector_address, [this, &file](const FATDirectory *entry) {
                if (entry->DIR_Attr ==
                    ToIntegral(FATDirectory::Attr::LongName)) {
                    const LongNameDirectory *long_dir =
                        reinterpret_cast<const LongNameDirectory *>(entry);

                    if (file.long_name_entries &&
                        IsOneOfVector(long_dir,
                                      file.long_name_entries.value())) {

                        ASSERT(entry->DIR_Name.name[0] != 0xE5);
                        memset((void *)(&(entry->DIR_Name.name[0])), 0xE5, 1);
                        ASSERT(entry->DIR_Name.name[0] == 0xE5);
                    }
                } else {
                    uint32_t cluster =
                        entry->DIR_FstClusLO | (entry->DIR_FstClusHI << 16);
                    if (cluster == file.first_cluster) {
                        memset((void *)(&entry->DIR_Name.name[0]), 0xE5, 1);
                    }
                }
            });
    });
}

void FATManager::CopyFileFrom(const std::string &path,
                              const std::string &dest) {

    auto file = FindFile(dest);
    if (file) {
        std::cerr << "file " << dest << " already exists" << std::endl;
        std::exit(1);
    } else {
        // open path for reading
        auto c_file_fd = open(path.c_str(), O_RDONLY);
        if (c_file_fd == -1) {
            std::cerr << "failed to open file " << path << std::endl;
            std::exit(1);
        }

        // get the size of the file
        struct stat file_stat;
        if (fstat(c_file_fd, &file_stat) == -1) {
            std::cerr << "failed to get file stat" << std::endl;
            close(c_file_fd);
            std::exit(1);
        }
        auto size = file_stat.st_size;

        // calculate the number of clusters needed
        uint32_t sector_count_per_cluster = sectors_per_cluster_;
        uint32_t bytes_per_cluster =
            bytes_per_sector_ * sector_count_per_cluster;
        uint32_t cluster_count_needed =
            (size + bytes_per_cluster - 1) / bytes_per_cluster;

        // if the file is too large, exit

        if (cluster_count_needed >
            this->fs_info_manager_->GetFreeClusterCount()) {
            std::cerr << "file too large" << std::endl;
            close(c_file_fd);
            std::exit(1);
        }

        char buffer[bytes_per_sector_];
        auto size_read = 0;
        while (true) {
            size_t n;
            if (n = read(c_file_fd, buffer, bytes_per_sector_); n == 0) {
                break;
            }
            size_read += n;
        }

        

        // get the first cluster
        auto first_free_cluster = this->fs_info_manager_->GetNextFreeCluster();

        // close the file
        close(c_file_fd);
    }
}

} // namespace cs5250