#pragma once
#include <optional>
#include <stdint.h>
#include <string>
#include <vector>

namespace cs5250 {

static char UnicodeToAscii(uint16_t unicode) {
    if (unicode >= 0x20 && unicode <= 0x7e) {
        return static_cast<char>(unicode);
    }
    return '?';
}

struct Extended16 {
    uint8_t BS_DrvNum;     /* offset 36 */
    uint8_t BS_Reserved1;  /* offset 37 */
    uint8_t BS_BootSig;    /* offset 38 */
    uint8_t BS_VolID[4];   /* offset 39 */
    uint8_t BS_VolLab[11]; /* offset 43 */
    char BS_FilSysType[8]; /* offset 54 */
    uint8_t _[448];
} __attribute__((packed));

struct Extended32 {
    uint32_t BPB_FATSz32;     /* offset 36 */
    uint16_t BPB_ExtFlags;    /* offset 40 */
    uint8_t BPB_FSVer[2];     /* offset 42 */
    uint32_t BPB_RootClus;    /* offset 44 */
    uint16_t BPB_FSInfo;      /* offset 48 */
    uint16_t BPB_BkBootSec;   /* offset 50 */
    uint8_t BPB_Reserved[12]; /* offset 52 */
    uint8_t BS_DrvNum;        /* offset 64 */
    uint8_t BS_Reserved1;     /* offset 65 */
    uint8_t BS_BootSig;       /* offset 66 */
    uint8_t BS_VolID[4];      /* offset 67 */
    uint8_t BS_VolLab[11];    /* offset 71 */
    uint8_t BS_FilSysType[8]; /* offset 82 */
    uint8_t _[420];
} __attribute__((packed));

struct BPB {
    uint8_t BS_jmpBoot[3];   /* offset 0 */
    uint8_t BS_OEMName[8];   /* offset 3 */
    uint16_t BPB_BytsPerSec; /* offset 11 */
    uint8_t BPB_SecPerClus;  /* offset 13 */
    uint16_t BPB_RsvdSecCnt; /* offset 14 */
    uint8_t BPB_NumFATs;     /* offset 16 */
    uint16_t BPB_RootEntCnt; /* offset 17 */
    uint16_t BPB_TotSec16;   /* offset 19 */
    uint8_t BPB_Media;       /* offset 21 */
    uint16_t BPB_FATSz16;    /* offset 22 */
    uint16_t BPB_SecPerTrk;  /* offset 24 */
    uint16_t BPB_NumHeads;   /* offset 26 */
    uint32_t BPB_HiddSec;    /* offset 28 */
    uint32_t BPB_TotSec32;   /* offset 32 */
    union {
        // Extended BPB structure for FAT12 and FAT16 volumes
        Extended16 fat16;
        // Extended BPB structure for FAT32 volumes
        Extended32 fat32;
    };
    uint16_t Signature_word; /* offset 510 */
} __attribute__((packed));

static_assert(sizeof(BPB) == 512, "BPB size is not 512 bytes");

/*
 * File System Information (FSInfo) Structure
 * RTFM: Section 5
 */
struct FSInfo {
    uint32_t FSI_LeadSig;       /* offset 0 */
    uint8_t FSI_Reserved1[480]; /* offset 4 */
    uint32_t FSI_StrucSig;      /* offset 484 */
    uint32_t FSI_Free_Count;    /* offset 488 */
    uint32_t FSI_Nxt_Free;      /* offset 492 */
    uint8_t FSI_Reserved2[12];  /* offset 496 */
    uint32_t FSI_TrailSig;      /* offset 508 */
} __attribute__((packed));

struct FileName {
    uint8_t name[8];
    uint8_t ext[3];
} __attribute__((packed));

struct FATDirectory {

    enum class Attr : uint8_t {
        ReadOnly = 0x01,
        Hidden = 0x02,
        System = 0x04,
        VolumeID = 0x08,
        Directory = 0x10,
        Archive = 0x20,
        LongName = ReadOnly | Hidden | System | VolumeID,
        LongNameMask =
            ReadOnly | Hidden | System | VolumeID | Directory | Archive,
    };

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

  public:
    static const std::string AttributeTypeToString(Attr attr) {
        switch (attr) {
        case Attr::ReadOnly:
            return "Read Only";
        case Attr::Hidden:
            return "Hidden";
        case Attr::System:
            return "System";
        case Attr::VolumeID:
            return "Volume ID";
        case Attr::Directory:
            return "Directory";
        case Attr::Archive:
            return "Archive";
        case Attr::LongName:
            return "Long Name";
        default:
            return "Unknown";
        }
    }
} __attribute__((packed));

struct LongNameDirectory {

    struct UnicodeChar {
        uint8_t low;
        uint8_t high;
    } __attribute__((packed));

    template <size_t N> struct Name {
        UnicodeChar values[N];
    } __attribute__((packed));

    template <size_t N>
    static const std::pair<std::string, bool> GetName(const Name<N> &name) {
        std::string str = "";
        for (size_t i = 0; i < N; ++i) {
            if (name.values[i].low == 0)
                return {str, true};
            str += UnicodeToAscii(
                reinterpret_cast<const uint16_t &>(name.values[i]));
        }
        return {str, false};
    }

    uint8_t LDIR_Ord;
    Name<5> LDIR_Name1;
    uint8_t LDIR_Attr;
    uint8_t LDIR_Type;
    uint8_t LDIR_Chksum;
    Name<6> LDIR_Name2;
    uint16_t LDIR_FstClusLO;
    Name<2> LDIR_Name3;

} __attribute__((packed));

static_assert(sizeof(LongNameDirectory) == 32);

static_assert(sizeof(FATDirectory) == 32);

static_assert(sizeof(FSInfo) == 512);

struct SimpleStruct {
    std::string name;
    uint32_t first_cluster;
    bool is_dir;
    std::optional<std::vector<const LongNameDirectory *>> long_name_entries;

    operator std::string() const { return name; }

    bool operator==(const SimpleStruct &other) const {
        return name == other.name && first_cluster == other.first_cluster &&
               is_dir == other.is_dir;
    }
};

} // namespace cs5250

// define hash for LsStruct
namespace std {
template <> struct hash<cs5250::SimpleStruct> {
    std::size_t operator()(const cs5250::SimpleStruct &k) const {
        return std::hash<uint32_t>{}(k.first_cluster);
    }
};
} // namespace std