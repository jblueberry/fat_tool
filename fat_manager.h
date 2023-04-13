#pragma once

#include "fat.h"
#include "fat_cluster.h"
#include <cassert>
#include <cstdint>
#include <fcntl.h>
#include <memory>
#include <string>
#include <sys/mman.h>
#include <unordered_map>
#include <vector>
#include <optional>

#define ASSERT(x)       assert(x)
#define ASSERT_EQ(x, y) assert((x) == (y))
#define REM(x, y)       ((x) % (y))

namespace cs5250 {

template <typename T, typename... Args>
bool isOneOf(const T &input, Args... args) {
    return ((input == args) || ...);
}

template <size_t N> bool allZero(const uint8_t (&arr)[N]) {
    for (size_t i = 0; i < N; ++i) {
        if (arr[i] != 0) {
            return false;
        }
    }
    return true;
}

template <typename E>
constexpr auto to_integral(E e) -> typename std::underlying_type<E>::type {
    return static_cast<typename std::underlying_type<E>::type>(e);
}

template <typename T>
concept StringConvertible = std::convertible_to<T, std::string>;

template <typename T>
using OptionalRef = std::optional<std::reference_wrapper<T>>;

class FATManager {
  private:
    const std::string file_path;
    const uint8_t *image = nullptr;
    off_t image_size = 0;
    uint32_t root_cluster = 0;
    std::unique_ptr<FATMap> fat_map;
    std::unordered_map<SimpleStruct, std::vector<SimpleStruct>> dir_map;

  protected:
    enum class FATType { FAT12, FAT16, FAT32 };
    FATType fat_type;
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint32_t reserved_sector_count;
    uint32_t fat_sector_count;
    uint32_t root_dir_sector_count;
    uint32_t data_sector_count;
    uint32_t fat_size;
    uint32_t count_of_clusters;
    uint8_t number_of_fats;

  public:
    template <StringConvertible T>
    FATManager(T &&file_path) : file_path(std::forward<T>(file_path)) {
        auto diskimg = file_path.c_str();
        int fd = open(diskimg, O_RDONLY);
        if (fd < 0) {
            perror("open");
            exit(1);
        }
        // get file length
        off_t size = lseek(fd, 0, SEEK_END);
        if (size == -1) {
            perror("lseek");
            exit(1);
        }
        this->image_size = size;
        image = static_cast<uint8_t *>(
            mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0));
        if (image == (void *)-1) {
            perror("mmap");
            exit(1);
        }
        close(fd);

        auto hdr = reinterpret_cast<const struct BPB *>(image);
        initBPB(*hdr);
    }

    ~FATManager() {
        if (image != nullptr) {
            munmap((void *)image, image_size);
        }
    }

    void ls();

    void ck();

    size_t copyFileTo(const std::string &path, const std::string &dest);

  private:
    OptionalRef<SimpleStruct> findFile(const std::string &path);

    inline const std::string info() const {
        std::string ret;
        ret += "FAT type: ";
        switch (fat_type) {
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
        ret += "\n";
        ret += "Bytes per sector: " + std::to_string(bytes_per_sector) + "\n";
        ret += "Sectors per cluster: " + std::to_string(sectors_per_cluster) +
               "\n";
        ret +=
            "Reserved sectors: " + std::to_string(reserved_sector_count) + "\n";
        ret += "FATs sectors: " + std::to_string(fat_sector_count) + "\n";
        ret +=
            "Root dir sectors: " + std::to_string(root_dir_sector_count) + "\n";
        ret += "Data sectors: " + std::to_string(data_sector_count);
        return ret;
    }

    inline uint32_t
    clusterNumberToFATEntryValue(uint32_t cluster_number) const {
        ASSERT(this->fat_type == FATType::FAT32);

        auto fat_offset = cluster_number * 4;
        auto fat_sector_number =
            reserved_sector_count + (fat_offset / bytes_per_sector);
        auto fat_entry_offset = fat_offset % bytes_per_sector;

        auto fat_sector = getSectorStart(fat_sector_number);
        auto fat_entry = reinterpret_cast<const uint32_t *>(fat_sector);

        return fat_entry[fat_entry_offset / 4] & 0x0FFFFFFF;
    }

    inline const uint8_t *getSectorStart(uint32_t sector_number) const {
        return image + (sector_number * bytes_per_sector);
    }

    inline uint32_t maximumValidClusterNumber() const {
        return count_of_clusters + 1;
    }

    void initBPB(const BPB &bpb);

    inline uint32_t
    dataClusterNumberToFirstSectorNumber(uint32_t cluster_number) {
        ASSERT(cluster_number >= 2);
        ASSERT(cluster_number <= maximumValidClusterNumber());

        auto first_data_sector =
            reserved_sector_count + (number_of_fats * fat_size);

        return (cluster_number - 2) * sectors_per_cluster + first_data_sector;
    }

    inline bool endOfFile(uint32_t fat_entry_value) const {
        ASSERT(fat_type == FATType::FAT32);
        return fat_entry_value >= 0x0FFFFFF8;
    }
};

} // namespace cs5250
