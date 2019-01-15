#ifndef __EX2_UTILS_H__
#define __EX2_UTILS_H__
#include "ext2.h"

extern unsigned char *disk;
//==========================================================
extern int occupied(unsigned char *bit_map, int index);
extern int get_free_block(unsigned char *bit_map, int num_inodes);
extern int get_free_inode(unsigned char *bit_map, int num_inodes);
extern char *get_dir_name(char *path, int *dir_num);
extern void get_path_name(char *path, char *path_name[], int dir_num);
extern int check_same_name(char *disk_entry_name, char *path_name, int name_len);
//============== helper function for ext2_cp ================
extern int check_dir_valid_cp(int b_num, char *path_name[], int index, int dir_num, int *inode_num);
extern int check_path_valid_cp(char *path_name[], int dir_num, struct ext2_inode *inodes, int *i_num, int *i_p_num);
extern int check_type(struct ext2_inode *inodes, int *i_p_num, char *path_name, int *type, int *i_num, int *i_g_num);
extern int check_current_inode(int result, int file_type, struct ext2_inode *inodes, char *path_name, int i_num, int *name_bit);
extern int check_arg_valid(struct ext2_inode *inodes, char *path_name_all[], int dir_num, char *name, char *file_name, int *i_g_num, int *i_num, int *name_bit);
//============== helper function for ext2_ln ================
//-----------------------(hard link)--------------------------
extern int check_file_valid(int b_num, char *path_name[], int index, int dir_num, int *inode_num);
extern int check_path_valid_ln(char *path_name[], int dir_num, struct ext2_inode *inodes, int *i_num);
//============== helper function for ext2_mkdir ================
extern int check_dir_valid(int b_num, char *path_name[], int index, int dir_num, int *inode_num);
extern int check_path_valid(char *path_name[], int dir_num, struct ext2_inode *inodes, int *i_num, int *i_p_num);
//============== helper function for ext2_rm ================
extern int check_path_valid_rm(char *path_name[], int dir_num, struct ext2_inode *inodes, int *f_num, int *i_num, int *i_p_num);
extern void shift_dir_entry(int b_num, char *name);
//============== helper function for ext2_restore ================
extern int get_correct_len(struct ext2_dir_entry *dir_entry);
extern int find_entry_exit(int b_num, char *name);
extern int check_can_restore(struct ext2_inode *inodes, unsigned char *inode_bits, unsigned char *block_bits, struct ext2_dir_entry *next_entry);
extern void restore_entry(struct ext2_group_desc *gd, struct ext2_inode *inodes, unsigned char *inode_bits, unsigned char *block_bits, struct ext2_dir_entry *dir_entry);
extern int shift_back_dir_entry(struct ext2_group_desc *gd, struct ext2_inode *inodes,unsigned char *inode_bits, unsigned char *block_bits, int b_num, char *name);
extern int check_arg_valid_restore(char *path_name_all[], int dir_num, char *name, struct ext2_inode *inodes, int *i_num, int *i_g_num);
//================================================================
extern void init_new_inode(struct ext2_inode *inodes, unsigned short mode, int inode_index, unsigned int size, int block_num);
extern void init_file_new_inode(struct ext2_inode *inodes, unsigned short mode, int inode_index, unsigned int size);
extern void init_dir_block(int b_num, int self_inode, int parent_i_num);
extern void setup_parent_block(struct ext2_group_desc *gd, unsigned char *block_bits, struct ext2_inode *inodes, int all_blocks, int i_num, int b_num, int inode_num, unsigned char type, char *name);
//==========================================================

#endif /* EX2_UTILS_H */