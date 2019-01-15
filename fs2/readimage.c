#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include "ext2.h"

unsigned char *disk;

int occupied(unsigned char *bit_map, int index) {
    int which_byte = index / 8;
    int which_bit = index % 8;
    unsigned char mask = 1 << which_bit;
    return bit_map[which_byte] & mask;
}

// fs1 tasks:
void print_blockgroup(struct ext2_group_desc *gd) {
    printf("Block group:\n");
    printf("    block bitmap: %d\n", gd->bg_block_bitmap);
    printf("    inode bitmap: %d\n", gd->bg_inode_bitmap);
    printf("    inode table: %d\n", gd->bg_inode_table);
    printf("    free blocks: %d\n", gd->bg_free_blocks_count);
    printf("    free inodes: %d\n", gd->bg_free_inodes_count);
    printf("    used_dirs: %d\n", gd->bg_used_dirs_count);
}

// fs2 tasks: print out the inode and block bitmaps, and the inodes.
void print_bitmap(unsigned char *block_bits, int num_bytes) {
    int byte;
    int bit;
    unsigned char in_use;
    for (byte = 0; byte < num_bytes; byte++) {
	for (bit = 0; bit < 8; bit++) {
	    in_use = (block_bits[byte] & (1 << bit)) >> bit;
	    printf("%d", in_use);
	}
	printf(" ");
    }
    printf("\n");
    return;
}

void print_one_inode(struct ext2_inode *inodes, int i_num) {
    char type;
    if ((inodes[i_num-1].i_mode & EXT2_S_IFDIR) != 0) {
	type = 'd';
    } else if ((inodes[i_num-1].i_mode & EXT2_S_IFREG) != 0) {
	type = 'f';
    } else if ((inodes[i_num-1].i_mode & EXT2_S_IFLNK) != 0) {
	type = 'l';
    } else {
	type = '0';
    }
    printf("[%d] type: %c size: %d links: %d blocks: %d\n", i_num, type, inodes[i_num-1].i_size, inodes[i_num-1].i_links_count, inodes[i_num-1].i_blocks);
    // print data blocks:
    printf("[%d] Blocks: ", i_num);
    int i;
    for (i = 0; i < inodes[i_num-1].i_blocks/2; i++) {
	printf(" %d", inodes[i_num-1].i_block[i]);
    }
    printf("\n");
    return;  
}

void print_inodes(struct ext2_inode *inodes, unsigned char *inode_bits, int num_inodes) {
    // print out the second inode
    print_one_inode(inodes, EXT2_ROOT_INO);

    // print our the rest inodes with index greater than 12.
    int inode_index;
    for ((inode_index = EXT2_GOOD_OLD_FIRST_INO); inode_index < num_inodes;inode_index++) {
    	if (occupied(inode_bits, inode_index)) {
	    print_one_inode(inodes, inode_index+1);
	}
    }
}

int main(int argc, char **argv) {
    if(argc != 2) {
        fprintf(stderr, "Usage: %s <image file name>\n", argv[0]);
        exit(1);
    }
    int fd = open(argv[1], O_RDWR);

    // Read disk image into memory.
    disk = mmap(NULL, 128 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(disk == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }

    // Read and print super block.
    struct ext2_super_block *sb = (struct ext2_super_block *)(disk + EXT2_BLOCK_SIZE);
    printf("Inodes: %d\n", sb->s_inodes_count);
    printf("Blocks: %d\n", sb->s_blocks_count);

    // Read and print block group descriptor.
    struct ext2_group_desc *gd = (struct ext2_group_desc *)(disk + EXT2_BLOCK_SIZE*2);
    print_blockgroup(gd);
  
    // Read and print block bitmap.
    unsigned char *block_bits = (unsigned char *)(disk + EXT2_BLOCK_SIZE*gd->bg_block_bitmap);
    printf("Block bitmap: ");
    print_bitmap(block_bits, sb->s_blocks_count/8);

    // Read and print inode bitmap.
    unsigned char *inode_bits = (unsigned char *)(disk + EXT2_BLOCK_SIZE*gd->bg_inode_bitmap);
    printf("Inode bitmap: ");
    print_bitmap(inode_bits, sb->s_inodes_count/8);

    // Locate inode table.
    struct ext2_inode *inodes = (struct ext2_inode *)(disk + 1024*gd->bg_inode_table);
    
    // Read and print inodes.
    printf("\nInodes:\n");
    print_inodes(inodes, inode_bits, sb->s_inodes_count);

    return 0;
}
