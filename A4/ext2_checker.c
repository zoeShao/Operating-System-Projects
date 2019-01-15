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

// (ext2_checker) (helper for Part b)
void check_dir_block(int b_num, struct ext2_inode *inodes, int *count) {
	int byte_index = 0;
    // go to the beginning of the block.
    unsigned char *block = (unsigned char *) (disk + EXT2_BLOCK_SIZE*b_num);
    while (byte_index != EXT2_BLOCK_SIZE) {
		struct ext2_dir_entry *dir_entry = (struct ext2_dir_entry *) (block + byte_index);
		if ((inodes[dir_entry->inode-1].i_mode & EXT2_S_IFREG) != 0) {
			if ((dir_entry->file_type & EXT2_FT_SYMLINK) != 0) {
				dir_entry->file_type = EXT2_FT_REG_FILE;
				fprintf(stderr, "Fixed: Entry type vs inode mismatch: inode [%d].\n", dir_entry->inode);
				*count += 1;
			}
		} else if ((inodes[dir_entry->inode-1].i_mode & EXT2_S_IFLNK) != 0) {
			if ((dir_entry->file_type & EXT2_FT_REG_FILE) != 0) {
				dir_entry->file_type = EXT2_FT_SYMLINK;
				fprintf(stderr, "Fixed: Entry type vs inode mismatch: inode [%d].\n", dir_entry->inode);
				*count += 1;
			}
		} else if ((inodes[dir_entry->inode-1].i_mode & EXT2_S_IFDIR) != 0) {
			if ((dir_entry->file_type & EXT2_FT_DIR) == 0) {
				dir_entry->file_type = EXT2_FT_DIR;
				fprintf(stderr, "Fixed: Entry type vs inode mismatch: inode [%d].\n", dir_entry->inode);
				*count += 1;
			}
		}
		byte_index += dir_entry->rec_len;
	}
}

// (ext2_checker) (helper for Part c)
void check_inode_allocated(int b_num, unsigned char *inode_bits, struct ext2_super_block *sb, struct ext2_group_desc *gd, int *count) {
	int byte_index = 0;
    // go to the beginning of the block.
    unsigned char *block = (unsigned char *) (disk + EXT2_BLOCK_SIZE*b_num);
    while (byte_index != EXT2_BLOCK_SIZE) {
		struct ext2_dir_entry *dir_entry = (struct ext2_dir_entry *) (block + byte_index);
		if (occupied(inode_bits, dir_entry->inode-1) == 0) {
			inode_bits[(dir_entry->inode-1)/8] |= (1 << ((dir_entry->inode-1) % 8));  // Update bit-map
			gd->bg_free_inodes_count--;
			sb->s_free_inodes_count--;
			fprintf(stderr, "Fixed: inode [%d] not marked as in-use.\n", dir_entry->inode);
			*count += 1;
		}
		byte_index += dir_entry->rec_len;
	}
}

// (ext2_checker) (helper for Part d)
void check_i_dtime(int b_num, struct ext2_inode *inodes, int *count) {
	int byte_index = 0;
    // go to the beginning of the block.
    unsigned char *block = (unsigned char *) (disk + EXT2_BLOCK_SIZE*b_num);
    while (byte_index != EXT2_BLOCK_SIZE) {
		struct ext2_dir_entry *dir_entry = (struct ext2_dir_entry *) (block + byte_index);
		if (inodes[dir_entry->inode-1].i_dtime != 0) {
			inodes[dir_entry->inode-1].i_dtime = 0;
			fprintf(stderr, "Fixed: valid inode marked for deletion: [%d].\n", dir_entry->inode);
			*count += 1;
		}
		byte_index += dir_entry->rec_len;
	}
}

