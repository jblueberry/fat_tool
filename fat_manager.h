#pragma once

#include "fat.h"
#include "fat_map.h"
#include "fsinfo_manager.h"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <optional>
#include <string>
#include <sys/mman.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <algorithm>

#define ASSERT(x)       assert(x)
#define ASSERT_EQ(x, y) assert((x) == (y))
#define REM(x, y)       ((x) % (y))

namespace cs5250 {

template <typename T, typename... Args>
bool IsOneOf(const T &input, Args... args) {
    return ((input == args) || ...);
}

template <size_t N> bool AllZero(const uint8_t (&arr)[N]) {
    for (size_t i = 0; i < N; ++i) {
        if (arr[i] != 0) {
            return false;
        }
    }
    return true;
}

template <typename E>
constexpr auto ToIntegral(E e) -> typename std::underlying_type<E>::type {
    return static_cast<typename std::underlying_type<E>::type>(e);
}

template <typename T>
concept StringConvertible = std::convertible_to<T, std::string>;

template <typename T>
using OptionalRef = std::optional<std::reference_wrapper<T>>;

class FATManager {
  private:
    const std::string file_path_;
    uint8_t *image_ = nullptr;
    off_t image_size_ = 0;
    uint32_t root_cluster_number_ = 0;
    std::unique_ptr<FATMap> fat_map_;
    std::unordered_map<SimpleStruct, std::vector<SimpleStruct>> dir_map_;
    SimpleStruct root_dir_;
    std::unique_ptr<FSInfoManager> fs_info_manager_;

    bool IsFreeDirEntry(const FATDirectory *dir) {
        return dir->DIR_Name.name[0] == 0x00;
    }

    bool IsDeletedDirEntry(const FATDirectory *dir) {
        return dir->DIR_Name.name[0] == 0xE5; // deleted
    }

    template <typename F>
    void ForEverySectorOfCluster(uint32_t cluster_number,
                                 F &&function_for_sector) {
        auto first_sector_number =
            FirstSectorNumberOfDataCluster(cluster_number);

        for (int i = 0; i < sectors_per_cluster_; ++i) {
            auto start_address = StartAddressOfSector(first_sector_number + i);
            function_for_sector(start_address);
        }
    }

    template <typename F>
    void ForEverySectorOfFile(const SimpleStruct &dir, F &&function) {

        auto cluster_number = dir.first_cluster;

        do {
            ForEverySectorOfCluster(cluster_number, function);
            cluster_number = fat_map_->Lookup(cluster_number);
        } while (!IsEndOfFile(cluster_number));
    }

    template <typename F>
    void ForEveryClusterOfFile(const SimpleStruct &dir, F &&function) {
        auto cluster_number = dir.first_cluster;

        do {
            function(cluster_number);
            cluster_number = fat_map_->Lookup(cluster_number);
        } while (!IsEndOfFile(cluster_number));
    }

    template <typename Func>
    void ForEveryDirEntryInDirSector(const uint8_t *data, Func &&func) {
        auto dirs_per_sector = bytes_per_sector_ / sizeof(FATDirectory);
        for (decltype(dirs_per_sector) index = 0; index < dirs_per_sector;
             ++index) {
            auto dir = reinterpret_cast<const FATDirectory *>(
                data + index * sizeof(FATDirectory));
            if (IsFreeDirEntry(dir)) {
                break;
            } else if (IsDeletedDirEntry(dir)) {
                continue;
            } else
                func(dir);
        }
    }

  protected:
    enum class FATType { FAT12, FAT16, FAT32 };
    FATType fat_type_;
    uint16_t bytes_per_sector_;
    uint8_t sectors_per_cluster_;
    uint32_t reserved_sector_count_;
    uint32_t fat_sector_count_;
    uint32_t root_dir_sector_count_;
    uint32_t data_sector_count_;
    uint32_t sector_count_per_fat_;
    uint32_t count_of_clusters_;
    uint8_t number_of_fats_;

