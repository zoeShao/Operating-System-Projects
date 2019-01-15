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

/**
* Return 1 or 0. If the return value equals 1, 
* it means the block is occupied. Vice versa.
*/
int occupied(unsigned char *bit_map, int index) {
    int which_byte = index / 8;
    int which_bit = index % 8;
    unsigned char mask = 1 << which_bit;
    return bit_map[which_byte] & mask;
}

/*
 * Returns the block num of a free inode.
 */
int get_free_block(unsigned char *bit_map, int num_inodes) {
    int inode_index;
    for (inode_index = 0; inode_index < num_inodes; inode_index++) {
        if (occupied(bit_map, inode_index) == 0) {
            return inode_index;
       }
    }
    return -1; // If no free block is avaliable.
}

/*
 * Returns the index num of a free inode.
 */
int get_free_inode(unsigned char *bit_map, int num_inodes) {
    int inode_index;
    for (inode_index = 0; inode_index < num_inodes; inode_index++) {
        if (occupied(bit_map, inode_index) == 0) {
            // check whether that inode number is a reserved inode or not.
            if (inode_index == 1 || inode_index >= 10) {
                return inode_index;
            }
       }
    }
    return -1; // There is no free inode.
}

/*
 * Extracts the dir name from the given path.
 */
char *get_dir_name(char *path, int *dir_num) {
    char *temp_path = malloc(strlen(path));
    strcpy(temp_path, path);
    char *token = strtok(temp_path, "/");
    char *next = token;
    int i = 0;
    while (next != NULL) {
        token = next;
        next = strtok(NULL, "/");
        i++;
    }
    *dir_num = i;
    return token;
}

/**
* Put each component of the path into an array of strings.
*/
void get_path_name(char *path, char *path_name[], int dir_num) {
    char *temp_path = malloc(strlen(path));
    strcpy(temp_path, path);
    char *token = strtok(temp_path, "/");
    int i = 0;
    while ((i < dir_num) && (token != NULL)) {
        path_name[i] = token;
        token = strtok(NULL, "/");
        i++;
    }
    return;
}

/**
* Check whether the given name is equal to the dir entry name.
*/
int check_same_name(char *disk_entry_name, char *path_name, int name_len) {
        char name1[name_len+1];
        strcpy(name1, disk_entry_name);
        name1[name_len]='\0';
        if (strcmp(path_name, name1) == 0) {
            return 0;
        }
    return 1;
}

/** (ext2_cp)
* Check the given dircetory path is valid or not
*/
int check_dir_valid_cp(int b_num, char *path_name[], int index, int dir_num, int *inode_num) {
    int byte_index = 0;
    // go to the beginning of the block.
    unsigned char *block = (unsigned char *) (disk + EXT2_BLOCK_SIZE*b_num);
    while (byte_index != EXT2_BLOCK_SIZE) {
        struct ext2_dir_entry *dir_entry = (struct ext2_dir_entry *) (block + byte_index);
        *inode_num = -1;
        // Check whether this entry is a directory or not.
        if ((dir_entry->file_type & EXT2_FT_DIR) != 0) {
            char* name = (char*) (block + byte_index + sizeof(struct ext2_dir_entry));
            char name1[dir_entry->name_len+1];
            strcpy(name1, name);
            name1[dir_entry->name_len]='\0';
            if (strcmp(path_name[index], name1) == 0) {
                *inode_num = dir_entry->inode;
                return 0;
            }
        }
        byte_index += dir_entry->rec_len;
    }
    return 1;
}

/** (ext2_cp)
* Do some error check for the given path. 
* 1. Check if any component on the path to the location 
*    where the final dir is to be created does not exist. --- ENOENT 
* 2. Check if the specified dir already exists --- EEXIST
*/
int check_path_valid_cp(char *path_name[], int dir_num, struct ext2_inode *inodes, int *i_num, int *i_p_num) {
    int error = 0; // Used to keep check the result of the check.
    int inode_num = -1; // If this subdirectory is valid, this variable stores the subdirectory's inode number.
    int index = 0; // It is the index of the array of path_name
    // Firstly, check the root directory (second inde).
    *i_num = EXT2_ROOT_INO; // Store the current inode number.
    *i_p_num = EXT2_ROOT_INO;
    int i;
    for (i = 0; i < inodes[*i_num-1].i_blocks/2; i++) {
        error = check_dir_valid_cp(inodes[*i_num-1].i_block[i], path_name, index, dir_num, &inode_num);
        if (error == 0) {
            break;
        }
    }
    if (error == 1) {
        fprintf(stderr, "'%s': No such file or directory.\n", path_name[index]);
        exit(ENOENT);
    }
    dir_num--;
    index++;
    if (inode_num != -1 && dir_num > 1) {
        *i_p_num = inode_num;
    }
    //Next, check the rest directories along the path.
    while((dir_num > 0) && (inode_num != -1) && (error == 0)) {
        *i_p_num = *i_num;
        *i_num = inode_num; // Store the current inode number.
        int j;
        for (j = 0; j < inodes[*i_num-1].i_blocks/2; j++) {
            error = check_dir_valid_cp(inodes[*i_num-1].i_block[j], path_name, index, dir_num, &inode_num);
            if (error == 0) {
                break;
            }
        }
        if (error == 1) {
            fprintf(stderr, "'%s': No such file or directory.\n", path_name[index]);
            exit(ENOENT);
        }
        index++;
        dir_num--;
    }
    *i_p_num = *i_num;
    *i_num = inode_num; // Store the current inode number.
    return error;
}

