#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stddef.h>

int main(int argc, char* argv[]){
    if (argc != 2) {
        printf("Usage: %s <fs_image>\n", argv[0]);
        return 1;
    }

    FILE* fs_image = fopen(argv[1], "r+");
    if (!fs_image) {
        perror("Failed to open file system image");
        return 1;
    }

    int inode_bitmap_idx = 13;

    uint8_t inode_bitmap[4096];
    fseek(fs_image, 1 * 4096, SEEK_SET);
    if (fread(inode_bitmap, 4096, 1, fs_image) != 1) {
        perror("Failed to read inode bitmap");
        return 1;
    }

    int byte = inode_bitmap_idx / 8;
    int bit = inode_bitmap_idx % 8;

    inode_bitmap[byte] &= ~(1 << bit);

    fseek(fs_image, 1 * 4096, SEEK_SET);
    fwrite(inode_bitmap, 4096, 1, fs_image);
    fclose(fs_image);

    return 0;
}