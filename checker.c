#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stddef.h>

typedef struct {
    uint16_t magic;
    int block_size;
    int total_blocks;
    int inode_bitmap_block;
    int data_bitmap_block;
    int inode_table_start;
    int first_data_block;
    int inode_size;
    int inode_count;
    uint8_t reserved[4058];
} Superblock;

typedef struct {
    int mode;
    int user_id;
    int group_id;
    int file_size;
    int lastacc_time;
    int creat_time;
    int lastmod_time;
    int del_time;
    int hard_links;
    int no_blocks;
    int direct[12];
    int indirect;
    int double_indirect;
    int triple_indirect;
    uint8_t reserved[156];
} Inode;

void print_superblock(const Superblock* sb) {
    printf("╔════════════════════════════════════════╗\n");
    printf("║            SUPERBLOCK INFO             ║\n");
    printf("╠═══════════════════════╦════════════════╣\n");
    printf("║ Magic number          ║ 0x%04X         ║\n", sb->magic);
    printf("║ Block size            ║ %-9ubytes ║\n", sb->block_size);
    printf("║ Total blocks          ║ %-15d║\n", sb->total_blocks);
    printf("║ Inode bitmap block    ║ %-15d║\n", sb->inode_bitmap_block);
    printf("║ Data bitmap block     ║ %-15d║\n", sb->data_bitmap_block);
    printf("║ Inode table start     ║ %-15d║\n", sb->inode_table_start);
    printf("║ First data block      ║ %-15d║\n", sb->first_data_block);
    printf("║ Inode size            ║ %-9dbytes ║\n", sb->inode_size);
    printf("║ Inode count           ║ %-15d║\n", sb->inode_count);
    printf("╚═══════════════════════╩════════════════╝\n");
}

int check_superblock(FILE* fs_image) {
    Superblock sb;

    fseek(fs_image, 0, SEEK_SET);
    fread(&sb, sizeof(Superblock), 1, fs_image);
    
    print_superblock(&sb);

    if (sb.magic != 0xD34D) {
        printf("Error: Invalid magic number %04X (expected D34D)\n", sb.magic);
    } else if (sb.block_size != 4096) {
        printf("Error: Invalid block size %u (expected 4096)\n", sb.block_size);
    } else if (sb.total_blocks != 64) {
        printf("Error: Invalid total blocks %u (expected 64)\n", sb.total_blocks);
    } else if (sb.inode_bitmap_block != 1) {
        printf("Error: Invalid inode bitmap block %u (expected 1)\n", sb.inode_bitmap_block);
    } else if (sb.data_bitmap_block != 2) {
        printf("Error: Invalid data bitmap block %u (expected 2)\n", sb.data_bitmap_block);
    } else if (sb.inode_table_start != 3) {
        printf("Error: Invalid inode table start %u (expected 3)\n", sb.inode_table_start);
    } else if (sb.first_data_block != 8) {
        printf("Error: Invalid first data block %u (expected 8)\n", sb.first_data_block);
    } else if (sb.inode_size != 256) {
        printf("Error: Invalid inode size %u (expected 256)\n", sb.inode_size);
    } else {
        printf("No error on superblock.\n");
        return 0;
    }

    return -1;
}

