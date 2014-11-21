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

Programming Assignment 2 - Simple File Systems
************************************************/


// Important block locations
#define SUPERBLOCK 0
#define FREE_LIST 1
#define ROOT 2
#define ROOT_SIZE 16
#define FAT ROOT+ROOT_SIZE
#define FAT_SIZE 4
#define DATA_START FAT+FAT_SIZE
// block size in bytes
#define BLOCKSIZE 2048

// super + free sector list + root + FAT
#define NUMBLOCKS 1+BLOCKSIZE+ROOT_SIZE+FAT_SIZE

// Assume at most 12 characters for filename
#define MAX_FNAME_LENGTH 12

// filename for file system
#define FILENAME "my.sfs"

// make sure EOF exists
#ifndef EOF
#define EOF -1
#endif

typedef struct root_entry {
    char name[MAX_FNAME_LENGTH];
    unsigned short size;
    unsigned short indx;
} root_entry;

typedef struct fat_entry {
    unsigned short data;
    unsigned short next;
} fat_entry;

typedef struct fd_entry {
    unsigned int read_ptr;
    unsigned int write_ptr;
    unsigned short size;
    unsigned short start;
} fd_entry;

int numUsed, filesOpen;
root_entry *root_directory;
fd_entry **fd_table;
fat_entry *fat_entry_table;

unsigned short first_open();
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
        int super_buff_size = 8;
        assert(sizeof(int) * super_buff_size <= BLOCKSIZE);
        int super_buff[super_buff_size];
        super_buff[0] = BLOCKSIZE;  // Size of each block
        super_buff[1] = NUMBLOCKS;  // Number of blocks on disk (including super)
        super_buff[1] = FREE_LIST;  // Block containing free list
        super_buff[2] = ROOT;       // Location of 1st block of root directory
        super_buff[4] = ROOT_SIZE;  // Number of for root directory
        super_buff[5] = FAT;        // Location of 1st block of FAT
        super_buff[6] = FAT_SIZE;   // Number of blocks for FAT
        super_buff[7] = DATA_START; // Location of 1st block of user data
        super_buff[8] = 0;          // Number of used blocks (= BLOCKSIZE - empty)

        write_blocks(SUPERBLOCK, 1, super_buff);

        // Create free lest
        long free_buff[BLOCKSIZE/sizeof(long)];
        int i;

        // Fill array with 1s to signify not used data blocks (1 = available)
        for (i = 0; i < BLOCKSIZE/sizeof(long); i++){
            free_buff[i] = !free_buff[i];
        }

        write_blocks(FREE_LIST, 1, free_buff);

        // Create root directory
        root_entry new_root_buff[BLOCKSIZE*ROOT_SIZE/sizeof(root_entry)];
        for (i = 0; i < BLOCKSIZE*ROOT_SIZE/sizeof(root_entry); i++)
            new_root_buff[i] = (root_entry) {.name = "\0", .size=0, .indx = EOF };

        write_blocks(ROOT, ROOT_SIZE, new_root_buff);

    } else {
        // Open disk before initialize data structures
        if (init_disk(FILENAME, BLOCKSIZE, NUMBLOCKS) != 0){
            fprintf(stderr, "Error in opening disk");
            return -1;
        }
    }

    // initialize variables
    numUsed = 0;
    filesOpen = 0;

    root_directory = (root_entry *) malloc(sizeof(root_entry) * BLOCKSIZE);
    fat_entry_table = (fat_entry*) malloc(sizeof(fat_entry) * BLOCKSIZE);

    if (!root_directory || !fat_entry_table){
        fprintf(stderr, "Error in malloc at mksfs");
        exit(1);}

    read_blocks(ROOT, ROOT_SIZE, root_directory);
    read_blocks(FAT, FAT_SIZE, fat_entry_table);
    return 0;
}

