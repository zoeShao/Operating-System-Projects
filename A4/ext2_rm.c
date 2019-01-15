#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>
#include <time.h>
#include "ext2.h"
#include "ext2_utils.h"

unsigned char *disk;
/**
* The ext2_rm program takes two command line arguments. 
* The first is the name of an ext2 formatted virtual disk, 
* and the second is an absolute path to a file or link (not a directory) on that disk. 
* The program should work like rm, removing the specified file from the disk. 
* If the file does not exist or if it is a directory, 
* then your program should return the appropriate error. 
* Note:
* 	e.g., no need to zero out data blocks, 
*	must set i_dtime in the inode, removing a directory entry need not shift the directory entries after the one being deleted, etc.).
*/
int main(int argc, char **argv) {
    int error = 0;
    //========================== Check input number ==========================
    if(argc != 3) {
        fprintf(stderr, "Usage: %s <image file name> <path to link>\n", argv[0]);
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
        fprintf(stderr, "Cannot remove the root directory.\n");
        return EEXIST;
    }
    char *path_name[dir_num]; // An array store each of the directory name along the path.
    get_path_name(path, path_name, dir_num);
    name = path_name[dir_num-1];
    //========================== Check the input argument is valid or not ==========================
    int f_num; // Used to store the inode number of the file.
    int i_num; // Used to store parent inode number.
    int i_p_num; // Used to store the grand parent inode number.
    error = check_path_valid_rm(path_name, dir_num, inodes, &f_num, &i_num, &i_p_num);
    if (error == 0) {
    	int i;
    	int blocks_num;
    	//-------------------Set up inode table-------------------
    	inodes[f_num-1].i_links_count--;
    	if (inodes[f_num-1].i_links_count <= 0) {
    		inode_bits[(f_num - 1) / 8] &= ~(1 << (f_num - 1) % 8); // Update bit-map
    		gd->bg_free_inodes_count++; // Update block-group-descriptor.
    		blocks_num = inodes[f_num-1].i_blocks/2;
            if (blocks_num > 12) {
                blocks_num = blocks_num - 1;
            }
    		for (i = 0; i < blocks_num; i++) {
    			if (i < 12) {
    				block_bits[(inodes[f_num-1].i_block[i] - 1) / 8] &= ~(1 << (inodes[f_num-1].i_block[i] - 1) % 8); // Update bit-map
    				gd->bg_free_blocks_count++; // Update block-group-descriptor.
    			} else {
    				int indir_b_num = inodes[f_num-1].i_block[12];
    				if (i == 12) {
    					block_bits[(indir_b_num - 1) / 8] &= ~(1 << (indir_b_num - 1) % 8); // Update bit-map
    					gd->bg_free_blocks_count++; // Update block-group-descriptor.
    				}
    				int byte_index = (i - 12)*4;
    				// go to the beginning of the block.
    				unsigned char *block = (unsigned char *) (disk + EXT2_BLOCK_SIZE*indir_b_num);
    				unsigned char *data_b_num = (unsigned char *) (block + byte_index);
    				block_bits[(*data_b_num - 1) / 8] &= ~(1 << (*data_b_num - 1) % 8); // Update bit-map
    				gd->bg_free_blocks_count++; // Update block-group-descriptor.
    			}
    		}
    		inodes[f_num-1].i_dtime = time(NULL);
    	}
    	//-------------------Setup parent entry-------------------
    	int b_num;
        for(i = 0; i<inodes[i_num-1].i_blocks/2; i++) {
            b_num = inodes[i_num-1].i_block[i];
            shift_dir_entry(b_num, name);
        }
        //-------------------Update block-group-descriptor ------------------
        sb->s_free_blocks_count = gd->bg_free_blocks_count;
        sb->s_free_inodes_count = gd->bg_free_inodes_count;
    }
    return error;
}