/** (ext2_cp)
* Check the last component of the absolute path is a directory or a file.
* Store the state in variable type: 1:dir; 2:file or symbolic link.
* If we find the last component on the disk, return 0; if not find, return 1.
*/
int check_type(struct ext2_inode *inodes, int *i_p_num, char *path_name, int *type, int *i_num, int *i_g_num) {
    int i;
    for (i = 0; i < inodes[*i_p_num-1].i_blocks/2; i++) {
        int b_num = inodes[*i_p_num-1].i_block[i];
        int byte_index = 0;
        unsigned char *block = (unsigned char *) (disk + EXT2_BLOCK_SIZE*b_num);
        while (byte_index != EXT2_BLOCK_SIZE) {
            struct ext2_dir_entry *dir_entry = (struct ext2_dir_entry *) (block + byte_index);
            char* name = (char*) (block + byte_index + sizeof(struct ext2_dir_entry));
            if ((dir_entry->file_type & EXT2_FT_REG_FILE) != 0) {
                if (check_same_name(name, path_name, dir_entry->name_len) == 0) {
                    if (dir_entry->inode == 0) {
                        return 1;
                    }
                    *i_p_num = dir_entry->inode;
                    *type = 2;
                    *i_num = -1;
                    return 0;
                }
            } else if ((dir_entry->file_type & EXT2_FT_DIR) != 0) {
                if (check_same_name(name, path_name, dir_entry->name_len) == 0) {
                    *i_p_num = dir_entry->inode;
                    *type = 1;
                    *i_g_num = *i_num;
                    *i_num = dir_entry->inode;
                    return 0;
                }
            } 
            byte_index += dir_entry->rec_len;
        }
    }
    return 1;
}

/** (ext2_cp)
* Case 1: If the last component's name of the absolute path is not the same of the file name, 
*        and it does not exist in the disk, we cp the file to that path and name it as 
*        the last component's name.
* Case 2: If we find the last component of the absolute path is a file or a directory that 
*        is on the disk, we need to deal with it.
*/
int check_current_inode(int result, int file_type, struct ext2_inode *inodes, char *path_name, int i_num, int *name_bit) {
    // Case 1:
    if (result == 1) {
        *name_bit = 1;
        return 0;
    // Case 2: 
    } else {
        // Case 2.1: If the last component of the absolute path is a file, 
        // we cannot cp a file to a path which leads to a file on disk.
        if (file_type == 2) {
            fprintf(stderr, "File already exists.\n");
            exit(EEXIST);
        } else if (file_type == 1) {
            // Case 2.2: If the last component of the absolute path is a dir, 
            // we need to check whether there is a file or a dir that has the same name in that dir.
            int i;
            for (i = 0; i < inodes[i_num-1].i_blocks/2; i++) {
                int b_num = inodes[i_num-1].i_block[i];
                int byte_index = 0;
                unsigned char *block = (unsigned char *) (disk + EXT2_BLOCK_SIZE*b_num);
                while (byte_index != EXT2_BLOCK_SIZE) {
                    struct ext2_dir_entry *dir_entry = (struct ext2_dir_entry *) (block + byte_index);
                    char* name = (char*) (block + byte_index + sizeof(struct ext2_dir_entry));
                    if (check_same_name(name, path_name, dir_entry->name_len) == 0) {
                        if ((dir_entry->file_type & EXT2_FT_REG_FILE) != 0) {
                            fprintf(stderr, "File already exists.\n");
                            exit(EEXIST); 
                        } else if ((dir_entry->file_type & EXT2_FT_DIR) != 0) {
                            fprintf(stderr, "There is a directory which has the same name as the file.\n");
                            exit(EEXIST);
                        }
                    }
                    byte_index += dir_entry->rec_len;
                }
            }
            // We did not find, so in this case, we can cp.
            *name_bit = 0;
            return 0;
        }
    }
    return 0;
}

