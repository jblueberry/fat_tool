#include "fat.h"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>

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

template <typename T>
concept StringConvertible = std::convertible_to<T, std::string>;

class FATManager {
  private:
    const std::string file_path;
    const uint8_t *image = nullptr;
    off_t image_size = 0;
    uint32_t root_cluster = 0;

    struct FSInfo {
        uint32_t lead_sig;
        uint8_t reserved1[480];
        uint32_t struct_sig;
        uint32_t free_count;
        uint32_t next_free;
        uint8_t reserved2[12];
        uint32_t trail_sig;
    } __attribute__((packed));

    struct FileName {
        char name[8];
        char ext[3];
    } __attribute__((packed));

    struct FATDirectory {
        FileName DIR_Name;
        uint8_t DIR_Attr;
        uint8_t DIR_NTRes;
        uint8_t DIR_CrtTimeTenth;
        uint16_t DIR_CrtTime;
        uint16_t DIR_CrtDate;
        uint16_t DIR_LstAccDate;
        uint16_t DIR_FstClusHI;
        uint16_t DIR_WrtTime;
        uint16_t DIR_WrtDate;
        uint16_t DIR_FstClusLO;
        uint32_t DIR_FileSize;
    } __attribute__((packed));

    static_assert(sizeof(FSInfo) == 512);

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

    const std::string info() const {
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
        ret += "Reserved sectors: " + std::to_string(reserved_sector_count) + "\n";
        ret += "FATs sectors: " + std::to_string(fat_sector_count) + "\n";
        ret += "Root dir sectors: " + std::to_string(root_dir_sector_count) + "\n";
        ret += "Data sectors: " + std::to_string(data_sector_count) + "\n";
        return std::move(ret);
    }

    void ls() {
        ASSERT(fat_type == FATType::FAT32);
        // list all the files in the root directory
        auto root_dir = this->root_cluster;

        while (true) {
            auto fat_entry = clusterNumberToFATEntryValue(root_dir);
            auto end = endOfFile(fat_entry);
            auto first_sector_number = dataClusterNumberToFirstSectorNumber(root_dir);
            auto dirs_per_sector = bytes_per_sector / sizeof(FATDirectory);

            for(auto i = 0; i < sectors_per_cluster; ++i) {
                auto sector = first_sector_number + i;
                auto sector_start = getSectorStart(sector);

                for(auto j = 0; j < dirs_per_sector; ++j) {
                    auto dir = reinterpret_cast<const FATDirectory *>(sector_start + j * sizeof(FATDirectory));
                    std::string main_name(dir->DIR_Name.name, 8);
                    std::string ext(dir->DIR_Name.ext, 3);
                    std::cout << main_name << "." << ext << std::endl;
                }
            }

            if (end) {
                break;
            }
        }
    }

  private:
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

    inline const uint8_t *FSInfo() const {
        ASSERT(this->fat_type == FATType::FAT32);
        return getSectorStart(1);
    }

    inline uint32_t maximumValidClusterNumber() const {
        return count_of_clusters + 1;
    }

    inline void initBPB(const BPB &bpb) {
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
            total_sector_count - (bpb.BPB_RsvdSecCnt + (bpb.BPB_NumFATs * fat_size) +
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
    }

    inline uint32_t dataClusterNumberToFirstSectorNumber(uint32_t cluster_number) {
        ASSERT(cluster_number >= 2);
        ASSERT(cluster_number <= maximumValidClusterNumber());

        auto first_data_sector = reserved_sector_count + (number_of_fats * fat_size);

        return (cluster_number - 2) * sectors_per_cluster + first_data_sector;
    }

    inline bool endOfFile(uint32_t fat_entry_value) const {
        ASSERT(fat_type == FATType::FAT32);
        return fat_entry_value >= 0x0FFFFFF8;
    }
};

} // namespace cs5250