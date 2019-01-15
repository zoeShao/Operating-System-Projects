#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>
#include "ext2.h"
#include "ext2_utils.h"

unsigned char *disk;
/**
The ext2_cp program takes three command line arguments. 
The first is the name of an ext2 formatted virtual disk. 
The second is the path to a file on your native operating system, 
and the third is an absolute path on your ext2 formatted disk. 
The program should work like cp, copying the file 
on your native file system onto the specified location on the disk. 
If the source file does not exist or the target is an invalid path, 
then your program should return the appropriate error (ENOENT). 
If the target is a file with the same name that already exists, 
you should not overwrite it (as cp would), just return EEXIST instead.
*/
int main(int argc, char **argv) {
    int error = 0;
    //========================== Check input number ==========================
    if(argc != 4) {
        fprintf(stderr, "Usage: %s <image file name> <a path to a file> <an absolute path>\n", argv[0]);
        exit(1);
    }
    int fd = open(argv[1], O_RDWR);
    //========================== Set up ==========================
    // Read disk image into memory.
    disk = mmap(NULL, 128 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(disk == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }
    // Get the super block. (located at block #2)
    struct ext2_super_block *sb = (struct ext2_super_block *)(disk + EXT2_BLOCK_SIZE);
    // Get the block group descriptor. (located at block #3)
    struct ext2_group_desc *gd = (struct ext2_group_desc *)(disk + EXT2_BLOCK_SIZE*2);
    // Get the block bitmap. (located at block #4)
    unsigned char *block_bits = (unsigned char *)(disk + EXT2_BLOCK_SIZE*gd->bg_block_bitmap);
    // Get the inode bitmap. (located at block #5)
    unsigned char *inode_bits = (unsigned char *)(disk + EXT2_BLOCK_SIZE*gd->bg_inode_bitmap);
    // Locate inode table. (start at block #6)
    struct ext2_inode *inodes = (struct ext2_inode *)(disk + EXT2_BLOCK_SIZE*gd->bg_inode_table);
    //========================== Handle the file path ==========================
    // <Step 1>: get the size and the content of the file.
    char *file_path = argv[2];
    char **buffer;
    unsigned int file_size;
    int blocks; // Used to store how many blocks a file need to store its content
    FILE *fp;
    fp = fopen(file_path, "rb+");
    if (fp == NULL) { // Check the file path is valid or not.
        perror("Failed(fopen)");
        exit(1);
    } else {
        fseek(fp, 0L, SEEK_END);
        file_size = ftell(fp);
        fseek(fp, 0L, SEEK_SET); // When get the size of the file, seek back.
        // Allocate the buffer.
        if (file_size % 1024 == 0) {
            blocks = file_size/1024;
        } else {
            blocks = (file_size/1024) + 1;
        }
        buffer = malloc(blocks*EXT2_BLOCK_SIZE);
        int j;
        for (j = 0; j < blocks; j++) {
            buffer[j] = malloc(EXT2_BLOCK_SIZE);
            if (buffer[j]) {
                fread(buffer[j], EXT2_BLOCK_SIZE, 1, fp);
            }
        }
    }
    if (fclose(fp) == -1) {
        perror("fclose");
        exit(1);
    }
    // <Step 2>: get the type of the file.
    char file_type = '\0';
    struct stat buf;
    lstat(file_path, &buf);
    if ((buf.st_mode & S_IFREG) != 0) {
        file_type = 'f'; 
    } else if ((buf.st_mode & S_IFLNK) != 0) {
        file_type = 'l'; 
    }
    // <Step 3>: get the name of the file.
    int file_num; // This variavle will not be used later.
    char *file_name = get_dir_name(file_path, &file_num); // Get the file name.
    //========================== handle the absolute path ==========================
    char *path = argv[3];
    int dir_num; // Used to store the number of directories.
    char *name = get_dir_name(path, &dir_num);
    char *path_name[dir_num];
    get_path_name(path, path_name, dir_num);
    name = path_name[dir_num-1];
    int name_bit; // Used to store the which name to use. (0:file_name; 1:name)
    int i_num; // Used to store parent inode number.
    int i_p_num; // Used to store the grand parent inode number.
    // check whether the arguments provided is valid.
    error = check_arg_valid(inodes, path_name, dir_num, name, file_name, &i_num, &i_p_num, &name_bit);
    if (error == 0) {
        //-------------------Find an empty inode (number)-------------------
        int new_inode = get_free_inode(inode_bits, sb->s_inodes_count) + 1;
        if (new_inode == -1) {
            return ENOSPC;
        }
        gd->bg_free_inodes_count--;
        inode_bits[(new_inode-1)/8] |= (1 << ((new_inode - 1) % 8));
        //-------------------Set up inode table-------------------
        if (file_type == 'f') {
            init_file_new_inode(inodes, EXT2_S_IFREG, new_inode-1, file_size);
        } else if (file_type == 'l') {
            init_file_new_inode(inodes, EXT2_S_IFLNK, new_inode-1, file_size);
        }
        //-------------------Put the content into the data block-------------------
        int i;
        int indirect_block;
        for (i = 0; i < blocks; i++) {
            if (i < 12) {
                // Find a free block (number) for this directory.
                int new_block = get_free_block(block_bits, sb->s_blocks_count) + 1;
                if (new_block == -1) {
                    return ENOSPC;
                }
                memcpy((disk + (new_block * EXT2_BLOCK_SIZE)), buffer[i], EXT2_BLOCK_SIZE);
                inodes[new_inode-1].i_block[i] = new_block;
                block_bits[(new_block-1)/8] |= (1 << (new_block - 1) % 8); // Update bit-map.
                gd->bg_free_blocks_count--; // Update block-group-descriptor.
            } else {
                if (i == 12) { // we need to use indirect block.
                    indirect_block = get_free_block(block_bits, sb->s_blocks_count) + 1;
                    if (indirect_block == -1) {
                        return ENOSPC;
                    }
                    inodes[new_inode-1].i_blocks += 2;
                    block_bits[(indirect_block-1)/8] |= (1 << (indirect_block - 1) % 8); // Update bit-map
                    gd->bg_free_blocks_count--; // Update block-group-descriptor.
                    inodes[new_inode-1].i_block[12] = indirect_block;
                }
                int *indirect;
                int new_block = get_free_block(block_bits, sb->s_blocks_count) + 1;
                if (new_block == -1) {
                    return ENOSPC;
                }
                block_bits[(new_block-1)/8] |= (1 << (new_block - 1) % 8); // Update bit-map
                gd->bg_free_blocks_count--; // Update block-group-descriptor.
                indirect = &new_block;
                memcpy((disk + (new_block * EXT2_BLOCK_SIZE)), buffer[i], EXT2_BLOCK_SIZE);
                memcpy((disk + (indirect_block * EXT2_BLOCK_SIZE + 4*(i-12))), indirect, 4);
            }
        }
        //-------------------Setup parent entry-------------------
        int b_num; // Note: if name bit = 0 -> file_name; if name bit = 1 -> name.
        for(i = 0; i<inodes[i_num-1].i_blocks/2; i++) {
            b_num = inodes[i_num-1].i_block[i];
        }
        if (file_type == 'f') {
            if (name_bit == 0) {
                setup_parent_block(gd, block_bits, inodes, sb->s_blocks_count, i_num, b_num, new_inode, EXT2_FT_REG_FILE, file_name);
            } else if (name_bit == 1) {
                setup_parent_block(gd, block_bits, inodes, sb->s_blocks_count, i_num, b_num, new_inode, EXT2_FT_REG_FILE, name);
            }
        } else if (file_type == 'l') {
            if (name_bit == 0) {
                setup_parent_block(gd, block_bits, inodes, sb->s_blocks_count, i_num, b_num, new_inode, EXT2_FT_REG_FILE, file_name);
            } else if (name_bit == 1) {
                setup_parent_block(gd, block_bits, inodes, sb->s_blocks_count, i_num, b_num, new_inode, EXT2_FT_REG_FILE, name);
            }
        }
        //-------------------Update super block-------------------
        sb->s_free_inodes_count = gd->bg_free_inodes_count;
        sb->s_free_blocks_count = gd->bg_free_blocks_count;
    }
    return error;
}