/** (ext2_cp)
* Check the given arguments is valid or not.
*/
int check_arg_valid(struct ext2_inode *inodes, char *path_name_all[], int dir_num, char *name, char *file_name, int *i_num, int *i_g_num, int *name_bit) {
    int i_p_num; // Used to store current inode number.
    int error = 0; // Used to store the value of the error.
    int result = 0;  // Used to store the result of finding the last component on the disk.
    int file_type = 1; // Used to store the type of that entry.
    // Case 1: the given third argument is root.
    if (dir_num == 0) {
        *i_num = EXT2_ROOT_INO;
        *i_g_num = EXT2_ROOT_INO;
        error = check_current_inode(result, file_type, inodes, file_name, *i_num, name_bit);
    // Case 2: the given third argument contains 2 parts. 
    //         (root and the last component)
    } else if (dir_num == 1) {
        i_p_num = EXT2_ROOT_INO;
        *i_num = i_p_num;
        *i_g_num = EXT2_ROOT_INO;
        result = check_type(inodes, &i_p_num, name, &file_type, i_num, i_g_num);
        error = check_current_inode(result, file_type, inodes, file_name, *i_num, name_bit);
    // Case 3: the given third argument contains more than 2 parts. 
    //         (the directory along the path and the last component of the path)
    } else {
        char *path_name[dir_num-1];
        int i;
        for (i = 0; i < dir_num - 1; i++) {
            path_name[i] = path_name_all[i];
        }
        error = check_path_valid_cp(path_name, dir_num-1, inodes, &i_p_num, i_g_num);
        *i_num = i_p_num;
        if (error == 0) {
            result = check_type(inodes, &i_p_num, name, &file_type, i_num, i_g_num);
            error = check_current_inode(result, file_type, inodes, file_name, *i_num, name_bit);
        }
    }
    return error;
}

/** (ext2_ln)
* Check the given component is a file or not.
* If it is not the last component of a path, then it should only be directory.
*/
int check_file_valid(int b_num, char *path_name[], int index, int dir_num, int *inode_num) {
    int byte_index = 0;
    // go to the beginning of the block.
    unsigned char *block = (unsigned char *) (disk + EXT2_BLOCK_SIZE*b_num);
    while (byte_index != EXT2_BLOCK_SIZE) {
        struct ext2_dir_entry *dir_entry = (struct ext2_dir_entry *) (block + byte_index);
        *inode_num = -1;
        char* name = (char*) (block + byte_index + sizeof(struct ext2_dir_entry));
        if (check_same_name(name, path_name[index], dir_entry->name_len) == 0) {
            if ((dir_entry->file_type & EXT2_FT_REG_FILE) != 0) {
                if (dir_num == 1) {
                    if (dir_entry->inode == 0) {
                        fprintf(stderr, "'%s': (Source) file or this directroy does not exists.\n", path_name[index]);
                        exit(ENOENT);
                    }
                    *inode_num = dir_entry->inode;
                    return 0;
                } else {
                    fprintf(stderr, "'%s': No such directory.\n", path_name[index]);
                    exit(ENOENT);
                }
            } else if ((dir_entry->file_type & EXT2_FT_DIR) != 0) {
                if (dir_num == 1) {
                    fprintf(stderr, "This operation does not allowed for directory.\n");
                    exit(EISDIR);
                } else {
                    *inode_num = dir_entry->inode;
                    return 0;
                }
            }
        }
    byte_index += dir_entry->rec_len;
    }
    return 1;
}

/** (ext2_ln)
* Check the given path is valid or not.
*/
int check_path_valid_ln(char *path_name[], int dir_num, struct ext2_inode *inodes, int *i_num) {
    int error = 0; // Used to keep check the result of the check.
    int inode_num = -1; // If this subdirectory is valid, this variable stores the subdirectory's inode number.
    int index = 0; // It is the index of the array of path_name
    *i_num = EXT2_ROOT_INO; // Store the parent inode number.
    int i;
    for (i = 0; i < inodes[*i_num-1].i_blocks/2; i++) {
        error = check_file_valid(inodes[*i_num-1].i_block[i], path_name, index, dir_num, &inode_num);
        if (error == 0) {
            break;
        }
    }
    if (error == 1) {
        fprintf(stderr, "'%s': (Source) file or this directroy does not exists.\n", path_name[index]);
        exit(ENOENT);
    }
    dir_num--;
    index++;
    // Next, check the rest directories along the path.
    while((dir_num > 0) && (inode_num != -1) && (error == 0)) {
        *i_num = inode_num; // Store the current inode number.
        int j;
        for (j = 0; j < inodes[*i_num-1].i_blocks/2; j++) {
            error = check_file_valid(inodes[*i_num-1].i_block[j], path_name, index, dir_num, &inode_num);
            if (error == 0) {
                break;
            }
        }
        if (error == 1) {
            fprintf(stderr, "'%s': (Source) file or this directroy does not exists.\n", path_name[index]);
            exit(ENOENT);
        }
        index++;
        dir_num--;
    }
    *i_num = inode_num; // Store the current inode number.
    return error;
}

