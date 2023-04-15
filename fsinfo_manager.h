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
        fs_info_->FSI_Free_Count = count;
    }

    uint32_t GetNextFreeCluster() { return fs_info_->FSI_Nxt_Free; }
};
} // namespace cs5250