void sfs_ls(void){
    if (!root_directory){
        fprintf(stderr,
            "Error in sfs_ls.\nFile system neads to be opened first");
        return;}

    int i;
    for (i = 0; i < BLOCKSIZE; i++){
        if (strcmp(root_directory[i].name, "\0") != 0)
            printf("%12s\t%d\n", root_directory[i].name, root_directory[i].size);
    }
}

int sfs_fopen(char *name){
    if (!root_directory){
        fprintf(stderr,
            "Error in sfs_ls.\nFile system neads to be opened first");
        return -1;}

    int i;
    for (i = 0; i < BLOCKSIZE; i++){
        if (strcmp(root_directory[i].name, name) == 0){
            fd_table = realloc(fd_table, 1+filesOpen);
            fd_table[filesOpen] = (fd_entry *) malloc(sizeof(fd_entry));
            fd_entry *new = fd_table[filesOpen];
            if (!new){
                fprintf(stderr, "Error opening %12s", name);
                return -1;}

            new->read_ptr = 0;
            new->write_ptr = root_directory[i].size;
            new->size = root_directory[i].size;
            new->start = root_directory[i].indx;
            return filesOpen++;
        }
    }

    for (i = 0; i < BLOCKSIZE; i++){
        if (strcmp(root_directory[i].name, "\0") == 0){
            unsigned short start = first_open();
            set_used(start);
            fd_table = realloc(fd_table, 1+filesOpen);
            fd_table[filesOpen] = (fd_entry *) malloc(sizeof(fd_entry));
            fd_entry *new = fd_table[filesOpen];
            if (!new){
                fprintf(stderr, "Error creating %12s", name);
                return -1;}

            new->read_ptr = 0;
            new->write_ptr = 0;
            new->size = 0;
            new->start = start;
            return filesOpen++;
        }
    }

    return -1;
}

int sfs_fclose(int fileID){
    fd_table[fileID] = NULL;
    return -1;
}

int sfs_fwrite(int fileID, char *buf, int length){
    return -1;
}

int sfs_fread(int fileID, char *buf, int length){
    return -1;
}

int sfs_fseek(int fileID, int offset){
    return -1;
}

int sfs_remove(char *file){
    return -1;
}

unsigned short first_open(){
    int *buff = malloc(BLOCKSIZE/sizeof(int));
    if (!buff){
        fprintf(stderr, "Malloc failed in 'first_open'");
        return -1;}

    int i;
    for (i = 0; i < BLOCKSIZE/sizeof(int); i++){
        int f = ffs(buff[i]);
        if (f){
            return f + i * sizeof(int);
        }
    }

    return -1;


}

void set_used(unsigned short indx){
    int i = indx/sizeof(int);   // where in bit array to flip
    int j = indx % sizeof(int); // which bit to flip
    int *buff = malloc(BLOCKSIZE/sizeof(int));
    if (!buff){
        fprintf(stderr, "Malloc failed in 'set'");
        return;}

    if (read_blocks(FREE_LIST, 1, buff) != 0){
        fprintf(stderr, "Couldn't read free list in 'set'");
        return;
    }

    buff[i] &= ~(1 << j);

    if (write_blocks(FREE_LIST, 1, buff) != 0){
        fprintf(stderr, "Couldn't write free list in 'clear'");
        return;
    }

}

void set_unused(unsigned short indx){
    int i = indx/sizeof(int);   // where in bit array to flip
    int j = indx % sizeof(int); // which bit to flip
    int *buff = malloc(BLOCKSIZE/sizeof(int));
    if (!buff){
        fprintf(stderr, "Malloc failed in 'clear'");
        return;}

    if (read_blocks(FREE_LIST, 1, buff) != 0){
        fprintf(stderr, "Couldn't read free list in 'clear'");
        return;
    }

    buff[i] |= 1 << j;

    if (write_blocks(FREE_LIST, 1, buff) != 0){
        fprintf(stderr, "Couldn't write free list in 'clear'");
        return;
    }
}