/** (ext2_mkdir)
* Check the given component can be created as a directory or not.
*/
int check_dir_valid(int b_num, char *path_name[], int index, int dir_num, int *inode_num) {
    int byte_index = 0;
    // go to the beginning of the block.
    unsigned char *block = (unsigned char *) (disk + EXT2_BLOCK_SIZE*b_num);
    while (byte_index != EXT2_BLOCK_SIZE) {
        struct ext2_dir_entry *dir_entry = (struct ext2_dir_entry *) (block + byte_index);
        *inode_num = -1;
        char* name = (char*) (block + byte_index + sizeof(struct ext2_dir_entry));
        if (check_same_name(name, path_name[index], dir_entry->name_len) == 0) {
            if ((dir_entry->file_type & EXT2_FT_REG_FILE) != 0) {
                if (dir_num == 1) {
                    if (dir_entry->inode == 0) {
                        *inode_num = dir_entry->inode;
                        return 0;
                    }
                    fprintf(stderr, "Cannot make the directory: File exists.\n");
                    exit(EEXIST);
                } else {
                    fprintf(stderr, "'%s': No such directory.\n", path_name[index]);
                    exit(ENOENT);
                }
            } else if ((dir_entry->file_type & EXT2_FT_DIR) != 0) {
                // Case 1: the specified dir already exists
                if (dir_num == 1) {
                    fprintf(stderr, "Cannot make the directory, Since it already exists.\n");
                    exit(EEXIST);
                } else {
                    *inode_num = dir_entry->inode;
                    return 0;
                }
            }
        }
    byte_index += dir_entry->rec_len;
    }
    return 1;
}

/** (ext2_mkdir)
* 1. Check if any component on the path to the location 
*    where the final dir is to be created does not exist. --- ENOENT 
* 2. Check if the specified dir already exists --- EEXIST
*/
int check_path_valid(char *path_name[], int dir_num, struct ext2_inode *inodes, int *i_num, int *i_p_num) {
    int error = 0; // Used to keep check the result of the check.
    int inode_num = -1; // If this subdirectory is valid, this variable stores the subdirectory's inode number.
    int index = 0; // It is the index of the array of path_name
    *i_num = EXT2_ROOT_INO; // Store the parent inode number.
    *i_p_num = EXT2_ROOT_INO; // Store the grand parent inode number.
    int i;
    for (i = 0; i < inodes[*i_num-1].i_blocks/2; i++) {
        error = check_dir_valid(inodes[*i_num-1].i_block[i], path_name, index, dir_num, &inode_num);
        if (error == 0) {
            break;
        }
    }
    if (error == 1 && dir_num > 1) {
        fprintf(stderr, "'%s': No such file or directory.\n", path_name[index]);
        exit(ENOENT);
    }
    error = 0;
    dir_num--;
    index++;
    if (inode_num != -1 && dir_num > 1) {
        *i_p_num = inode_num;
    }
    // Next, check the rest directories along the path.
    while((dir_num > 0) && (inode_num != -1) && (error == 0)) {
        *i_p_num = *i_num;
        *i_num = inode_num; // Store the parent inode number.
        int j;
        for (j = 0; j < inodes[*i_num-1].i_blocks/2; j++) {
            error = check_dir_valid(inodes[*i_num-1].i_block[j], path_name, index, dir_num, &inode_num);
            if (error == 0) {
                break;
            }
        }
        if (error == 1 && dir_num > 1) {
            fprintf(stderr, "'%s': No such file or directory.\n", path_name[index]);
            exit(ENOENT);
        }
        error = 0;
        index++;
        dir_num--;
    }
    return error;
}

