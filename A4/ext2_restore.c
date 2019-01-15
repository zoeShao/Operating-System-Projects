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
* The ext2_restore program takes two command line arguments. 
* The first is the name of an ext2 formatted virtual disk, 
* and the second is an absolute path to a file or link (not a directory!) on that disk. 
* The program should be the exact opposite of rm, 
* restoring the specified file that has been previous removed. 
* Note: If the file does not exist (it may have been overwritten), or if it is a directory, 
* then your program should return the appropriate error.
Note: If the directory entry for the file has not been overwritten, 
you will still need to make sure that the inode has not been reused, 
and that none of its data blocks have been reallocated. 
You may assume that the bitmaps are reliable indicators of such fact. 
If the file cannot be fully restored, your program should terminate with ENOENT, 
indicating that the operation was unsuccessful.
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
        fprintf(stderr, "Cannot restore the root directory.\n");
        return EEXIST;
    }
    char *path_name[dir_num]; // An array store each of the directory name along the path.
    get_path_name(path, path_name, dir_num);
    name = path_name[dir_num-1];
    //========================== Check the input argument is valid or not ==========================
    int i_num; // Used to store parent inode number.
    int i_p_num; // Used to store the grand parent inode number.
    error = check_arg_valid_restore(path_name, dir_num, name, inodes, &i_num, &i_p_num);
    if (error == 0) {
    	int i;
    	//-------------------Do the restore-------------------
    	int b_num;
        for(i = 0; i < inodes[i_num-1].i_blocks/2; i++) {
            b_num = inodes[i_num-1].i_block[i];
            error = shift_back_dir_entry(gd, inodes, inode_bits, block_bits, b_num, name);
            if (error == 0) {
            	break;
            }
        }
        if (error != 0) {
        	fprintf(stderr, "'%s': No such file.\n", name);
        	return ENOENT;
        }
        //-------------------Update block-group-descriptor ------------------
        sb->s_free_blocks_count = gd->bg_free_blocks_count;
        sb->s_free_inodes_count = gd->bg_free_inodes_count;

    }
    return error;
}