// (ext2_checker) (helper for Part e)
void check_block_allocated(int b_num, struct ext2_inode *inodes, unsigned char *block_bits, struct ext2_super_block *sb, struct ext2_group_desc *gd, int *count) {
	int byte_index = 0;
    // go to the beginning of the block.
    unsigned char *block = (unsigned char *) (disk + EXT2_BLOCK_SIZE*b_num);
    while (byte_index != EXT2_BLOCK_SIZE) {
		struct ext2_dir_entry *dir_entry = (struct ext2_dir_entry *) (block + byte_index);
		int i;
		int blocks_num;
		blocks_num = inodes[dir_entry->inode-1].i_blocks/2;
		if (blocks_num > 12) {
            blocks_num = blocks_num - 1;
        }
        int block_count = 0;
		for (i = 0; i < blocks_num; i++) {
			if (i < 12) {
				if (occupied(block_bits, inodes[dir_entry->inode-1].i_block[i]-1) == 0) {
					block_bits[(inodes[dir_entry->inode-1].i_block[i]-1)/8] |= (1 << (inodes[dir_entry->inode-1].i_block[i]-1) % 8); // Update bit-map
					gd->bg_free_blocks_count--;
					sb->s_free_blocks_count--;
					block_count += 1;
				}
			} else {
				if (i == 12) {
					if (occupied(block_bits, inodes[12].i_block[i]-1) == 0) {
						block_bits[(inodes[dir_entry->inode-1].i_block[i]-1)/8] |= (1 << (inodes[dir_entry->inode-1].i_block[i]-1) % 8); // Update bit-map
						gd->bg_free_blocks_count--;
						sb->s_free_blocks_count--;
						block_count += 1;
					}
					int byte_index = (i - 12)*4;
    				// go to the beginning of the block.
    				unsigned char *block = (unsigned char *) (disk + EXT2_BLOCK_SIZE*inodes[dir_entry->inode-1].i_block[12]);
    				unsigned char *data_b_num = (unsigned char *) (block + byte_index);
    				if (occupied(block_bits, inodes[*data_b_num-1].i_block[i]-1) == 0) {
    					block_bits[(*data_b_num-1)/8] |= (1 << (*data_b_num-1) % 8); // Update bit-map
						gd->bg_free_blocks_count--;
						sb->s_free_blocks_count--;
						block_count += 1;
    				}
				}
			}
		}
		byte_index += dir_entry->rec_len;
		if (block_count > 0) {
			*count += block_count;
			fprintf(stderr, "Fixed: %d in-use data blocks not marked in data bitmap for inode: [%d].\n", block_count, dir_entry->inode);
		}
	}
}