/** (ext2_rm)
* Check the given path can be used for removing a file.
*/
int check_path_valid_rm(char *path_name[], int dir_num, struct ext2_inode *inodes, int *f_num, int *i_num, int *i_p_num) {
    int error = 0; // Used to keep check the result of the check.
    int inode_num = -1; // If this subdirectory is valid, this variable stores the subdirectory's inode number.
    int index = 0; // It is the index of the array of path_name
    *f_num = -1;
    *i_num = EXT2_ROOT_INO; // Store the parent inode number.
    *i_p_num = EXT2_ROOT_INO; // Store the grand parent inode number.
    int i;
    for (i = 0; i < inodes[*i_num-1].i_blocks/2; i++) {
        error = check_file_valid(inodes[*i_num-1].i_block[i], path_name, index, dir_num, &inode_num);
        if (error == 0) {
            break;
        }
    }
    if (error == 1) {
        fprintf(stderr, "'%s': (Source) file or this directroy does not exists.\n", path_name[index]);
        exit(ENOENT);
    }
    dir_num--;
    index++;
    if (inode_num != -1 && dir_num > 1) {
        *i_p_num = inode_num;
    }
    // Next, check the rest directories along the path.
    while((dir_num > 0) && (inode_num != -1) && (error == 0)) {
        *i_p_num = *i_num;
        *i_num = inode_num; // Store the parent inode number.
        *f_num = inode_num;
        int j;
        for (j = 0; j < inodes[*i_num-1].i_blocks/2; j++) {
            error = check_file_valid(inodes[*i_num-1].i_block[j], path_name, index, dir_num, &inode_num);
            if (error == 0) {
                break;
            }
        }
        if (error == 1) {
            fprintf(stderr, "'%s': (Source) file or this directroy does not exists.\n", path_name[index]);
            exit(ENOENT);
        }
        index++;
        dir_num--;
    }
    *f_num = inode_num; // Store the current inode number.
    return error;
}

/** (ext2_rm)
* When removing a file, we are not actually remove it from the file system. We shift the entry, and add its rec_len to
*/
void shift_dir_entry(int b_num, char *name) {
    int byte_index = 0;
    int cur_len;
    // go to the beginning of the block.
    unsigned char *block = (unsigned char *) (disk + EXT2_BLOCK_SIZE*b_num);
    struct ext2_dir_entry *before_entry = (struct ext2_dir_entry *) (block + byte_index);
    struct ext2_dir_entry *cur_entry = (struct ext2_dir_entry *) (block + byte_index);
    while (byte_index != EXT2_BLOCK_SIZE) {
        before_entry = cur_entry;
        cur_entry = (struct ext2_dir_entry *) (block + byte_index);
        char *entry_name = (char*) (block + byte_index + sizeof(struct ext2_dir_entry));
        if (check_same_name(entry_name, name, cur_entry->name_len) == 0) {
            if ((cur_entry->file_type & EXT2_FT_REG_FILE) != 0) {
                if (byte_index == 0) {
                    cur_entry->inode = 0; // If removing the first entry of the block, set its inode to zero.
                } else {
                    cur_len = cur_entry->rec_len;
                    before_entry->rec_len += cur_len;
                }
            }
        }
        byte_index += cur_entry->rec_len;
    }
}

/** (ext2_restore)
* A helper function for shift_back_dir_entry.
* This function just return the actual length of a directory entry.
*/
int get_correct_len(struct ext2_dir_entry *dir_entry) {
    int actual_len;
    if (dir_entry->name_len % 4 != 0) {
        actual_len = 12 + dir_entry->name_len - (dir_entry->name_len % 4);
    } else {
        actual_len = 8 + dir_entry->name_len;
    }
    return actual_len;
}

/** (ext2_restore)
* A helper function for shift_back_dir_entry.
* This function helps to check whether there is an entry between these gaps.
*/
int find_entry_exit(int b_num, char *name) {
    int byte_index = 0;
    // go to the beginning of the block.
    unsigned char *block = (unsigned char *) (disk + EXT2_BLOCK_SIZE*b_num);
    struct ext2_dir_entry *dir_entry = (struct ext2_dir_entry *) (block + byte_index);
    while (byte_index != EXT2_BLOCK_SIZE) {
        dir_entry = (struct ext2_dir_entry *) (block + byte_index);
        char *entry_name = (char*) (block + byte_index + sizeof(struct ext2_dir_entry));
        if (check_same_name(entry_name, name, dir_entry->name_len) == 0) {
                    return 0;
        }
        byte_index += dir_entry->rec_len;
    }
    return 1;
}


