#include "fat_manager.h"
#include <deque>
#include <fstream>
#include <functional>
#include <iostream>
#include <optional>
#include <unordered_map>
#include <vector>

namespace cs5250 {

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

    auto fat_start_address =
        reinterpret_cast<uint32_t *>(reinterpret_cast<uint64_t>(image_) +
                                     (bpb.BPB_RsvdSecCnt * bytes_per_sector_));

    // only use the first FAT
    this->fat_map_ = std::make_unique<FATMap>(
        this->sector_count_per_fat_ * bytes_per_sector_ / 4, fat_start_address);

    auto files_under_dir =
        [this](const SimpleStruct &file,
               const SimpleStruct &parent) -> std::vector<SimpleStruct> {
        std::vector<SimpleStruct> ret;
        auto seen_long_name = false;
        std::string long_name = "";
        std::vector<LongNameDirectory *> long_name_dirs;

        auto sector_function = [this, &ret, &seen_long_name, &long_name, &file,
                                &parent, &long_name_dirs](auto sector_data_address) {
            auto dirs_per_sector = bytes_per_sector_ / sizeof(FATDirectory);
            for (decltype(dirs_per_sector) j = 0; j < dirs_per_sector; ++j) {
                auto dir = reinterpret_cast<const FATDirectory *>(
                    sector_data_address + j * sizeof(FATDirectory));

                if (dir->DIR_Attr == ToIntegral(FATDirectory::Attr::LongName)) {
                    if (!seen_long_name) {
                        seen_long_name = true;
                        long_name = "";
                    }
                    const LongNameDirectory * long_dir =
                        reinterpret_cast<const LongNameDirectory *>(dir);

                    auto parse_long_name =
                        [](const LongNameDirectory *long_name_dir)
                        -> std::pair<std::string, bool> {
                        std::string ret = "";
                        auto [name1, end1] = LongNameDirectory::getName(
                            long_name_dir->LDIR_Name1);
                        ret = ret + name1;
                        if (end1)
                            return {ret, true};

                        auto [name2, end2] = LongNameDirectory::getName(
                            long_name_dir->LDIR_Name2);

                        ret = ret + name2;
                        if (end2)
                            return {ret, true};

                        auto [name3, end3] = LongNameDirectory::getName(
                            long_name_dir->LDIR_Name3);
                        ret = ret + name3;
                        if (end3)
                            return {ret, true};
                        return {ret, false};
                    };

                    auto [name, end] = parse_long_name(long_dir);
                    long_name = name + long_name;
                    long_name_dirs.push_back(long_dir);
                } else {
                    auto isFreeDir = [](const FATDirectory *dir) {
                        return dir->DIR_Name.name[0] == 0x00 || // free
                               dir->DIR_Name.name[0] == 0xE5;   // deleted
                    };

                    auto is_free = isFreeDir(dir);

                    if (is_free) {
                        if (seen_long_name) {
                            std::cerr << "long name not ended but free "
                                         "dir found";
                            std::abort();
                        } else
                            break;
                    }
                    std::string name;
                    std::vector<LongNameDirectory *> this_long_name_dirs;
                    if (!seen_long_name) {
                        name = ShortNameOf(dir->DIR_Name.name);
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
}

void FATManager::Ls() {
    ASSERT(fat_type_ == FATType::FAT32);

    // recursively print the map
    std::function<void(const SimpleStruct &, std::string prefix)> print_map =
        [&](const SimpleStruct &cur, std::string prefix) {
            if (dir_map_.find(cur) != dir_map_.end()) {
                for (auto &sub : dir_map_[cur]) {
                    print_map(sub, prefix + cur.name + "/");
                }
            } else {
                std::cout << prefix << cur.name << std::endl;
            }
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
}

void FATManager::DeleteSingleFile(const SimpleStruct &file) {
    // get sector entries
    std::vector<uint32_t> cluster_entries;
    ForEveryClusterOfFile(file, [this, &cluster_entries](uint32_t cluster) {
        cluster_entries.push_back(cluster);
    });

    // check
    for (auto it = cluster_entries.begin(); it != cluster_entries.end(); it++) {
        if (it != cluster_entries.end() - 1) {
            auto next = it + 1;
            ASSERT_EQ(*next, this->fat_map_->lookup(*it));
        } else if (it == cluster_entries.end() - 1) {
            auto entry = this->fat_map_->lookup(*it);
            ASSERT(IsEndOfFile(entry));
        }
    }

    std::cout << "check passed" << std::endl;

    for (auto cluster : cluster_entries) {
        this->fat_map_->setFree(cluster);
    }
}

void FATManager::RemoveEntryInDir(const SimpleStruct &dir,
                                  const SimpleStruct &entry) {
    UNIMPLEMENTED();
}

void FATManager::CopyFileFrom(const std::string &path,
                              const std::string &dest) {
    auto file = FindFile(path);
    if (!file) {
        std::cerr << "file " << path << " not found" << std::endl;
        std::exit(1);
    }
}

} // namespace cs5250