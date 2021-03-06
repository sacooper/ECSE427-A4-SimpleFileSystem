#include "sfs_api.h"
#include "disk_emu.h"
#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
/************************************************
ECSE 427 / COMP 310 - Operating Systems
SCOTT COOPER
260503452

Programming Assignment 2 - Simple File System
************************************************/


// Important block locations
#define SUPERBLOCK 0
#define FREE_LIST 1
#define ROOT_LOC 2
#define ROOT_SIZE 20
#define FAT_LOC ROOT_LOC+ROOT_SIZE
#define FAT_SIZE 4
#define DATA_START FAT_LOC+FAT_SIZE
// block size in bytes
#define BLOCKSIZE 2048

// super + free sector list + root + FAT_LOC
#define NUMBLOCKS 1+BLOCKSIZE+ROOT_SIZE+FAT_SIZE

// Assume at most 12 characters for filename
#define MAX_FNAME_LENGTH 12

// filename for file system
#define FILENAME "my.sfs"

typedef struct directory_entry {
    char name[MAX_FNAME_LENGTH + 1];
    unsigned short indx;
    unsigned int size;
} directory_entry;

typedef struct FAT_entry {
    unsigned short data;
    unsigned short next;
} FAT_entry;

typedef struct file_descriptor {
    unsigned int read_ptr;
    unsigned int write_ptr;
    unsigned int size;
    unsigned short start;
} file_descriptor;

int filesOpen;
directory_entry *root_directory;
file_descriptor **file_descriptor_table;
FAT_entry *FAT;

int first_open();
void set_used(unsigned short indx);
void set_unused(unsigned short indx);

int mksfs(int fresh){
    if (fresh){
        // Check if file system currently exists, and delete it if it does
        if( access( FILENAME, F_OK ) != -1 ) {
            unlink(FILENAME);}

        // Create a new disk
        if (init_fresh_disk(FILENAME, BLOCKSIZE, NUMBLOCKS)!=0){
            fprintf(stderr, "Cannot create fresh filesystem");
            return -1;
        }

        // Create super block
        int *super_buff = malloc(BLOCKSIZE);

        if (!super_buff){
            fprintf(stderr, "Error creating super block");
            return -1;}

        super_buff[0] = BLOCKSIZE;  // Size of each block
        super_buff[1] = NUMBLOCKS;  // Number of blocks on disk (including super)
        super_buff[1] = FREE_LIST;  // Block containing free list
        super_buff[2] = ROOT_LOC;       // Location of 1st block of root directory
        super_buff[4] = ROOT_SIZE;  // Number of for root directory
        super_buff[5] = FAT_LOC;        // Location of 1st block of FAT_LOC
        super_buff[6] = FAT_SIZE;   // Number of blocks for FAT_LOC
        super_buff[7] = DATA_START; // Location of 1st block of user data

        write_blocks(SUPERBLOCK, 1, super_buff);
        free(super_buff);

        // Create free lest
        unsigned int *free_buff = malloc(BLOCKSIZE);
        if (!free_buff){
            fprintf(stderr, "Error initialize free sector list");
            return -1;}

        int i;

        // Fill array with 1s to signify not used data blocks (1 = available)
        for (i = 0; i < BLOCKSIZE/sizeof(unsigned int); i++){
            free_buff[i] = ~0;}

        write_blocks(FREE_LIST, 1, free_buff);
        free(free_buff);

        // Create root directory
        directory_entry *new_root_buff = malloc(BLOCKSIZE*sizeof(directory_entry));

        if (!new_root_buff){
            fprintf(stderr, "Error initializing root directory");
            return -1;}

        for (i = 0; i < BLOCKSIZE*ROOT_SIZE/sizeof(directory_entry); i++)
            new_root_buff[i] = (directory_entry) {.name = "\0", .size=0, .indx = BLOCKSIZE };

        write_blocks(ROOT_LOC, ROOT_SIZE, new_root_buff);
        free(new_root_buff);

        // Create File Allocation Table
        FAT_entry *new_fat_table = malloc(BLOCKSIZE * sizeof(FAT_entry));

        if (!new_fat_table){
            fprintf(stderr, "Error intitializing FAT_LOC");
            return -1;}

        for (i = 0; i < BLOCKSIZE; i++){
            new_fat_table[i].data = BLOCKSIZE;
            new_fat_table[i].next = BLOCKSIZE;}

        write_blocks(FAT_LOC, FAT_SIZE, new_fat_table);
        free(new_fat_table);

    } else {
        // Open disk before initialize data structures
        if (init_disk(FILENAME, BLOCKSIZE, NUMBLOCKS) != 0){
            fprintf(stderr, "Error in opening disk");
            return -1;
        }
    }

    int *super_block = malloc(BLOCKSIZE);
    if (!super_block){
        fprintf(stderr, "Error reading super block");
        return -1;}

    read_blocks(SUPERBLOCK, 1, super_block);

    // Initialize variables
    filesOpen = 0;

    root_directory = malloc(sizeof(directory_entry) * BLOCKSIZE);
    FAT = malloc(sizeof(FAT_entry) * BLOCKSIZE);

    if (!root_directory || !FAT){
        fprintf(stderr, "Error in malloc at mksfs");
        exit(1);}

    read_blocks(ROOT_LOC, ROOT_SIZE, root_directory);
    read_blocks(FAT_LOC, FAT_SIZE, FAT);

    return 0;
}