/** (ext2_restore)
* A helper function for restore entry.
* This function check whether the inode or the data block of the file has been used.
*/
int check_can_restore(struct ext2_inode *inodes, unsigned char *inode_bits, unsigned char *block_bits, struct ext2_dir_entry *next_entry) {
    if (next_entry->inode == 0) {
        fprintf(stderr, "Failed to restore this file.\n");
        exit(ENOENT);
    }
    // Check wether this entry's inode has been used or not.
    if (occupied(inode_bits, next_entry->inode-1) == 1) {
        fprintf(stderr, "Failed to restore this file.\n");
        exit(ENOENT);
    }
    // Check whether its data block has been overwritten or not.
    int i;
    int blocks_num;
    blocks_num = inodes[next_entry->inode-1].i_blocks/2;
    if (blocks_num > 12) {
        blocks_num = blocks_num - 1;
    }
    for (i = 0; i < blocks_num; i++) {
        if (i < 12) {
            if (occupied(block_bits, inodes[next_entry->inode-1].i_block[i]-1) == 1) {
                fprintf(stderr, "Failed to restore this file. It's content has been overwritten.\n");
                exit(ENOENT);
            }
        } else {
            if (i == 12) {
                int indirect_num = inodes[next_entry->inode-1].i_block[12];
                if (occupied(block_bits, indirect_num-1) == 1) {
                    fprintf(stderr, "Failed to restore this file. It's content has been overwritten.\n");
                    exit(ENOENT);
                }
            }
            int byte_index = (i - 12)*4;
            // go to the beginning of the block.
            unsigned char *block = (unsigned char *) (disk + EXT2_BLOCK_SIZE*inodes[next_entry->inode-1].i_block[12]);
            unsigned char *data_b_num = (unsigned char *) (block + byte_index);
            if (occupied(block_bits, *data_b_num-1) == 1) {
                fprintf(stderr, "Failed to restore this file. It's content has been overwritten.\n");
                exit(ENOENT);
            }
        } 
    }
    return 0;
}

/** (ext2_restore)
* A helper function for shift_back_dir_entry.
* This function set its inode back and restore the data content.
*/
void restore_entry(struct ext2_group_desc *gd, struct ext2_inode *inodes, unsigned char *inode_bits, unsigned char *block_bits, struct ext2_dir_entry *dir_entry) {
    check_can_restore(inodes, inode_bits, block_bits, dir_entry);
    gd->bg_free_inodes_count--;
    inode_bits[(dir_entry->inode-1)/8] |= (1 << ((dir_entry->inode - 1) % 8));
    inodes[dir_entry->inode-1].i_dtime = 0;
    inodes[dir_entry->inode-1].i_links_count = 1;
    int i;
    int blocks_num = inodes[dir_entry->inode-1].i_blocks/2;
    if (blocks_num > 12) {
        blocks_num -= 1;
    }
    for (i = 0; i < blocks_num; i++) {
        if (i < 12) {
            block_bits[(inodes[dir_entry->inode-1].i_block[i]-1)/8] |= (1 << (inodes[dir_entry->inode-1].i_block[i] - 1) % 8); // Update bit-map.
            gd->bg_free_blocks_count--; // Update block-group-descriptor.
        } else {
            if (i == 12) {
                int indirect_num = inodes[dir_entry->inode-1].i_block[12];
                block_bits[(indirect_num-1)/8] |= (1 << (indirect_num - 1) % 8); // Update bit-map.
                gd->bg_free_blocks_count--; // Update block-group-descriptor.
            }
            int byte_index = (i - 12)*4;
            // go to the beginning of the block.
            unsigned char *block = (unsigned char *) (disk + EXT2_BLOCK_SIZE*inodes[dir_entry->inode-1].i_block[12]);
            unsigned char *data_b_num = (unsigned char *) (block + byte_index);
            block_bits[(*data_b_num-1)/8] |= (1 << (*data_b_num-1) % 8); // Update bit-map
            gd->bg_free_blocks_count--; // Update block-group-descriptor.
        }
    }
    return;
}

/** (ext2_restore)
* Go through the dirctory block, search for gaps. Check the name is match or not,
* and do content restore.
*/
int shift_back_dir_entry(struct ext2_group_desc *gd, struct ext2_inode *inodes,unsigned char *inode_bits, unsigned char *block_bits, int b_num, char *name) {
    int byte_index = 0;
    int correct_len;
    // go to the beginning of the block.
    unsigned char *block = (unsigned char *) (disk + EXT2_BLOCK_SIZE*b_num);
    while(byte_index != EXT2_BLOCK_SIZE) {
        struct ext2_dir_entry *cur_entry = (struct ext2_dir_entry *) (block + byte_index);
        correct_len = get_correct_len(cur_entry);
        unsigned char *entry_start = block + byte_index;
        unsigned char *entry_end = entry_start + cur_entry->rec_len;
        unsigned char *actual_end = (unsigned char *) (block + correct_len);
        while (actual_end < entry_end) {
            struct ext2_dir_entry *next_entry = (struct ext2_dir_entry *) (block + byte_index);
            char *next_name = (char*) (block + byte_index + sizeof(struct ext2_dir_entry));
            if (next_entry->name_len == 0) {
                return ENOENT;
            }
            // If we find the entry with the name provided we need to restore the file.
            if (check_same_name(next_name, name, next_entry->name_len) == 0) {
                restore_entry(gd, inodes, inode_bits, block_bits, next_entry);
                int len;
                len = ((unsigned char *) next_entry) - ((unsigned char *) cur_entry);
                next_entry->rec_len = cur_entry->rec_len - len;
                cur_entry->rec_len = len;
                return 0;
            } else {
                if (find_entry_exit(b_num, next_name) == 0) {
                    cur_entry = (struct ext2_dir_entry *) (block + byte_index);
                }
                byte_index += get_correct_len(next_entry);
            }
            
        }
        byte_index += cur_entry->rec_len; 
    }
    return ENOENT;
}