  public:
    template <StringConvertible T>
    FATManager(T &&file_path) : file_path_(std::forward<T>(file_path)) {
        auto diskimg = file_path.c_str();
        // open the disk image as read-write
        int fd = open(diskimg, O_RDWR);
        if (fd < 0) {
            perror("open");
            exit(1);
        }
        off_t size = lseek(fd, 0, SEEK_END);
        if (size == -1) {
            perror("lseek");
            exit(1);
        }
        this->image_size_ = size;

        // mmap in READ-WRITE mode
        // image_ = static_cast<uint8_t *>(
        //     mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0));
        image_ =
            static_cast<uint8_t *>(mmap(NULL, size, O_RDWR, MAP_SHARED, fd, 0));
        if (image_ == (void *)-1) {
            perror("mmap");
            exit(1);
        }
        close(fd);

        auto hdr = reinterpret_cast<const struct BPB *>(image_);
        InitBPB(*hdr);
    }

    ~FATManager() {
        if (image_ != nullptr) {
            munmap((void *)image_, image_size_);
        }
    }

    void Ls();

    void Ck();

    void CopyFileTo(const std::string &path, const std::string &dest);

    void CopyFileFrom(const std::string &path, const std::string &dest);

    void Delete(const std::string &path);

  private:
    OptionalRef<SimpleStruct> FindFile(const std::string &path);

    std::optional<std::vector<std::reference_wrapper<SimpleStruct>>>
    FindFileWithDirs(const std::string &path);

    void DeleteSingleFile(const SimpleStruct &file);

    void DeleteSingleDir(const SimpleStruct &dir);

    void RemoveEntryInDir(const SimpleStruct &dir, const SimpleStruct &file);

    inline const std::string Info() const {
        std::string ret;
        switch (fat_type_) {
        case FATType::FAT12:
            ret += "FAT12";
            break;
        case FATType::FAT16:
            ret += "FAT16";
            break;
        case FATType::FAT32:
            ret += "FAT32";
            break;
        }
        ret += " filesystem\n";
        ret += "BytsPerSec = " + std::to_string(bytes_per_sector_) + "\n";
        ret += "SecPerClus = " + std::to_string(sectors_per_cluster_) + "\n";
        ret += "RsvdSecCnt = " + std::to_string(reserved_sector_count_) + "\n";
        ret += "FATsSecCnt = " + std::to_string(fat_sector_count_) + "\n";
        ret += "RootSecCnt = " + std::to_string(root_dir_sector_count_) + "\n";
        ret += "DataSecCnt = " + std::to_string(data_sector_count_);
        return ret;
    }

    inline uint8_t *StartAddressOfSector(uint32_t sector_number) const {
        return image_ + (sector_number * bytes_per_sector_);
    }

    inline uint32_t MaximumValidClusterNumber() const {
        return count_of_clusters_ + 1;
    }

    void InitBPB(const BPB &bpb);

    inline uint32_t FirstSectorNumberOfDataCluster(uint32_t cluster_number) {
        ASSERT(cluster_number >= 2);
        ASSERT(cluster_number <= MaximumValidClusterNumber());

        auto first_data_sector =
            reserved_sector_count_ + (number_of_fats_ * sector_count_per_fat_);

        return (cluster_number - 2) * sectors_per_cluster_ + first_data_sector;
    }

    inline bool IsEndOfFile(uint32_t fat_entry_value) const {
        ASSERT(fat_type_ == FATType::FAT32);
        return fat_entry_value >= 0x0FFFFFF8;
    }

    inline void DecreaseFreeClusterCount(uint32_t number) {
        this->fs_info_manager_->SetFreeClusterCount(
            this->fs_info_manager_->GetFreeClusterCount() - number);
    }

    inline void IncreaseFreeClusterCount(uint32_t number) {
        this->fs_info_manager_->SetFreeClusterCount(
            this->fs_info_manager_->GetFreeClusterCount() + number);
    }

    inline std::vector<uint32_t> ClustersOfFile(const SimpleStruct &file) {
        std::vector<uint32_t> cluster_entries;
        ForEveryClusterOfFile(file, [this, &cluster_entries](uint32_t cluster) {
            cluster_entries.push_back(cluster);
        });
        return cluster_entries;
    }

    inline void WriteFileToDir(const SimpleStruct &dir,
                               const SimpleStruct &file, uint32_t size);

