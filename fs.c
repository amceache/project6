
#include "fs.h"
#include "disk.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <math.h>

#define FS_MAGIC           0xf0f03410
#define INODES_PER_BLOCK   128
#define POINTERS_PER_INODE 5
#define POINTERS_PER_BLOCK 1024

struct Disk {
	int mounted;
};

struct fs_superblock {
	int magic;
	int nblocks;
	int ninodeblocks;
	int ninodes;
};

struct fs_inode {
	int isvalid;
	int size;
	int direct[POINTERS_PER_INODE];
	int indirect;
};

union fs_block {
	struct fs_superblock super;
	struct fs_inode inode[INODES_PER_BLOCK];
	int pointers[POINTERS_PER_BLOCK];
	char data[DISK_BLOCK_SIZE];
};

/* Globals */
static struct Disk disk;

int fs_format()
{
	// check if disk is mounted
	if (disk.mounted) {
		return 0;
	}

	// set up super block
	union fs_block block;
	block.super.magic = FS_MAGIC;
	block.super.nblocks = disk_size();
	// 10% of these to inodes
	block.super.ninodeblocks = (int) disk_size()*0.1;
	block.super.ninodes = block.super.ninodeblocks * INODES_PER_BLOCK;

	// write superblock
	disk_write(0, block.data);

	// clear inode table
	for (int i=0; i<DISK_BLOCK_SIZE; i++) {
		block.data[i] = 0;
	}

	for (int i=1; i<disk_size(); i++) {
		disk_write(i, block.data);
	}


	return 1;
}

void fs_debug()
{
	union fs_block block;

	disk_read(0,block.data);

	printf("superblock:\n");
	// check if magic number valid
	if (block.super.magic == FS_MAGIC) {
		printf("    magic number is valid\n");
	} else {
		printf("    magic number is not valid\n");
	}

	printf("    %d blocks on disk\n",block.super.nblocks);
	printf("    %d blocks for inodes\n",block.super.ninodeblocks);
	printf("    %d inodes total\n",block.super.ninodes);

	// look through inode blocks
	for (int i=0; i<INODES_PER_BLOCK; i++) {
		struct fs_inode inode = block.inode[i];
		if (inode.isvalid) {
			printf("inode %d:\n", i);
			printf("    size: %d\n", inode.size);
			printf("    direct blocks:");
			for (int j=0; j<POINTERS_PER_INODE; j++) {
				printf(" %d", inode.direct[j]);
			}
			printf("\n");
			if (inode.indirect > 0) {
				printf("    indirect block: %d\n",inode.indirect);
				// read indirect block data
				printf("    indirect data blocks:");
				union fs_block ind_block;
				disk_read(inode.indirect,ind_block.data);
				for (int k=0; k<POINTERS_PER_BLOCK; k++) {
					if (ind_block.pointers[k] > 0) {
						printf(" %d", ind_block.pointers[k]);
					}
				}
				printf("\n");
			}
		}
	}
}

int fs_mount()
{
	// check if already mounted
	if (disk.mounted) {
		return 0;
	}
	return 0;
}

int fs_create()
{
	return 0;
}

int fs_delete( int inumber )
{
	return 0;
}

int fs_getsize( int inumber )
{
	return -1;
}

int fs_read( int inumber, char *data, int length, int offset )
{
	return 0;
}

int fs_write( int inumber, const char *data, int length, int offset )
{
	return 0;
}
