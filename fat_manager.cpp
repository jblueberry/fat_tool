#include "fat_manager.h"
#include <deque>
#include <functional>
#include <iostream>
#include <unordered_map>
#include <vector>

namespace cs5250 {

struct LsStruct {
    std::string name;
    uint32_t cluster;
    bool is_dir;

    operator std::string() const { return name; }

    bool operator==(const LsStruct &other) const {
        return name == other.name && cluster == other.cluster &&
               is_dir == other.is_dir;
    }
};
} // namespace cs5250

// define hash for LsStruct
namespace std {
template <> struct hash<cs5250::LsStruct> {
    std::size_t operator()(const cs5250::LsStruct &k) const {
        return std::hash<uint32_t>{}(k.cluster);
    }
};
} // namespace std

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

void FATManager::ls() {
    ASSERT(fat_type == FATType::FAT32);

    auto parse_cluster = [this](auto this_cluster,
                                auto parent = -1) -> std::vector<LsStruct> {
        std::vector<LsStruct> ret;
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

    std::unordered_map<LsStruct, std::vector<LsStruct>> map;
    auto dummy = LsStruct{"", this->root_cluster, true};

    std::deque<std::pair<LsStruct, int>> q;
    q.push_back({dummy, this->root_cluster});

    while (!q.empty()) {
        auto cur = q.front();
        q.pop_front();

        if (cur.first.is_dir) {
            auto sub_lists = parse_cluster(cur.first.cluster, cur.second);
            for (auto &list : sub_lists) {
                q.push_back({list, cur.first.cluster});
            }
            map[cur.first] = std::move(sub_lists);
        }
    }

    // recursively print the map
    std::function<void(const LsStruct &, std::string prefix)> print_map =
        [&](const LsStruct &cur, std::string prefix) {
            if (map.find(cur) != map.end()) {
                for (auto &sub : map[cur]) {
                    print_map(sub, prefix + cur.name + "/");
                }
            } else {
                std::cout << prefix << cur.name << std::endl;
            }
        };

    print_map(dummy, "");
}

void FATManager::ck() { std::cout << info() << std::endl; }
} // namespace cs5250