    inline uint8_t CheckSumOfShortName(FATDirectory::ShortName *name) {
        uint8_t sum = 0;

        auto p_fcb_name = reinterpret_cast<uint8_t *>(name);

        for (auto i = 0; i < 11; ++i) {
            sum = ((sum & 1) ? 0x80 : 0) + (sum >> 1) + (*p_fcb_name++);
        }

        return sum;
    }
    inline std::vector<LongNameDirectory>
    LongNameEntriesOfName(const std::string &name) {
        std::vector<LongNameDirectory> long_name_entries;
        // return long_name_entries;
        auto name_length = name.length();
        auto long_name_entry_count = (name_length + 12) / 13;
        for (decltype(long_name_entry_count) i = 0; i < long_name_entry_count; ++i) {
            LongNameDirectory long_name_entry;
            long_name_entry.LDIR_Ord = i + 1;
            long_name_entry.LDIR_Attr = ToIntegral(FATDirectory::Attr::LongName);
            if (i == long_name_entry_count - 1) {
                long_name_entry.LDIR_Ord |= 0x40;
            }
            // create a short name with all 0s
            FATDirectory::ShortName short_name;
            memset(&short_name, 'a', sizeof(short_name));
            long_name_entry.LDIR_Chksum = CheckSumOfShortName(&short_name);

            long_name_entry.LDIR_FstClusLO = 0;
            long_name_entry.LDIR_Type = 0;

            auto start = i * 13;
            auto end = start + 13;
            if (end > name_length) {
                end = name_length;
            }

            // see how many characters we can copy

            auto char_number = end - start;
            // if there is 1-5 characters left, we only need to fill in the
            // LDIR_Name1

            if (char_number <= 5) {
                for (decltype(char_number) j = 0; j < 5; ++j) {
                    if (j >= char_number) {
                        long_name_entry.LDIR_Name1.values[j] =
                            LongNameDirectory::UnicodeChar(0);
                    } else {
                        long_name_entry.LDIR_Name1.values[j] =
                            LongNameDirectory::UnicodeChar(name[start + j]);
                    }
                }

                // set LDIR_Name2 and LDIR_Name3 to 0
                memset(&long_name_entry.LDIR_Name2, 0,
                       sizeof(long_name_entry.LDIR_Name2));
                memset(&long_name_entry.LDIR_Name3, 0,
                       sizeof(long_name_entry.LDIR_Name3));
                long_name_entries.push_back(long_name_entry);
                continue;
            }

            // if there is 6-11 characters left, we need to fill in the
            // LDIR_Name1 and LDIR_Name2

            if (char_number <= 11) {
                for (decltype(char_number) j = 0; j < 5; ++j) {
                    long_name_entry.LDIR_Name1.values[j] =
                        LongNameDirectory::UnicodeChar(name[start + j]);
                }
                for (decltype(char_number) j = 5; j < 11; ++j) {
                    if (j >= char_number) {
                        long_name_entry.LDIR_Name2.values[j - 5] =
                            LongNameDirectory::UnicodeChar(0);
                    } else {
                        long_name_entry.LDIR_Name2.values[j - 5] =
                            LongNameDirectory::UnicodeChar(name[start + j]);
                    }
                }

                memset(&long_name_entry.LDIR_Name3, 0,
                       sizeof(long_name_entry.LDIR_Name3));
                long_name_entries.push_back(long_name_entry);
                continue;
            }

            // if there is 12-13 characters left, we need to fill in the
            // LDIR_Name1, LDIR_Name2 and LDIR_Name3
            for (decltype(char_number) j = 0; j < 5; ++j) {
                long_name_entry.LDIR_Name1.values[j] =
                    LongNameDirectory::UnicodeChar(name[start + j]);
            }
            for (decltype(char_number) j = 5; j < 11; ++j) {
                long_name_entry.LDIR_Name2.values[j - 5] =
                    LongNameDirectory::UnicodeChar(name[start + j]);
            }
            for (decltype(char_number) j = 11; j < 13; ++j) {
                if (j >= char_number) {
                    long_name_entry.LDIR_Name3.values[j - 11] =
                        LongNameDirectory::UnicodeChar(0);
                } else {
                    long_name_entry.LDIR_Name3.values[j - 11] =
                        LongNameDirectory::UnicodeChar(name[start + j]);
                }
            }
            long_name_entries.push_back(long_name_entry);
        }

        // reverse the order of the long name entries
        std::reverse(long_name_entries.begin(), long_name_entries.end());
        return long_name_entries;
    }

    OptionalRef<SimpleStruct> FindParentDir(const std::string &path);
};

} // namespace cs5250
