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
    std::vector<cs5250::SimpleStruct> *current_dir = &dir_map_[root_dir_];

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
        for (auto &dir : *current_dir) {
            if (dir.name == name && dir.is_dir) {
                return dir;
            }
        }
        return std::nullopt;
    };

    auto find_file = [&](std::string name) -> OptionalRef<SimpleStruct> {
        for (auto &dir : *current_dir) {
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
                current_dir = &dir_map_[*dir];
            } else {
                return std::nullopt;
            }
        }
    }

    return std::nullopt;
}

OptionalRef<SimpleStruct> FATManager::FindParentDir(const std::string &path) {

    auto current_dir = &root_dir_;

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
        // std::cout << "find dir for " << name << std::endl;
        // std::cout << "current dir is " << current_dir.name << " is it root? "
        //           << (current_dir.first_cluster ==
        //           this->root_cluster_number_)
        //           << std::endl;
        for (auto &dir : dir_map_[*current_dir]) {
            if (dir.name == name && dir.is_dir) {
                return dir;
            }
        }
        return std::nullopt;
    };

    for (auto &p : path_list) {
        // std::cout << "now current dir is " << current_dir.name << std::endl;
        if (p == path_list.back()) {
            return *current_dir;
        } else {
            auto dir = find_dir(p);
            if (dir) {
                current_dir = &(dir->get());
                // std::cout << "current dir is now " << current_dir.name
                //           << std::endl;
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

        // get the parent dir of the file
        auto parent_dir_op = FindParentDir(dest);

        if (!parent_dir_op) {
            std::cerr << "parent dir not found" << std::endl;
            close(c_file_fd);
            std::exit(1);
        }

        auto &&parent_dir = parent_dir_op.value().get();

        std::cout << "the parent dir is " << parent_dir.name << std::endl;

        auto get_file_name = [](const std::string &path) {
            auto pos = path.find_last_of('/');
            if (pos == std::string::npos) {
                return path;
            } else {
                return path.substr(pos + 1);
            }
        };

        auto file_name = get_file_name(dest);

        if (size == 0) {
            auto empty_file = SimpleStruct{file_name, 0, false};
            WriteFileToDir(parent_dir, empty_file, 0);
            close(c_file_fd);
            return;
        }

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

        std::vector<uint32_t> clusters_claimed;
        for (auto i = 0; i < cluster_count_needed; i++) {
            auto next_free_cluster =
                this->fs_info_manager_->GetNextFreeCluster();
            this->fs_info_manager_->SetNextFreeCluster(
                this->fat_map_->FindFree());
            clusters_claimed.push_back(next_free_cluster);
        }

        this->fs_info_manager_->SetFreeClusterCount(
            this->fs_info_manager_->GetFreeClusterCount() -
            cluster_count_needed);

        // set the chain of clusters
        for (auto i = 0; i < cluster_count_needed - 1; i++) {
            this->fat_map_->Set(clusters_claimed[i], clusters_claimed[i + 1]);
        }
        this->fat_map_->Set(clusters_claimed[cluster_count_needed - 1],
                            0x0FFFFFFF);

        char buffer[bytes_per_sector_];
        auto size_read_totally = 0;

        // clean the cluster allocated, all 0
        for (auto i = 0; i < cluster_count_needed; i++) {
            auto first_sector =
                FirstSectorNumberOfDataCluster(clusters_claimed[i]);
            for (auto j = 0; j < sector_count_per_cluster; j++) {
                auto data = StartAddressOfSector(first_sector + j);
                memset((void *)data, 0, bytes_per_sector_);
            }
        }

        // write the file to the cluster allocated
        for (auto i = 0; i < cluster_count_needed; i++) {
            // if not the last cluster, write the whole cluster
            if (i != cluster_count_needed - 1) {
                auto first_sector =
                    FirstSectorNumberOfDataCluster(clusters_claimed[i]);
                for (auto j = 0; j < sector_count_per_cluster; j++) {
                    auto data = StartAddressOfSector(first_sector + j);
                    auto size_read = read(c_file_fd, buffer, bytes_per_sector_);
                    if (size_read == -1) {
                        std::cerr << "failed to read file" << std::endl;
                        close(c_file_fd);
                        std::exit(1);
                    }

                    ASSERT(size_read == bytes_per_sector_);

                    size_read_totally += size_read;
                    memmove((void *)data, buffer, bytes_per_sector_);
                }
            } else {
                auto first_sector =
                    FirstSectorNumberOfDataCluster(clusters_claimed[i]);
                // if the last cluster, write the remaining bytes
                for (auto j = 0; j < sector_count_per_cluster; j++) {
                    auto size_read = read(c_file_fd, buffer, bytes_per_sector_);
                    if (size_read == -1) {
                        std::cerr << "failed to read file" << std::endl;
                        close(c_file_fd);
                        std::exit(1);
                    }
                    if (size_read == 0) {
                        break;
                    }
                    size_read_totally += size_read;

                    auto data = StartAddressOfSector(first_sector + j);
                    memmove((void *)data, buffer, size_read_totally);
                }
            }
        }

        auto created_file = SimpleStruct{file_name, clusters_claimed[0], false};
        WriteFileToDir(parent_dir, created_file, size);

        // close the file
        close(c_file_fd);
    }
}

inline void FATManager::WriteFileToDir(const SimpleStruct &dir,
                                       const SimpleStruct &file,
                                       uint32_t size) {
    auto long_name_entries = LongNameEntriesOfName(file.name);
    {
        std::string name = "";
        for (auto &entry : long_name_entries) {
            auto [name_part, _] = NameOfLongNameEntry(entry);
            name = name_part + name;
        }
        ASSERT_EQ(name, file.name);
    }
    auto dir_entry = FATDirectory();
    memset(&dir_entry.DIR_Name, 0, sizeof(dir_entry.DIR_Name));
    dir_entry.DIR_NTRes = 0;
    dir_entry.DIR_Attr = 0;

    dir_entry.DIR_CrtTimeTenth = 0;
    dir_entry.DIR_CrtTime = 0;
    dir_entry.DIR_CrtDate = 0;

    dir_entry.DIR_LstAccDate = 0;
    dir_entry.DIR_FstClusHI = file.first_cluster >> 16;
    dir_entry.DIR_WrtTime = 0;
    dir_entry.DIR_WrtDate = 0;
    dir_entry.DIR_FstClusLO = file.first_cluster & 0xffff;
    dir_entry.DIR_FileSize = size;

    // write this entries to the dir data block
    // first, find the first empty entry
    // auto first_empty_entry = FindFirstEmptyEntry(dir);
}

} // namespace cs5250