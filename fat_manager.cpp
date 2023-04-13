#include "fat_manager.h"
#include <deque>
#include <functional>
#include <iostream>
#include <optional>
#include <unordered_map>
#include <vector>

namespace cs5250 {

static std::string parseShortName(const char name[11]) {
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

void FATManager::ck() { std::cout << info() << std::endl; }

void FATManager::initBPB(const BPB &bpb) {
    auto root_dir_sector_count =
        ((bpb.BPB_RootEntCnt * 32) + (bpb.BPB_BytsPerSec - 1)) /
        bpb.BPB_BytsPerSec;

    auto fat_size = 0;

    if (bpb.BPB_FATSz16 != 0)
        fat_size = bpb.BPB_FATSz16;
    else
        fat_size = bpb.fat32.BPB_FATSz32;

    this->fat_size = fat_size;

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

    this->count_of_clusters = count_of_clusters;

    if (count_of_clusters < 4085) {
        fat_type = FATType::FAT12;
    } else if (count_of_clusters < 65525) {
        fat_type = FATType::FAT16;
    } else {
        fat_type = FATType::FAT32;
        this->root_cluster = bpb.fat32.BPB_RootClus;
        ASSERT_EQ(root_dir_sector_count, 0);
    }

    bytes_per_sector = bpb.BPB_BytsPerSec;
    sectors_per_cluster = bpb.BPB_SecPerClus;
    reserved_sector_count = bpb.BPB_RsvdSecCnt;
    fat_sector_count = bpb.BPB_NumFATs * fat_size;
    this->root_dir_sector_count = root_dir_sector_count;
    this->data_sector_count = data_sector_count;
    this->number_of_fats = bpb.BPB_NumFATs;

    auto fat_start_address =
        reinterpret_cast<uint32_t *>(reinterpret_cast<uint64_t>(image) +
                                     (bpb.BPB_RsvdSecCnt * bytes_per_sector));

    this->fat_map =
        std::make_unique<FATMap>(this->fat_size * 512 / 4, fat_start_address);

    auto parse_cluster = [this](auto this_cluster,
                                auto parent = -1) -> std::vector<SimpleStruct> {
        std::vector<SimpleStruct> ret;
        auto seen_long_name = false;
        std::string long_name = "";
        while (true) {
            auto fat_entry = clusterNumberToFATEntryValue(this_cluster);
            auto end = endOfFile(fat_entry);
            auto first_sector_number =
                dataClusterNumberToFirstSectorNumber(this_cluster);
            auto dirs_per_sector = bytes_per_sector / sizeof(FATDirectory);

            for (auto i = 0; i < sectors_per_cluster; ++i) {
                auto sector = first_sector_number + i;
                auto sector_start = getSectorStart(sector);
                auto sector_end = sector_start + bytes_per_sector;

                for (decltype(dirs_per_sector) j = 0; j < dirs_per_sector;
                     ++j) {
                    auto dir = reinterpret_cast<const FATDirectory *>(
                        sector_start + j * sizeof(FATDirectory));

                    if (dir->DIR_Attr ==
                        to_integral(FATDirectory::Attr::LongName)) {
                        if (!seen_long_name) {
                            seen_long_name = true;
                            long_name = "";
                        }
                        auto long_dir =
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
                        if (!seen_long_name) {
                            // extract short name from dir->DIR_Name[0: 8]
                            // extract extension from dir->DIR_Name[8: 11]
                            name = parseShortName(dir->DIR_Name.name);

                        } else {
                            name = std::move(long_name);
                            seen_long_name = false;
                        }

                        // check whether it is a file or a directory
                        bool is_dir = false;

                        // convert enum class member
                        // FATDirectory::Attr::Directory to uint8_t

                        if (dir->DIR_Attr ==
                            to_integral(FATDirectory::Attr::Directory)) {
                            // it is a directory
                            is_dir = true;
                        } else {
                            // it is a file
                            is_dir = false;
                        }

                        uint32_t cluster =
                            dir->DIR_FstClusLO | (dir->DIR_FstClusHI << 16);

                        if (cluster == this_cluster || cluster == parent ||
                            cluster == 0) {
                        } else
                            ret.push_back({name, cluster, is_dir});
                    }
                }
            }

            if (end) {
                break;
            } else {
                this_cluster = fat_entry;
            }
        }

        return ret;
    };

    auto dummy = SimpleStruct{"", this->root_cluster, true};

    std::deque<std::pair<SimpleStruct, int>> q;
    q.push_back({dummy, this->root_cluster});

    while (!q.empty()) {
        auto cur = q.front();
        q.pop_front();

        if (cur.first.is_dir) {
            auto sub_lists = parse_cluster(cur.first.cluster, cur.second);
            for (auto &list : sub_lists) {
                q.push_back({list, cur.first.cluster});
            }
            dir_map[cur.first] = std::move(sub_lists);
        }
    }
}

void FATManager::ls() {
    ASSERT(fat_type == FATType::FAT32);

    // recursively print the map
    std::function<void(const SimpleStruct &, std::string prefix)> print_map =
        [&](const SimpleStruct &cur, std::string prefix) {
            if (dir_map.find(cur) != dir_map.end()) {
                for (auto &sub : dir_map[cur]) {
                    print_map(sub, prefix + cur.name + "/");
                }
            } else {
                std::cout << prefix << cur.name << std::endl;
            }
        };

    auto dummy = SimpleStruct{"", this->root_cluster, true};

    print_map(dummy, "");
}

OptionalRef<SimpleStruct> FATManager::findFile(const std::string &path) {
    auto root = SimpleStruct{"", this->root_cluster, true};
    auto &current_dir = dir_map[root];

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
                current_dir = dir_map[*dir];
            } else {
                return std::nullopt;
            }
        }
    }

    return std::nullopt;
}

size_t FATManager::copyFileTo(const std::string &path,
                              const std::string &dest) {
    auto file = findFile(path);
    if (!file) {
        std::cerr << "file " << path << " not found" << std::endl;
        std::exit(1);
    }

    auto cluster = file->get().cluster;

    auto next_cluster_of = [this](uint32_t cluster) -> uint32_t {
        // look up the cluster in the FAT table
        auto fat_entry = this->getFATEntry(cluster);

        return 0;
    };

    
    std::cout << "cluster: " << cluster << std::endl;

    // get the first sector of the cluster
    auto first_sector = this->dataClusterNumberToFirstSectorNumber(cluster);
    for (auto i = 0; i < sectors_per_cluster; i++) {
        auto sector = first_sector + i;
        auto buf = this->getSectorStart(sector);
        std::cout << "sector: " << reinterpret_cast<const char *>(buf)
                  << std::endl;
    }

    return 0;
}

} // namespace cs5250