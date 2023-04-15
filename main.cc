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
            fprintf(stderr,
                    "Usage: %s %s %s local:[path] image:[path] or %s %s %s "
                    "image:[path] local:[path]\n",
                    argv[0], argv[1], argv[2], argv[0], argv[1], argv[2]);
            exit(1);
        }
        // argv[3] or argv[4] should be in the format of "image:/path/to/file"
        // and "local:/path/to/file" try to read the first 6 characters
        auto src = std::string(argv[3]);
        auto dst = std::string(argv[4]);

        if (src.substr(0, 6) == "image:" && dst.substr(0, 6) == "local:") {
            mgr.CopyFileTo(src.substr(6), dst.substr(6));
        } else if (src.substr(0, 6) == "local:" &&
                   dst.substr(0, 6) == "image:") {
            mgr.CopyFileFrom(src.substr(6), dst.substr(6));
        } else {
            fprintf(stderr,
                    "Usage: %s %s %s local:[path] image:[path] or %s %s %s "
                    "image:[path] local:[path]\n",
                    argv[0], argv[1], argv[2], argv[0], argv[1], argv[2]);
            exit(1);
        }

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
