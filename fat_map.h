#pragma once

#include <cassert>
#include <cstdint>
#include <iostream>

#define ASSERT(x) assert(x)

namespace cs5250 {

class FATMap {
  private:
    uint8_t fat_num_;
    uint32_t size_;
    std::vector<uint32_t *> cluster_starts_;

  public:
    FATMap(uint8_t fat_num, uint32_t size,
           std::vector<uint32_t *> &&cluster_starts)
        : fat_num_(fat_num), size_(size), cluster_starts_(cluster_starts) {}

    uint32_t Lookup(uint32_t cluster_number) {
        if (cluster_number < 0 || cluster_number >= size_) {
            std::cerr << "cluster number out of range" << std::endl;
            return 0;
        }
        return cluster_starts_[0][cluster_number] & 0x0FFFFFFF;
    }

    void SetFree(uint32_t cluster_number) {
        if (cluster_number < 0 || cluster_number >= size_) {
            std::cerr << "cluster number out of range" << std::endl;
            return;
        }

        for (auto &cluster_start : cluster_starts_)
            cluster_start[cluster_number] = 0;
    }

    void Set(uint32_t cluster_number, uint32_t next_cluster) {
        if (cluster_number < 0 || cluster_number >= size_) {
            std::cerr << "cluster number out of range" << std::endl;
            return;
        }

        for (auto &cluster_start : cluster_starts_) {
            ASSERT(cluster_start[cluster_number] == 0);
            cluster_start[cluster_number] = next_cluster;
        }
    }

    std::optional<std::vector<uint32_t>> FindFree(uint32_t num) {
        std::vector<uint32_t> free_clusters;
        for (uint32_t i = 0; i < size_; i++) {
            if (Lookup(i) == 0) {
                free_clusters.push_back(i);
                if (free_clusters.size() == num)
                    return free_clusters;
            }
        }
        return std::nullopt;
    }
};

} // namespace cs5250