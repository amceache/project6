// fs.c
/*
 *
 * ************************************************************************** */

#include "fs.h"
#include "disk.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <math.h>

#define DISK_BLOCK_SIZE	    4096
#define FS_MAGIC	    0xf0f03410
#define INODES_PER_BLOCK    128
#define POINTERS_PER_INODE  5 // Pointers in inode structure
#define POINTERS_PER_BLOCK  1024 // Pointers in indirect block

/* STRUCTS ------------------------------------------------------------------ */

struct Disk 
{
    int mounted;
};

struct fs_superblock 
{
    int magic;
    int nblocks;
    int ninodeblocks;
    int ninodes;
};

struct fs_inode 
{
    int isvalid;
    int size;
    int direct[POINTERS_PER_INODE];
    int indirect;
};

// Represents 4 different ways of interpreting raw disk data
union fs_block 
{
    struct fs_superblock super;
    struct fs_inode inode[INODES_PER_BLOCK];
    int pointers[POINTERS_PER_BLOCK];
    char data[DISK_BLOCK_SIZE];
};

/* GLOBALS ------------------------------------------------------------------ */

static struct Disk disk;

/* FUNCTIONS ---------------------------------------------------------------- */

/* creates a new filesystem on the disk, destroys data already present */
int fs_format()
{
    if (disk.mounted) 
    { // return failure if disk is mounted
	return 0; 
    }

    // set up super block
    union fs_block block;
    block.super.magic = FS_MAGIC;
    block.super.nblocks = disk_size();
	
    // 10% of these to inodes
    int nblocks = block.super.nblocks;
    double ninodes = (double)nblocks * 0.1;
    
    // round up ninodes (from exactly 10%)
    if ((int) ninodes < ninodes)
    {
	block.super.ninodeblocks = (int)ninodes + 1;
    }
    else
    {
	block.super.ninodeblocks = (int)ninodes;
    }

    block.super.ninodes = block.super.ninodeblocks * INODES_PER_BLOCK;

    // write superblock
    disk_write(0, block.data);

    // clear inode table
    for (int i=0; i<DISK_BLOCK_SIZE; i++) 
    {
	block.data[i] = 0;
    }

    for (int i=1; i<disk_size(); i++) 
    {
	disk_write(i, block.data);
    }

    return 1;
}

/* scans a mounted filesystem and repot on how the inodes and blocks are organized */
void fs_debug()
{
    union fs_block block;

    disk_read(0,block.data);

    printf("superblock:\n");
	
    // check if magic number valid
    if (block.super.magic == FS_MAGIC) 
    {
	printf("    magic number is valid\n");
    } 
    else 
    {
	printf("    magic number is not valid\n");
    }

    printf("    %d blocks on disk\n",block.super.nblocks);
    printf("    %d blocks for inodes\n",block.super.ninodeblocks);
    printf("    %d inodes total\n",block.super.ninodes);

    // look through inode blocks
    for (int i=0; i<INODES_PER_BLOCK; i++) 
    {
	struct fs_inode inode = block.inode[i];
	if (inode.isvalid) 
	{
	    printf("inode %d:\n", i);
	    printf("    size: %d\n", inode.size);
	    printf("    direct blocks:");
	    for (int j=0; j<POINTERS_PER_INODE; j++) 
	    {
		printf(" %d", inode.direct[j]);
	    }
	    printf("\n");
	    if (inode.indirect > 0) 
	    {
		printf("    indirect block: %d\n",inode.indirect);
		// read indirect block data
		printf("    indirect data blocks:");
		union fs_block ind_block;
		disk_read(inode.indirect,ind_block.data);
		for (int k=0; k<POINTERS_PER_BLOCK; k++) 
		{
		    if (ind_block.pointers[k] > 0) 
		    {
			printf(" %d", ind_block.pointers[k]);
		    }
		}
		printf("\n");
	    }
	}
    }
}

/* examine the disk for a filesystem, build a free block bitmap, prepare the filesystem for use */
int fs_mount()
{
    union fs_block block;
    
    // check if already mounted
    if (disk.mounted) 
    {
	printf("File system already mounted\n");
	return 0;
    }

    disk_read(0, block.data); // read superblock

    // check for correct magic number
    if (block.super.magic != FS_MAGIC)
    {
	printf("Invalid Magic Number\n");
	return 0;
    }

    int nblocks = block.super.nblocks;
    // create free block bitmap


    return 0;
}

/* create a new inode of zero length */
int fs_create()
{
    return 0;
}

/* delete the inode indicated by the number */
int fs_delete( int inumber )
{
    return 0;
}

/* return the logical size of of the given inode (bytes) */
int fs_getsize( int inumber )
{
    return -1;
}

/* read data from a valid inode */
int fs_read( int inumber, char *data, int length, int offset )
{
    return 0;
}

/* write data to a valie inode */
int fs_write( int inumber, const char *data, int length, int offset )
{
    return 0;
}
