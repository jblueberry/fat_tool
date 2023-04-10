#include <fcntl.h>
#include <linux/limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include "fat_manager.h"
#include <iostream>

/*
 * Hexdump the given data.
 *
 * WARNING: THIS FUNCTION IS ONLY FOR DEBUGGING PURPOSES!
 * This function prints the data using an external command called "hexdump", but
 * your program should not depend on external programs. Before submitting, you
 * must remove this function. If you include this function in your submission,
 * you may face a penalty.
 */
// void hexdump(const void *data, size_t size) {
// #warning "You must remove this function before submitting."
//     FILE *proc;

//     proc = popen("hexdump -C", "w");
//     if (proc == NULL) {
//         perror("popen");
//         exit(1);
//     }
//     fwrite(data, 1, size, proc);
// }

int main(int argc, char *argv[]) {
    setbuf(stdout, NULL);
    if (argc < 3) {
        fprintf(stderr, "Usage: %s disk.img ck\n", argv[0]);
        exit(1);
    }
    const char *diskimg = argv[1];

    using cs5250::FATManager;
    auto file_path = std::string(diskimg);
    FATManager mgr{file_path};

    auto command = std::string(argv[2]);

    if (command == "ck") {
        mgr.ck();
    } else if (command == "ls") {
        mgr.ls();
    } else {
        std::cerr << "Unknown command: " << command << std::endl;
        exit(1);
    }

    /*
     * Print the contents of the first cluster.
     */
    // hexdump(image, sizeof(*hdr));
}