/** (ext2_restore)
* Check the given argument is valid to restore. It cannot be a directory
* Case 1: the given third argument contains 2 parts.(root and the last component)
* Case 2: the given third argument contains more than 2 parts. 
* (the directory along the path and the last component of the path)
*/
int check_arg_valid_restore(char *path_name_all[], int dir_num, char *name, struct ext2_inode *inodes, int *i_num, int *i_g_num) {
    int error = 0;
    int i_p_num; // Used to store current inode number.
    int result = 0;  // Used to store the result of finding the last component on the disk.
    int file_type = 1; // Used to store the type of that entry.
    // Case 1: 
    if (dir_num == 1) {
        i_p_num = EXT2_ROOT_INO;
        *i_num = i_p_num;
        *i_g_num = EXT2_ROOT_INO;
        result = check_type(inodes, &i_p_num, name, &file_type, i_num, i_g_num);
        if (result == 0) {
            if (file_type == 1) {
                fprintf(stderr, "Can't restore a directory.\n");
                exit(EISDIR);
            } else if (file_type == 2) {
                if (i_p_num == 0) {
                    fprintf(stderr, "Cannot restore this entry.\n");
                    exit(ENOENT);
                }
                fprintf(stderr, "Invalid path.\n");
                exit(ENOENT);
            }
        }
    // Case 2: 
    } else {
        char *path_name[dir_num-1];
        int i;
        for (i = 0; i < dir_num - 1; i++) {
            path_name[i] = path_name_all[i];
        }
        error = check_path_valid_cp(path_name, dir_num-1, inodes, &i_p_num, i_g_num);
        *i_num = i_p_num;
        if (error == 0) {
            result = check_type(inodes, &i_p_num, name, &file_type, i_num, i_g_num);
            if (result == 0) {
                if (file_type == 1) {
                    fprintf(stderr, "Can't restore a directory.\n");
                    exit(EISDIR);
                } else if (file_type == 2) {
                    if (i_p_num == 0) {
                        fprintf(stderr, "Cannot restore this entry.\n");
                        exit(ENOENT);
                    }
                    fprintf(stderr, "Invalid path.\n");
                    exit(ENOENT);
                }
            }
        }
    }
    return error;
}

/** (ext2_mkdir)
* When create a new inode, we need to init it.
*/
void init_new_inode(struct ext2_inode *inodes, unsigned short mode, int inode_index, unsigned int size, int block_num) {
    inodes[inode_index].i_mode = mode;
    inodes[inode_index].i_size = size;
    inodes[inode_index].i_uid = 0;
    inodes[inode_index].i_gid = 0;
    inodes[inode_index].i_dtime = 0;
    inodes[inode_index].i_links_count = 2;
    inodes[inode_index].i_blocks = size/512;
    inodes[inode_index].osd1 = 0;
    int i ;
    for (i = 0; i < inodes[inode_index].i_blocks/2; i++) {
        inodes[inode_index].i_block[i] = block_num;
        block_num++;
    }
    inodes[inode_index].i_generation = 0;
    inodes[inode_index].i_file_acl = 0;
    inodes[inode_index].i_dir_acl = 0;
    inodes[inode_index].i_faddr = 0;
    for (i = 0; i < 3; i++) {
        inodes[inode_index].extra[i] = 0;
    }
}

