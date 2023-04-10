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
    }

    uint32_t get(int cluster_number) {
        if (cluster_number < 0 || cluster_number >= size) {
            std::cout << "cluster number out of range" << std::endl;
            return 0;
        }
        return cluster_start[cluster_number];
    }
};

} // namespace cs5250