/**
* The ext2_checker program takes only one command line argument:
* the name of an ext2 formatted virtual disk. 
* The program should implement a lightweight file system checker, 
* which detects a small subset of possible file system inconsistencies 
* and takes appropriate actions to fix them 
* (as well as counts the number of fixes).
*/
int main(int argc, char **argv) {
    int count = 0;
    //========================== Check input number ==========================
    if(argc != 2) {
        fprintf(stderr, "Usage: %s <image file name>\n", argv[0]);
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

    //---------------------------- Checker part a --------------------------------------------
    // Check a: the superblock and block group counters for free blocks and free inodes must match 
    // the number of free inodes and data blocks as indicated in the respective bitmaps. 
    // If an inconsistency is detected, the checker will trust the bitmaps and update the counters. 
    // Once such an inconsistency is fixed, your program should output a message.
    int total_free_inodes = gd->bg_free_inodes_count;
    int total_free_blocks = gd->bg_free_blocks_count;
    int count_inodes = 0;
    int count_blocks = 0;
    int i;
    for (i = 0; i < sb->s_inodes_count; i++) {
    	if (occupied(inode_bits, i) == 0) {
    		count_inodes++;
    	}
    }
    if (count_inodes != total_free_inodes) {
    	int z = abs(gd->bg_free_inodes_count - count_inodes);
    	gd->bg_free_inodes_count = count_inodes;
    	sb->s_free_inodes_count = count_inodes;
    	count += z;
    	fprintf(stderr, "Fixed: block group's free inodes counter was off by %d compared to the bitmap.\n", z);
    }
    for (i = 0; i < sb->s_blocks_count; i++) {
    	if (occupied(block_bits, i) == 0) {
    		count_blocks++;
    	}
    }
    if (count_blocks != total_free_blocks) {
    	int z = abs(gd->bg_free_blocks_count - count_blocks);
    	gd->bg_free_blocks_count = count_blocks;
    	sb->s_free_blocks_count = count_blocks;
    	count += z;
    	fprintf(stderr, "Fixed: block group's free blocks counter was off by %d compared to the bitmap.\n", z);
    }

    int inode_index;
    int num_inodes = sb->s_inodes_count;
    //---------------------------- Checker part c --------------------------------------------
    // For each file, directory or symlink, you must check that its inode
    // is marked as allocated in the inode bitmap. If it isn't, 
    // then the inode bitmap must be updated to indicate that the inode is in use. 
    // You should also update the corresponding counters in the block group and superblock 
    // (they should be consistent with the bitmap at this point). 
    // Once such an inconsistency is repaired, your program should output a message. 
    // Each inconsistency counts towards to total number of fixes.
    //------------> Start from the root:
    for (i = 0; i < inodes[EXT2_ROOT_INO-1].i_blocks/2; i++) {
		check_inode_allocated(inodes[EXT2_ROOT_INO-1].i_block[i], inode_bits, sb, gd, &count);
    }
    //------------> Go through the rest directory (check inode 12-32, if directory, process it)
    for ((inode_index = EXT2_GOOD_OLD_FIRST_INO); inode_index < num_inodes; inode_index++) {
    	if ((occupied(inode_bits, inode_index)) && ((inodes[inode_index].i_mode & EXT2_S_IFDIR) != 0)) {
	    	for (i = 0; i < inodes[inode_index].i_blocks/2; i++) {
				check_inode_allocated(inodes[inode_index].i_block[i], inode_bits, sb, gd, &count);
     	    }
		}
    }
    //---------------------------- Checker part b --------------------------------------------
    // For each file, directory, or symlink, you must check if its inode's i_mode 
    // matches the directory entry file_type. If it does not, 
    // then you shall trust the inode's i_mode and fix the file_type to match. 
    // Once such an inconsistency is repaired, your program should output amessage.
    // Each inconsistency counts towards to total number of fixes.
    //------------> Start from the root:
    for (i = 0; i < inodes[EXT2_ROOT_INO-1].i_blocks/2; i++) {
		check_dir_block(inodes[EXT2_ROOT_INO-1].i_block[i], inodes, &count);
    }
    //------------> Go through the rest directory (check inode 12-32, if directory, process it)
    for ((inode_index = EXT2_GOOD_OLD_FIRST_INO); inode_index < num_inodes; inode_index++) {
    	if ((occupied(inode_bits, inode_index)) && ((inodes[inode_index].i_mode & EXT2_S_IFDIR) != 0)) {
	    	for (i = 0; i < inodes[inode_index].i_blocks/2; i++) {
				check_dir_block(inodes[inode_index].i_block[i], inodes, &count);
     	    }
		}
    }
    //---------------------------- Checker part d --------------------------------------------
    // For each file, directory, or symlink, you must check that its inode's i_dtime is set to 0.
    // If it isn't, you must reset (to 0), to indicate that the file should not be marked for removal.
    // Once such an inconsistency is repaired, your program should output 
    // the following message: "Fixed: valid inode marked for deletion: [I]", 
    // where I is the inode number. Each inconsistency counts towards to total number of fixes.
    //------------> Start from the root:
    for (i = 0; i < inodes[EXT2_ROOT_INO-1].i_blocks/2; i++) {
		check_i_dtime(inodes[EXT2_ROOT_INO-1].i_block[i], inodes, &count);
    }
    //------------> Go through the rest directory (check inode 12-32, if directory, process it)
    for ((inode_index = EXT2_GOOD_OLD_FIRST_INO); inode_index < num_inodes; inode_index++) {
    	if ((occupied(inode_bits, inode_index)) && ((inodes[inode_index].i_mode & EXT2_S_IFDIR) != 0)) {
	    	for (i = 0; i < inodes[inode_index].i_blocks/2; i++) {
				check_i_dtime(inodes[inode_index].i_block[i], inodes, &count);
     	    }
		}
    }
    //---------------------------- Checker part e --------------------------------------------
    // For each file, directory, or symlink, you must check that all 
    // its data blocks are allocated in the data bitmap. 
    // If any of its blocks is not allocated, you must fix this by updating the data bitmap.
    // You should also update the corresponding counters in the block group and superblock, 
    // (they should be consistent with the bitmap at this point). 
    // Once such an inconsistency is fixed, your program should output 
    // the following message: "Fixed: D in-use data blocks not marked in data bitmap for inode: [I]",
    // where D is the number of data blocks fixed, and I is the inode number. 
    // Each inconsistency counts towards to total number of fixes.
    //------------> Start from the root:
    for (i = 0; i < inodes[EXT2_ROOT_INO-1].i_blocks/2; i++) {
		check_block_allocated(inodes[EXT2_ROOT_INO-1].i_block[i], inodes, block_bits, sb, gd, &count);
    }
    //------------> Go through the rest directory (check inode 12-32, if directory, process it)
    
    for ((inode_index = EXT2_GOOD_OLD_FIRST_INO); inode_index < num_inodes; inode_index++) {
    	if ((occupied(inode_bits, inode_index)) && ((inodes[inode_index].i_mode & EXT2_S_IFDIR) != 0)) {
	    	for (i = 0; i < inodes[inode_index].i_blocks/2; i++) {
				check_block_allocated(inodes[inode_index].i_block[i], inodes, block_bits, sb, gd, &count);
     	    }
		}
    }

    //---------------------------- Final step --------------------------------------------
    // Your program must count all the fixed inconsistencies, 
    // and produce one last message: either "N file system inconsistencies repaired!",
    // where N is the number of fixes made, or "No file system inconsistencies detected!". 
    if (count > 0) {
    	fprintf(stderr, "%d file system inconsistencies repaired!\n", count);
    } else {
    	fprintf(stderr, "No file system inconsistencies detected!\n");
    }
    return 0;
}