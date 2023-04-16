#pragma once

#include "fat.h"
#include <cstdint>

namespace cs5250 {
class FSInfoManager {
  private:
    FSInfo *fs_info_;

  public:
    FSInfoManager(uint8_t *data) : fs_info_(reinterpret_cast<FSInfo *>(data)) {}

    uint32_t GetFreeClusterCount() { return fs_info_->FSI_Free_Count; }

    void SetFreeClusterCount(uint32_t count) {
        std::cout << "set free cluster count from " << fs_info_->FSI_Free_Count
                  << " to " << count << std::endl;
        fs_info_->FSI_Free_Count = count;
    }

    uint32_t GetNextFreeCluster() { return fs_info_->FSI_Nxt_Free; }

    void SetNextFreeCluster(uint32_t cluster) {
        fs_info_->FSI_Nxt_Free = cluster;
    }
};
} // namespace cs5250