void sfs_ls(void){
    if (!root_directory){   // Check file system is initialized
        fprintf(stderr,
            "Error in sfs_ls.\nFile system neads to be initialized first");
        return;}

    int i;

    for (i = 0; i < BLOCKSIZE; i++){        // Print out files and sizes
        if (strncmp(root_directory[i].name, "\0", 1) != 0){
            printf("%12s: %d\n", root_directory[i].name, root_directory[i].size);
        }
    }
}

int sfs_fopen(char *name){
    // Check name is valid
    if (strlen(name) > MAX_FNAME_LENGTH)
        return -1;

    // Make sure file system has been initialized
    if (!root_directory){
        fprintf(stderr,
            "Error in sfs_fopen.\nFile system neads to be opened first");
        return -1;}

    int i;

    // Check if file exists
    for (i = 0; i < BLOCKSIZE; i++){
        if (strcmp(root_directory[i].name, name) == 0){
            int fd = -1, j;

            // Make sure we haven't already opened this file.
            // If so, return the original file descriptor
            for (j = 0; j < filesOpen; j++){
                if (file_descriptor_table[j] && root_directory[i].indx == file_descriptor_table[j]->start)
                    return j;
            }

            // Otherwise, create an FD for that file, assuming we have room
            for (j = 0; j < filesOpen; j++){
                if (!file_descriptor_table[j]){
                    file_descriptor_table[j] = malloc(sizeof(file_descriptor));
                    fd = j;
                    break;
                }
            }

            // If we don't have room, make room
            if (fd == -1){
                file_descriptor_table = realloc(file_descriptor_table, (1+filesOpen)*(sizeof(file_descriptor *)));
                file_descriptor_table[filesOpen] = (file_descriptor *) malloc(sizeof(file_descriptor));
                fd = filesOpen++;
            }

            // Initialize file descriptor with correct info
            file_descriptor *new = file_descriptor_table[fd];
            if (!new){
                fprintf(stderr, "Error opening %12s", name);
                return -1;}

            new->read_ptr = 0;
            new->write_ptr = root_directory[i].size;
            new->size = root_directory[i].size;
            new->start = root_directory[i].indx;
            return fd;
        }
    }

    // Otherwise, the file has't been created, find a space and create it
    for (i = 0; i < BLOCKSIZE; i++){

        // Find an empty spot
        if (strncmp(root_directory[i].name, "\0", 1) == 0){

            int fd = -1, j;

            // Find a location in the FD table
            for (j = 0; j < filesOpen; j++){
                if (file_descriptor_table[j] == NULL){
                    file_descriptor_table[j] = malloc(sizeof(file_descriptor));
                    fd = j;
                    break;
                }
            }

            if (fd == -1){
                file_descriptor_table = realloc(file_descriptor_table, (1+filesOpen)*(sizeof(file_descriptor *)));
                file_descriptor_table[filesOpen] = (file_descriptor *) malloc(sizeof(file_descriptor));
                fd = filesOpen++;
            }

            file_descriptor *new = file_descriptor_table[fd];

            if (!new){
                fprintf(stderr, "Error creating %12s", name);
                return -1;}

            // Find first available FAT table entry
            int start = -1;

            for (i = 0; i < BLOCKSIZE;i++){
                if (FAT[i].data == BLOCKSIZE){
                    start = i;
                    break;
                }
            }

            if (start == -1) return -1;

            // Find first available place for first block of data
            int data = first_open();
            if (data == -1) return -1;
            set_used(data);

            // Initialize valus
            new->read_ptr = 0;
            new->write_ptr = 0;
            new->size = 0;
            new->start = start;

            strncpy(root_directory[i].name, name, 13);
            root_directory[i].size = 0;
            root_directory[i].indx = start;
            write_blocks(ROOT_LOC, ROOT_SIZE, root_directory);

            FAT[start].data = data;
            write_blocks(FAT_LOC, FAT_SIZE, FAT);
            return fd;
        }
    }
    return -1;
}