void check_inode_bitmap_consistency(FILE* fs_image, Inode** inodes, const Superblock* sb) {
    int errors_found = 0;
    
    uint8_t inode_bitmap[4096];
    fseek(fs_image, sb->inode_bitmap_block * sb->block_size, SEEK_SET);
    if (fread(inode_bitmap, sb->block_size, 1, fs_image) != 1) {
        perror("Failed to read inode bitmap");
        return;
    }

    printf("\nChecking inode bitmap consistency...\n");
    printf("╔════════════════════════════════════════╗\n");
    printf("║       INODE BITMAP CONSISTENCY CHECK   ║\n");
    printf("╠════════════╦════════════╦══════════════╣\n");
    printf("║ Inode      ║ Bitmap     ║ Status       ║\n");
    printf("╠════════════╬════════════╬══════════════╣\n");

    for (int i = 0; i < sb->inode_count; i++) {
        int byte = i / 8;
        int bit = i % 8;
        int bitmap_set = inode_bitmap[byte] & (1 << bit);
        int should_be_set = (inodes[i]->hard_links > 0) && (inodes[i]->del_time == 0);

        if (bitmap_set && !should_be_set) {
            printf("║ %-10d ║ allocated  ║ ERROR: Inode has links=%d dtime=%d ║\n",
                   i, inodes[i]->hard_links, inodes[i]->del_time);
            errors_found++;
        } else if (!bitmap_set && should_be_set) {
            printf("║ %-10d ║ free       ║ ERROR: Inode should be allocated (links=%d) ║\n",
                   i, inodes[i]->hard_links);
            errors_found++;
        } else {
            printf("║ %-10d ║ %-9s  ║ OK           ║\n",
                   i, bitmap_set ? "allocated" : "free");
        } 
    }

    printf("╚════════════╩════════════╩══════════════╝\n");
    printf("Found %d inode bitmap inconsistencies\n", errors_found);
}

int check_block_reference(const Inode* inode, const uint8_t* data_bitmap, int* block_refs,
                         int first_data_block, int total_blocks, int inode_num, int block) {
    if (block == 0) {
        return 0;
    }

    if (block < first_data_block || block >= total_blocks) {
        printf("BADBLOCK: Inode %d references invalid block %d", inode_num, block);
        return -1;
    }

    int block_index = block - first_data_block;
    int byte = block_index / 8;
    int bit = block_index % 8;

    if (block_refs[block_index] > 0) {
        printf("DUPLICATE: Inode %d references block %d which is already referenced", inode_num, block);
        return -1;
    }

    if (!(data_bitmap[byte] & (1 << bit))) {
        printf("BITMAPERROR: Inode %d references block %d but bitmap says free", inode_num, block);
        block_refs[block_index]++;
        return -2;
    }
    block_refs[block_index]++;
    return 0;
}

