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

        // try to print the first 10 entries of the FAT table
        for (int i = 0; i < 10; i++) {
            std::cout << "FAT[" << i << "] = " << std::hex << cluster_start[i] << std::endl;
        }
    }
};

} // namespace cs5250