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
* The ext2_ln program takes three command line arguments. 
* The first is the name of an ext2 formatted virtual disk. 
* The other two are absolute paths on your ext2 formatted disk. 
* The program should work like ln, creating a link from the first 
* specified file to the second specified path. 
* This program should handle any exceptional circumstances, 
* for example: 
* if the source file does not exist (ENOENT), 
* if the link name already exists (EEXIST), 
* if a hardlink refers to a directory (EISDIR), etc. 
* then your program should return the appropriate error code. 
* Additionally, this command may take a "-s" flag, after the disk image argument. 
* When this flag is used, your program must create a symlink instead (other arguments remain the same).
* Note:
*		Do *not* implement fast symlinks, just store the path in a data block regardless of length, 
* 		to keep things uniform in your implementation, and to facilitate testing.
*/
int main(int argc, char **argv) {
    int error = 0;
    //========================== Check input number ==========================
    if(argc != 4 && argc != 5) {
        fprintf(stderr, "Usage: %s <image file name> [-s] <a path to a file> <an absolute path>\n", argv[0]);
        exit(1);
    } else if (argc == 5) {
    	if(!(strcmp(argv[2], "-s") == 0)) {
    		fprintf(stderr,"Invalid optional flag\n");
    		exit(1);
    	}
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

    //----------================= [Case 1]: hard link =================----------
    if (argc == 4) {
    	//========================== Handle the file path ==========================
    	// <Step 1>: get the name of the file.
    	char *file_path = argv[2];
    	int file_num; // Used to store the number of directories.
    	char *file_name;
    	file_name = get_dir_name(file_path, &file_num);
    	if (file_num == 0) {
        	fprintf(stderr, "hard link not allowed for directory.\n");
        	return EISDIR;
    	}
    	char *file_path_name[file_num]; // An array store each of the directory name along the path.
    	get_path_name(file_path, file_path_name, file_num);
    	file_name = file_path_name[file_num-1];
    	int f_num; // Used to store the inode number of the file.
    	error = check_path_valid_ln(file_path_name, file_num, inodes, &f_num);
    	if (error == 0) {
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
    			//-------------------increment link counts-------------------
    			inodes[f_num-1].i_links_count++;
        		//-------------------Setup parent entry-------------------
        		int b_num; // Note: if name bit = 0 -> file_name; if name bit = 1 -> name.
        		int i;
        		for(i = 0; i<inodes[i_num-1].i_blocks/2; i++) {
            		b_num = inodes[i_num-1].i_block[i];
        		}
            	if (name_bit == 0) {
            		setup_parent_block(gd, block_bits, inodes, sb->s_blocks_count, i_num, b_num, f_num, EXT2_FT_REG_FILE, file_name);
            	} else if (name_bit == 1) {
            		setup_parent_block(gd, block_bits, inodes, sb->s_blocks_count, i_num, b_num, f_num, EXT2_FT_REG_FILE, name);
            	}
            	//-------------------Update block-group-descriptor and super block-------------------
        		sb->s_free_inodes_count = gd->bg_free_inodes_count;
        		sb->s_free_blocks_count = gd->bg_free_blocks_count;

    		}
    	}
    //----------================= [Case 2]: soft link =================----------	
    } else {
    	//========================== Handle the file path ==========================
    	// <Step 1>: get the content of the file.
    	char *file_path = argv[3];
    	// <Step 2>: get the name of the file.
    	int file_num; // Used to store the number of directories.
    	char *file_name;
 
    	file_name = get_dir_name(file_path, &file_num);
    	if (file_num == 0) {
    		file_name = "/";
    	} else {
    		char *file_path_name[file_num]; // An array store each of the directory name along the path.
    		get_path_name(file_path, file_path_name, file_num);
    		file_name = file_path_name[file_num-1];
    	}
    	// [Note]: since it creates a soft link, we do not need validity checking.
    	//========================== handle the absolute path ==========================
    	char *path = argv[4];
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
        	inode_bits[(new_inode-1)/8] |= (1 << ((new_inode - 1) % 8)); // Update bit-map
        	gd->bg_free_inodes_count--; // Update block-group-descriptor.
        	//-------------------Set up inode table-------------------
      		init_file_new_inode(inodes, EXT2_S_IFLNK, new_inode-1, strlen(file_path));
      		//-------------------Put the content into the data block-------------------
      		int new_block = get_free_block(block_bits, sb->s_blocks_count) + 1;
      		if (new_block == -1) {
            	return ENOSPC;
        	}
            memcpy((disk + (new_block * EXT2_BLOCK_SIZE)), file_path, strlen(file_path));
            inodes[new_inode-1].i_block[0] = new_block;
            block_bits[(new_block-1)/8] |= (1 << (new_block - 1) % 8); // Update bit-map
            gd->bg_free_blocks_count--; // Update block-group-descriptor.
            //-------------------Setup parent entry-------------------
        	int i;
        	int b_num; // Note: if name bit = 0 -> file_name; if name bit = 1 -> name.
        	for(i = 0; i<inodes[i_num-1].i_blocks/2; i++) {
            	b_num = inodes[i_num-1].i_block[i];
        	}
        	if (name_bit == 0) {
            	setup_parent_block(gd, block_bits, inodes, sb->s_blocks_count, i_num, b_num, new_inode, EXT2_FT_SYMLINK, file_name);
            } else if (name_bit == 1) {
            	setup_parent_block(gd, block_bits, inodes, sb->s_blocks_count, i_num, b_num, new_inode, EXT2_FT_SYMLINK, name);
            }
            //-------------------Update super block-------------------
        	sb->s_free_inodes_count = gd->bg_free_inodes_count;
        	sb->s_free_blocks_count = gd->bg_free_blocks_count;
    	}
    }
    return error;
}