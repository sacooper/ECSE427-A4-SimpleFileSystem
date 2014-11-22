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
#define ROOT_SIZE 18
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
    unsigned short indx;
    unsigned int size;
} root_entry;

typedef struct fat_entry {
    unsigned short data;
    unsigned short next;
} fat_entry;

typedef struct fd_entry {
    unsigned int read_ptr;
    unsigned int write_ptr;
    unsigned int size;
    unsigned short start;
} fd_entry;

int numUsed, filesOpen;
root_entry *root_directory;
fd_entry **file_descriptor_table;
fat_entry *fat_entry_table;

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
        super_buff[2] = ROOT;       // Location of 1st block of root directory
        super_buff[4] = ROOT_SIZE;  // Number of for root directory
        super_buff[5] = FAT;        // Location of 1st block of FAT
        super_buff[6] = FAT_SIZE;   // Number of blocks for FAT
        super_buff[7] = DATA_START; // Location of 1st block of user data
        super_buff[8] = 0;          // Number of used blocks (= BLOCKSIZE - empty)

        write_blocks(SUPERBLOCK, 1, super_buff);
        free(super_buff);

        // Create free lest
        long *free_buff = malloc(BLOCKSIZE);
        if (!free_buff){
            fprintf(stderr, "Error initialize free sector list");
            return -1;}

        int i;

        // Fill array with 1s to signify not used data blocks (1 = available)
        for (i = 0; i < BLOCKSIZE/sizeof(long); i++){
            free_buff[i] = ~free_buff[i];}

        write_blocks(FREE_LIST, 1, free_buff);
        free(free_buff);

        // Create root directory
        assert(ROOT_SIZE==sizeof(root_entry));
        root_entry *new_root_buff = malloc(BLOCKSIZE*sizeof(root_entry));

        if (!new_root_buff){
            fprintf(stderr, "Error initializing root directory");
            return -1;}

        for (i = 0; i < BLOCKSIZE*ROOT_SIZE/sizeof(root_entry); i++)
            new_root_buff[i] = (root_entry) {.name = "\0", .size=0, .indx = 0 };

        write_blocks(ROOT, ROOT_SIZE, new_root_buff);
        free(new_root_buff);

        // Create File Allocation Table
        assert(FAT_SIZE == sizeof(fat_entry));
        fat_entry *new_fat_table = malloc(BLOCKSIZE * sizeof(fat_entry));

        if (!new_fat_table){
            fprintf(stderr, "Error intitializing FAT");
            return -1;}

        for (i = 0; i < BLOCKSIZE; i++){
            new_fat_table[i].data = BLOCKSIZE;
            new_fat_table[i].next = BLOCKSIZE;}

        write_blocks(FAT, FAT_SIZE, new_fat_table);
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
    // initialize variables
    numUsed = super_block[8];
    filesOpen = 0;

    root_directory = malloc(sizeof(root_entry) * BLOCKSIZE);
    fat_entry_table = malloc(sizeof(fat_entry) * BLOCKSIZE);

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
            "Error in sfs_ls.\nFile system neads to be initialized first");
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
            int fd = -1, j;
            for (j = 0; j < filesOpen; j++){
                if (!file_descriptor_table[j]){
                    file_descriptor_table[j] = malloc(sizeof(fd_entry));
                    fd = j;
                    break;
                }
            }
            if (fd == -1){
                file_descriptor_table = realloc(file_descriptor_table, (1+filesOpen)*(sizeof(fd_entry *)));
                file_descriptor_table[filesOpen] = (fd_entry *) malloc(sizeof(fd_entry));
                fd = filesOpen++;
            }
            fd_entry *new = file_descriptor_table[fd];
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

    for (i = 0; i < BLOCKSIZE; i++){
        if (strcmp(root_directory[i].name, "\0") == 0){
            int start = first_open();
            if (start == -1){
                fprintf(stderr, "Unable to create file %s", name);
                return -1;}

            set_used(start);
            int fd = -1, j;
            for (j = 0; j < filesOpen; j++){
                if (!file_descriptor_table[j]){
                    file_descriptor_table[j] = malloc(sizeof(fd_entry));
                    fd = j;
                    break;
                }
            }
            if (fd == -1){
                file_descriptor_table = realloc(file_descriptor_table, (1+filesOpen)*(sizeof(fd_entry *)));
                file_descriptor_table[filesOpen] = (fd_entry *) malloc(sizeof(fd_entry));
                fd = filesOpen++;
            }

            fd_entry *new = file_descriptor_table[fd];

            if (!new){
                fprintf(stderr, "Error creating %12s", name);
                return -1;}

            new->read_ptr = 0;
            new->write_ptr = 0;
            new->size = 0;
            new->start = start;
            return fd;
        }
    }

    return -1;
}