void check_inode_data_bitmap_consistency(FILE* fs_image, const Inode* inode, const uint8_t* data_bitmap, 
                                        int* block_refs, int first_data_block, int total_blocks, 
                                        int block_size, int inode_num) {
    if (inode->hard_links <= 0 || inode->del_time != 0) {
        return;
    }

    if (inode->no_blocks == 0) {
        return;
    }

    for (int i = 0; i < 12; i++) {
        int res = check_block_reference(inode, data_bitmap, block_refs, first_data_block, 
                                      total_blocks, inode_num, inode->direct[i]);
        if (res != 0) {
            printf(" || DIRECT[%d]\n", i);
        }
    }

    if (inode->indirect != 0) {
        int res = check_block_reference(inode, data_bitmap, block_refs, first_data_block,
                                      total_blocks, inode_num, inode->indirect);
        if (res != 0) {
            printf(" || INDIRECT\n");
        }
        
        if (res != -1) {
            uint32_t* pointers = malloc(block_size);
            if (pointers) {
                fseek(fs_image, inode->indirect * block_size, SEEK_SET);
                fread(pointers, block_size, 1, fs_image);
                
                for (int i = 0; i < block_size/sizeof(uint32_t); i++) {
                    res = check_block_reference(inode, data_bitmap, block_refs,
                                              first_data_block, total_blocks,
                                              inode_num, pointers[i]);
                    if (res != 0) {
                        printf(" || INDIRECT_PTR[%d]\n", i);
                    }
                }
                free(pointers);
            }
        }
    }

    if (inode->double_indirect != 0) {
        int res = check_block_reference(inode, data_bitmap, block_refs,
                                      first_data_block, total_blocks,
                                      inode_num, inode->double_indirect);
        if (res != 0) {
            printf(" || DOUBLE_INDIRECT\n");
        }
        
        if (res != -1) {
            uint32_t* l1_pointers = malloc(block_size);
            if (l1_pointers) {
                fseek(fs_image, inode->double_indirect * block_size, SEEK_SET);
                if (fread(l1_pointers, block_size, 1, fs_image) == 1) {
                    for (int i = 0; i < block_size/sizeof(uint32_t); i++) {
                        if (l1_pointers[i] == 0) continue;
                        
                        res = check_block_reference(inode, data_bitmap, block_refs,
                                                  first_data_block, total_blocks,
                                                  inode_num, l1_pointers[i]);
                        if (res != 0) {
                            printf(" || DOUBLE_INDIRECT_L1[%d]\n", i);
                        }
                        
                        if (res != -1) {
                            uint32_t* l2_pointers = malloc(block_size);
                            if (l2_pointers) {
                                fseek(fs_image, l1_pointers[i] * block_size, SEEK_SET);
                                if (fread(l2_pointers, block_size, 1, fs_image) == 1) {
                                    for (int j = 0; j < block_size/sizeof(uint32_t); j++) {
                                        res = check_block_reference(inode, data_bitmap, block_refs,
                                                                  first_data_block, total_blocks,
                                                                  inode_num, l2_pointers[j]);
                                        if (res != 0) {
                                            printf(" || DOUBLE_INDIRECT_L2[%d][%d]\n", i, j);
                                        }
                                    }
                                } else {
                                    printf("ERROR: Failed to read level2 block %d\n", l1_pointers[i]);
                                }
                                free(l2_pointers);
                            }
                        }
                    }
                } else {
                    printf("ERROR: Failed to read double indirect block %d\n", inode->double_indirect);
                }
                free(l1_pointers);
            }
        }
    }

    if (inode->triple_indirect != 0) {
        int res = check_block_reference(inode, data_bitmap, block_refs,
                                      first_data_block, total_blocks,
                                      inode_num, inode->double_indirect);
        if (res != 0) {
            printf(" || TRIPLE_INDIRECT\n");
        }
        
        if (res != -1) {
            uint32_t* l1_pointers = malloc(block_size);
            if (l1_pointers) {
                fseek(fs_image, inode->triple_indirect * block_size, SEEK_SET);
                if (fread(l1_pointers, block_size, 1, fs_image) == 1) {
                    for (int i = 0; i < block_size/sizeof(uint32_t); i++) {
                        if (l1_pointers[i] == 0) continue;
                        
                        res = check_block_reference(inode, data_bitmap, block_refs,
                                                  first_data_block, total_blocks,
                                                  inode_num, l1_pointers[i]);
                        if (res != 0) {
                            printf(" || TRIPLE_INDIRECT_L1[%d]\n", i);
                        }
                        
                        if (res != -1) {
                            uint32_t* l2_pointers = malloc(block_size);
                            if (l2_pointers) {
                                fseek(fs_image, l1_pointers[i] * block_size, SEEK_SET);
                                if (fread(l2_pointers, block_size, 1, fs_image) == 1) {
                                    for (int j = 0; j < block_size/sizeof(uint32_t); j++) {
                                        res = check_block_reference(inode, data_bitmap, block_refs,
                                                                  first_data_block, total_blocks,
                                                                  inode_num, l2_pointers[j]);
                                        if (res != 0) {
                                            printf(" || TRIPLE_INDIRECT_L2[%d][%d]\n", i, j);
                                        }
                                        if (res != -1) {
                                            uint32_t* l3_pointers = malloc(block_size);
                                            if (l3_pointers) {
                                                fseek(fs_image, l2_pointers[j] * block_size, SEEK_SET);
                                                if (fread(l3_pointers, block_size, 1, fs_image) == 1) {
                                                    for (int k = 0; k < block_size/sizeof(uint32_t); k++) {
                                                        res = check_block_reference(inode, data_bitmap, block_refs,
                                                                                    first_data_block, total_blocks,
                                                                                    inode_num, l3_pointers[k]);
                                                        if (res != 0) {
                                                            printf(" || TRIPLE_INDIRECT_L3[%d][%d][%d]\n", i, j, k);
                                                        }
                                                    }
                                                } else {
                                                    printf("ERROR: Failed to read level3 block %d\n", l2_pointers[j]);
                                                }
                                                free(l3_pointers);
                                            }
                                        }
                                    }
                                } else {
                                    printf("ERROR: Failed to read level2 block %d\n", l1_pointers[i]);
                                }
                                free(l2_pointers);
                            }
                        }
                    }
                } else {
                    printf("ERROR: Failed to read triple indirect block %d\n", inode->double_indirect);
                }
                free(l1_pointers);
            }
        }
    }
}

