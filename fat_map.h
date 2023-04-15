#pragma once

#include <cstdint>
#include <iostream>

namespace cs5250 {

class FATMap {
  private:
    int size;
    uint32_t *cluster_start;

  public:
    FATMap(int size, uint32_t *cluster_start)
        : size(size), cluster_start(cluster_start) {
          std::cout << "FATMap size " << size << std::endl;
        }

    uint32_t lookup(uint32_t cluster_number) {
        if (cluster_number < 0 || cluster_number >= size) {
            std::cerr << "cluster number out of range" << std::endl;
            return 0;
        }
        return cluster_start[cluster_number] & 0x0FFFFFFF;
    }

    void setFree(uint32_t cluster_number) {
        if (cluster_number < 0 || cluster_number >= size) {
            std::cerr << "cluster number out of range" << std::endl;
            return;
        }

        std::cout << "set free " << cluster_number << std::endl;
        cluster_start[cluster_number] = 0;
    }

    void set(uint32_t cluster_number, uint32_t next_cluster) {
        if (cluster_number < 0 || cluster_number >= size) {
            std::cerr << "cluster number out of range" << std::endl;
            return;
        }
        cluster_start[cluster_number] = next_cluster;
    }
};

} // namespace cs5250