int sfs_fclose(int fileID){
    free(file_descriptor_table[fileID]);
    file_descriptor_table[fileID] = NULL;
    return 0;
}

int sfs_fwrite(int fileID, char *buf, int length){
    if (fileID >= filesOpen || file_descriptor_table[fileID] == NULL)
        return -1;

    fd_entry *to_write = file_descriptor_table[fileID];

    fat_entry *current = &(fat_entry_table[to_write->start]);

    char *disk_buff = malloc(BLOCKSIZE);   // Buffer to read sector into
    int i = to_write->write_ptr/BLOCKSIZE;   // which sector write_ptr is in
    int j = to_write->write_ptr % BLOCKSIZE; // how far into sector write_ptr is

    while (i > 0){  // find correct sector
        current = &(fat_entry_table[current->next]);
        i--;}

    i = 0;  // how many sectors we've read into buf

    int offset = 0;
    while (length > 0){
        read_blocks(DATA_START + current->data, 1, disk_buff);
        memcpy(disk_buff + j, buf + offset, (BLOCKSIZE - j < length ? BLOCKSIZE - j :  length));
        write_blocks(DATA_START + current->data, 1, disk_buff);

        length -= (BLOCKSIZE - j);
        offset += (BLOCKSIZE - j);
        j = 0;
        i++;

        if (current->next == BLOCKSIZE && length > 0){
            int next = first_open();
            if (next == -1) return -1;

            int k;
            for (k = 0; k < BLOCKSIZE; k++){
                if (fat_entry_table[k].data == BLOCKSIZE){
                    current->next = k;
                    fat_entry_table[k].data = next;
                    break;
                }
            }
        }
        if (length > 0)
            current = &(fat_entry_table[current->next]);
    }

    to_write->write_ptr += length;

    return 0;
}

// Negative return value => invalid file ID
int sfs_fread(int fileID, char *buf, int length){
    if (fileID >= filesOpen || file_descriptor_table[fileID] == NULL)
        return -1;

    fd_entry *to_read = file_descriptor_table[fileID];

    fat_entry *current = &(fat_entry_table[to_read->start]);

    char *disk_buff = malloc(BLOCKSIZE);   // Buffer to read sector into
    int i = to_read->read_ptr/BLOCKSIZE;   // which sector read ptr is in
    int j = to_read->read_ptr % BLOCKSIZE; // how far into sector read_ptr is

    while (i > 0){  // find correct sector
        current = &(fat_entry_table[current->next]);
        i--;}

    i = 0;  // how many sectors we've read into buf

    int offset = 0;
    while (length > 0){
        read_blocks(current->data, 1, disk_buff);

        memcpy(buf + offset, disk_buff + j, (BLOCKSIZE - j < length ? BLOCKSIZE - j :  length));
        length -= (BLOCKSIZE - j);
        offset += (BLOCKSIZE - j);
        j = 0;
        i++;

        if (current->next == BLOCKSIZE && length != 0){
            return -1;}

        if (length > 0){
            current = &(fat_entry_table[current->next]);}
    }

    to_read->read_ptr += length;

    return 0;
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
        if (strcmp(root_directory[i].name, file) == 0){
            root_entry *to_remove = &(root_directory[i]);
            strcpy(to_remove->name,"\0");
            to_remove->size =0;
            fat_entry *fat_tr = &(fat_entry_table[to_remove->indx]);
            to_remove->indx = 0;

            while (fat_tr != NULL){
                set_unused(fat_tr->data);
                fat_tr->data = BLOCKSIZE;
                fat_entry *fat_tr_next = &(fat_entry_table[fat_tr->next]);
                fat_tr->next = BLOCKSIZE;
                fat_tr = fat_tr_next;
            }
            return 0;

        }
    }

    return -1;
}

// Get the value of the first available unused spot
int first_open(){
    int *buff = malloc(BLOCKSIZE/sizeof(int));
    if (!buff){
        fprintf(stderr, "Malloc failed in 'first_open'");
        return -1;}

    int i;
    for (i = 0; i < BLOCKSIZE/sizeof(int); i++){
        unsigned short f = ffs(buff[i]);
        if (f){
            return f + i * sizeof(int);
        }
    }

    return -1;


}

// Set indx to allocated
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

// Set indx to free
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