void check_data_bitmap_consistency(FILE* fs_image, Inode** inodes, const Superblock* sb) {
    uint8_t data_bitmap[4096];
    fseek(fs_image, sb->data_bitmap_block * sb->block_size, SEEK_SET);
    if (fread(data_bitmap, sb->block_size, 1, fs_image) != 1) {
        perror("Failed to read data bitmap");
        return;
    }

    int no_of_datablocks = sb->total_blocks - sb->first_data_block;
    int* block_refs = calloc(no_of_datablocks, sizeof(int));
    if (!block_refs) {
        perror("Failed to allocate block_refs");
        return;
    }

    for (int i = 0; i < sb->inode_count; i++) {
        check_inode_data_bitmap_consistency(fs_image, inodes[i], data_bitmap, block_refs, 
                                          sb->first_data_block, sb->total_blocks, 
                                          sb->block_size, i);
    }
    // Verify bitmap vs reference counts
    for (int b = sb->first_data_block; b < sb->total_blocks; b++) {
        int idx = b - sb->first_data_block;
        int byte = idx / 8;
        int bit = idx % 8;
        int is_allocated = (data_bitmap[byte] >> bit) & 1;
        int ref_count = block_refs[idx];

        if (is_allocated && ref_count == 0) {
            printf("UNUSED_BLOCK: Block %u marked used but not referenced\n", b);
        } else if (!is_allocated && ref_count > 0) {
            printf("MISSING_BITMAP: Block %u referenced %d times but marked free\n", b, ref_count);
        }
    }

    free(block_refs);
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        printf("Usage: %s <fs_image>\n", argv[0]);
        return 1;
    }
    
    FILE* fs_image = fopen(argv[1], "r+");
    if (!fs_image) {
        perror("Failed to open file system image");
        return 1;
    }
    
    int superblock_success = check_superblock(fs_image);
    
    if (superblock_success != 0) {
        printf("Error in superblock. Exiting program ...");
        fclose(fs_image);
        return 0;
    }

    Superblock sb;
    fseek(fs_image, 0, SEEK_SET);
    fread(&sb, sizeof(Superblock), 1, fs_image);
    
    Inode** inodes = malloc(sb.inode_count * sizeof(Inode*));
    if (!inodes) {
        perror("Failed to allocate inodes");
        fclose(fs_image);
        return 1;
    }

    for (int i = 0; i < sb.inode_count; i++) {
        inodes[i] = malloc(sizeof(Inode));
        if (!inodes[i]) {
            perror("Failed to allocate inode");
            for (int j = 0; j < i; j++) free(inodes[j]);
            free(inodes);
            fclose(fs_image);
            return 1;
        }
        fseek(fs_image, 3*4096 + i*256, SEEK_SET);
        fread(inodes[i], sizeof(Inode), 1, fs_image);
    }

    check_inode_bitmap_consistency(fs_image, inodes, &sb);
    check_data_bitmap_consistency(fs_image, inodes, &sb);

    for (int i = 0; i < sb.inode_count; i++) {
        free(inodes[i]);
    }
    free(inodes);
    fclose(fs_image);
    printf("File system check completed successfully\n");
    return 0;
}