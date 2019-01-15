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
* The ext2_mkdir program takes two command line arguments. 
* The first is the name of an ext2 formatted virtual disk. 
* The second is an absolute path on your ext2 formatted disk. 
* The program should work like mkdir, 
* creating the final directory on the specified path on the disk. 
* If any component on the path to the location where 
* the final directory is to be created does not exist or 
* if the spec fied directory already exists, 
* then your program should return the appropriate error (ENOENT or EEXIST).
* Note: 1. directory entries should be aligned to 4B
*          entry names are not null-terminated, etc.
*        2. When you allocate a new inode or data block, 
*          you *must use the next one available* from the corresponding bitmap 
*          (excluding reserved inodes, of course).
*        3. Be careful to consider trailing slashes in paths. 
*          (on piazza: try this on VM: mkdir /mnt/multipleslashes////////)
*          (on piazza: one case: names of folders can have spaces)
*/
int main(int argc, char **argv) {
    int error = 0;
    //========================== Check input number ==========================
    if(argc != 3) {
        fprintf(stderr, "Usage: %s <image file name> <an absolute path>\n", argv[0]);
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
    struct ext2_inode *inodes = (struct ext2_inode *)(disk + 1024*gd->bg_inode_table);
    //========================== Handle the file path ==========================
    char *path = argv[2];
    int dir_num; // Used to store the number of directories.
    char *name;
    name = get_dir_name(path, &dir_num);
    if (dir_num == 0) {
        fprintf(stderr, "Cannot make the root directory, Since it already exists.\n");
        return EEXIST;
    }
    char *path_name[dir_num]; // An array store each of the directory name along the path.
    get_path_name(path, path_name, dir_num);
    name = path_name[dir_num-1];
    //========================== Check the input argument is valid or not ==========================
    int i_num; // Used to store parent inode number.
    int i_p_num; // Used to store the grand parent inode number.
    error = check_path_valid(path_name, dir_num, inodes, &i_num, &i_p_num);
    if (error == 0) {
        //-------------------Find an empty inode (number) for this directory-------------------
        int new_inode = get_free_inode(inode_bits, sb->s_inodes_count) + 1;
        if (new_inode == -1) {
            return ENOSPC;
        }
        inode_bits[(new_inode-1)/8] |= (1 << ((new_inode - 1) % 8)); // Update bit-map
        gd->bg_free_inodes_count--; // Update block-group-descriptor.
        //-------------------Find a free block (number) for this directory-------------------
        int new_block = get_free_block(block_bits, sb->s_blocks_count) + 1;
        if (new_block == -1) {
            return ENOSPC;
        }
        block_bits[(new_block-1)/8] |= (1 << (new_block - 1) % 8); // Update bit-map
        gd->bg_free_blocks_count--; // Update block-group-descriptor.
        //-------------------Set up inode table-------------------
        init_new_inode(inodes, EXT2_S_IFDIR, new_inode-1, EXT2_BLOCK_SIZE, new_block);
        //-------------------Setup self entry-------------------
        init_dir_block(new_block, new_inode, i_num);
        //-------------------Setup parent inode-------------------
        inodes[i_num-1].i_links_count++;
        //-------------------Setup parent entry-------------------
        int i;
        int b_num;
        for(i = 0; i<inodes[i_num-1].i_blocks/2; i++) {
            b_num = inodes[i_num-1].i_block[i];
        }
        setup_parent_block(gd, block_bits, inodes, sb->s_blocks_count, i_num, b_num, new_inode, EXT2_FT_DIR, name);
        //-------------------Update block-group-descriptor ------------------
        sb->s_free_blocks_count = gd->bg_free_blocks_count;
        sb->s_free_inodes_count = gd->bg_free_inodes_count;
        gd->bg_used_dirs_count++;
    }
    return error;
}