// Make sure that the file descriptor is valid
int sfs_fclose(int fileID){
    // Make sure fileID is valid and fileID hasn't already been closed
    if (fileID >= filesOpen || file_descriptor_table[fileID] == NULL)
        return -1;

    free(file_descriptor_table[fileID]);
    file_descriptor_table[fileID] = NULL;
    return 0;
}

int sfs_fwrite(int fileID, char *buf, int length){
    int length_orig = length;

    // Make sure we have a valid fileID
    if (buf == NULL || length < 0 ||
        fileID >= filesOpen || file_descriptor_table[fileID] == NULL)
        return -1;

    file_descriptor *to_write = file_descriptor_table[fileID];

    FAT_entry *current = &(FAT[to_write->start]);

    char *disk_buff = malloc(BLOCKSIZE);        // Buffer to read sector into
    int i = (to_write->write_ptr) / BLOCKSIZE;  // which sector write_ptr is in
    int j = (to_write->write_ptr) % BLOCKSIZE;  // how far into sector write_ptr is

    while (i > 0){  // find correct sector
        current = &(FAT[current->next]);
        i--;}

    i = 0;  // how many sectors we've read into buf

    int offset = 0;
    while (length > 0){     // keep writing while there's something left to write
        read_blocks(DATA_START + current->data, 1, disk_buff);
        memcpy(disk_buff + j, buf + offset, (BLOCKSIZE - j < length ? BLOCKSIZE - j :  length));
        write_blocks(DATA_START + current->data, 1, disk_buff);

        length -= (BLOCKSIZE - j);
        offset += (BLOCKSIZE - j);
        j = 0;
        i++;

        if (current->next == BLOCKSIZE && length > 0){
            int k;
            int found = 0;
            for (k = 0; k < BLOCKSIZE; k++){
                if (FAT[k].data == BLOCKSIZE){
                    int next = first_open();
                    if (next == -1) return -1;
                    set_used(next);
                    current->next = k;
                    FAT[k].data = next;
                    write_blocks(FAT_LOC, FAT_SIZE, FAT);
                    found = 1;
                    break;
                }
            }
            if (!found)
                return -1;
        }

        if (length > 0)
            current = &(FAT[current->next]);
    }

    // Increase the size of the file as necessary
    to_write->size = to_write->write_ptr + length_orig > to_write->size
                        ? to_write->write_ptr + length_orig
                        : to_write->size;

    // Increase the write_ptr
    to_write->write_ptr += length_orig;

    for (i = 0; i < BLOCKSIZE; i++){
        // Increase size of root_directory entry
        if (to_write->start == root_directory[i].indx){
            root_directory[i].size = to_write->size;
            write_blocks(ROOT_LOC, ROOT_SIZE, root_directory);
            break;
        }
    }

    free(disk_buff);
    return length_orig;
}

