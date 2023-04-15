#include "fat_manager.h"
#include <fcntl.h>
#include <iostream>
#include <linux/limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    setbuf(stdout, NULL);
    if (argc < 3) {
        fprintf(stderr, "Usage: %s [path] [command]\n", argv[0]);
        exit(1);
    }
    const char *diskimg = argv[1];

    using cs5250::FATManager;
    auto file_path = std::string(diskimg);
    FATManager mgr{file_path};

    auto command = std::string(argv[2]);

    if (command == "ck") {
        mgr.Ck();
    } else if (command == "ls") {
        mgr.Ls();
    } else if (command == "cp") {
        if (argc < 5) {
            fprintf(stderr, "Usage: %s %s %s [srouce] [destination]\n", argv[0],
                    argv[1], argv[2]);
            exit(1);
        }
        auto src = std::string(argv[3]);
        auto dst = std::string(argv[4]);
        mgr.CopyFileTo(src, dst);
    } else if (command == "rm") {
        if (argc < 4) {
            fprintf(stderr, "Usage: %s %s %s [path]\n", argv[0], argv[1],
                    argv[2]);
            exit(1);
        }
        auto path = std::string(argv[3]);
        mgr.Delete(path);
    } else {
        std::cerr << "Unknown command: " << command << std::endl;
        exit(1);
    }
}