/** (ext2_cp)
* When create a new file inode, we need to init it.
*/
void init_file_new_inode(struct ext2_inode *inodes, unsigned short mode, int inode_index, unsigned int size) {
    inodes[inode_index].i_mode = mode;
    inodes[inode_index].i_size = size;
    inodes[inode_index].i_uid = 0;
    inodes[inode_index].i_dtime = 0;
    inodes[inode_index].i_gid = 0;
    inodes[inode_index].i_links_count = 1;
    if (size % 512 == 0) {
        inodes[inode_index].i_blocks = size/512;
    } else {
        inodes[inode_index].i_blocks = (size/1024) * 2 + 2;
    }
    inodes[inode_index].osd1 = 0;
    int i ;
    for (i = 0; i < inodes[inode_index].i_blocks/2; i++) {
        inodes[inode_index].i_block[i] = 0;
    }
    inodes[inode_index].i_generation = 0;
    inodes[inode_index].i_file_acl = 0;
    inodes[inode_index].i_dir_acl = 0;
    inodes[inode_index].i_faddr = 0;
    for (i = 0; i < 3; i++) {
        inodes[inode_index].extra[i] = 0;
    }
}

/** (ext2_mkdir)
* When create a new directory block, we need to init it.
*/
void init_dir_block(int b_num, int self_inode, int parent_i_num) {
    unsigned char *new_block = (unsigned char *) (disk + b_num*EXT2_BLOCK_SIZE);
    int byte_index = 0;
    struct ext2_dir_entry *new_dir_entry = (struct ext2_dir_entry *) (new_block + byte_index);
    new_dir_entry->inode = self_inode;
    new_dir_entry->rec_len = 12;
    new_dir_entry->name_len = 1;
    new_dir_entry->file_type = EXT2_FT_DIR;
    strcpy(new_dir_entry->name, ".");
    byte_index += new_dir_entry->rec_len;
    new_dir_entry = (struct ext2_dir_entry *) (new_block + byte_index);
    new_dir_entry->inode = parent_i_num;
    new_dir_entry->rec_len = EXT2_BLOCK_SIZE - byte_index;
    new_dir_entry->name_len = 2;
    new_dir_entry->file_type = EXT2_FT_DIR;
    strcpy(new_dir_entry->name, "..");
}

/** 
* When added a new dirctory entry into the file system, we need to update its parent block. 
* There maybe some cases that when putting a lot of files or subdirctories into the parent directory,
* it may span more than one block.
*/
void setup_parent_block(struct ext2_group_desc *gd, unsigned char *block_bits, struct ext2_inode *inodes, int all_blocks, int i_num, int b_num, int inode_num, unsigned char type, char *name) {
    unsigned char *block = (unsigned char *) (disk + b_num*EXT2_BLOCK_SIZE);
    int byte_index = 0;
    int last_rec_len;
    int new_len;
    int last_space;
    int cur_len;
    struct ext2_dir_entry *cur_entry = NULL;
    while (byte_index != EXT2_BLOCK_SIZE) {
        cur_entry = (struct ext2_dir_entry *) (block + byte_index);
        last_rec_len = cur_entry->rec_len;
        byte_index += cur_entry->rec_len;
    }
    if (strlen(name) % 4 != 0) {
        new_len = 12 + strlen(name) - (strlen(name) % 4);
    } else {
        new_len = 8 + strlen(name);
    }
    if (cur_entry->name_len % 4 != 0) {
        cur_len = 12 + cur_entry->name_len - (cur_entry->name_len % 4);
    } else {
        cur_len = 8 + cur_entry->name_len;
    }
    last_space = last_rec_len - cur_len;
    if (new_len <= last_space) {
        cur_entry->rec_len = cur_len;
        byte_index = byte_index - last_rec_len + cur_entry->rec_len;
        cur_entry = (struct ext2_dir_entry *) (block + byte_index);
        cur_entry->file_type = type;
        cur_entry->inode = inode_num;
        cur_entry->rec_len = EXT2_BLOCK_SIZE - byte_index;
        cur_entry->name_len = strlen(name);
        strcpy(cur_entry->name, name);
    } else {
        int new_block_num = get_free_block(block_bits, all_blocks) + 1;
        if (new_block_num == -1) {
            exit(ENOSPC);
        }
        block_bits[(new_block_num-1)/8] |= (1 << (new_block_num - 1) % 8);
        gd->bg_free_blocks_count--;
        inodes[i_num-1].i_blocks += 2;
        inodes[i_num-1].i_block[(inodes[i_num-1].i_blocks/2) - 1] = new_block_num;
        unsigned char *new_block = (unsigned char *) (disk + new_block_num*EXT2_BLOCK_SIZE);
        struct ext2_dir_entry *new_dir_entry = (struct ext2_dir_entry *) (new_block);
        new_dir_entry->inode = inode_num;
        new_dir_entry->rec_len = 1024;
        new_dir_entry->name_len = strlen(name);
        new_dir_entry->file_type = type;
        strcpy(new_dir_entry->name, name);
    }
}