// Negative return value => invalid file ID
int sfs_fread(int fileID, char *buf, int length){
    if (length < 0 || buf == NULL ||
        fileID >= filesOpen || file_descriptor_table[fileID] == NULL)
        return -1;

    file_descriptor *to_read = file_descriptor_table[fileID];

    FAT_entry *current = &(FAT[to_read->start]);

    // Make sure we aren't reading past the last written byte of the file
    if (to_read->read_ptr + length > to_read->size)
        length = to_read->size - to_read->read_ptr;

    int length_orig = length;
    char *disk_buff = malloc(BLOCKSIZE);   // Buffer to read sector into
    int i = (to_read->read_ptr) / BLOCKSIZE;   // which sector read ptr is in
    int j = (to_read->read_ptr) % BLOCKSIZE; // how far into sector read_ptr is

    while (i > 0){  // find correct sector
        current = &(FAT[current->next]);
        i--;}

    int offset = 0;
    while (length > 0){
        read_blocks(DATA_START + current->data, 1, disk_buff);
        memcpy(buf + offset, disk_buff + j, (BLOCKSIZE - j < length ? BLOCKSIZE - j :  length));
        length -= (BLOCKSIZE - j);
        offset += (BLOCKSIZE - j);
        j = 0;

        // check if trying to read past end of file
        if (current->next == BLOCKSIZE && length > 0){
            return -1;}

        if (length > 0){
            current = &(FAT[current->next]);}
    }
    free(disk_buff);
    to_read->read_ptr += length_orig;
    return length_orig;
}

// Negative return value => invalid file ID
int sfs_fseek(int fileID, int offset){
    if (fileID >= filesOpen || file_descriptor_table[fileID] == NULL)
        return -1;

    file_descriptor_table[fileID]->read_ptr = offset;
    file_descriptor_table[fileID]->write_ptr = offset;
    return 0;
}

// Negative return value => file not found
int sfs_remove(char *file){
    int i;
    for (i = 0; i < BLOCKSIZE; i++){
        if (strncmp(root_directory[i].name, file, 13) == 0){
            directory_entry *to_remove = &(root_directory[i]);
            strcpy(to_remove->name,"\0");
            to_remove->size =0;
            FAT_entry *fat_tr = &(FAT[to_remove->indx]);
            to_remove->indx = BLOCKSIZE;

            while (1){
                set_unused(fat_tr->data);
                fat_tr->data = BLOCKSIZE;
                if (fat_tr->next == BLOCKSIZE)
                    break;
                FAT_entry *fat_tr_next = &(FAT[fat_tr->next]);
                fat_tr->next = BLOCKSIZE;
                fat_tr = fat_tr_next;
            }

            write_blocks(FAT_LOC, FAT_SIZE, FAT);
            return 0;

        }
    }

    return -1;
}

// Get the value of the first available unused spot
int first_open(){
    unsigned int *buff = malloc(BLOCKSIZE);
    if (!buff){
        fprintf(stderr, "Malloc failed in 'first_open'\n");
        return -1;}

    read_blocks(FREE_LIST, 1, buff);

    int i;
    for (i = 0; i < BLOCKSIZE/sizeof(unsigned int); i++){
        int f = ffs(buff[i]);
        if (f){
            return f + i*8*sizeof(unsigned int) -1;
        }
    }

    return -1;
}

// Set indx to allocated
void set_used(unsigned short indx){
    int i = indx/(8*sizeof(unsigned int));   // where in bit array to flip
    int j = indx % (8*sizeof(unsigned int)); //  which bit to flip
    unsigned int *buff = malloc(BLOCKSIZE);

    if (!buff){
        fprintf(stderr, "Malloc failed in 'set'\n");
        return;}

    read_blocks(FREE_LIST, 1, buff);
    buff[i] &= ~(1 << j);
    write_blocks(FREE_LIST, 1, buff);

}

// Set indx to free
void set_unused(unsigned short indx){
    int i = indx/(8*sizeof(unsigned int));   // where in bit array to flip
    int j = indx % (8*sizeof(unsigned int)); // which bit to flip
    unsigned int *buff = malloc(BLOCKSIZE);
    if (!buff){
        fprintf(stderr, "Malloc failed in 'clear'\n");
        return;}

    read_blocks(FREE_LIST, 1, buff);
    buff[i] |= 1 << j;
    write_blocks(FREE_LIST, 1, buff);
}
