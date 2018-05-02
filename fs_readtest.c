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
    struct fs_inode inodes[INODES_PER_BLOCK];
    int pointers[POINTERS_PER_BLOCK];
    char data[DISK_BLOCK_SIZE];
};

/* GLOBALS ------------------------------------------------------------------ */

static struct Disk disk;
int *bitmap;

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
    int inodes = block.super.ninodeblocks+1;

    for(int i=1; i < inodes; i++)
    {	// access all inode blocks
	disk_read(i, block.data);
	for(int j=0; j < INODES_PER_BLOCK; j++)
	{ // access all inodes
	    block.inodes[j].isvalid = 0; // <-- access inode j in block i
	    for (int k = 0; k < POINTERS_PER_INODE; k++)
	    {
		int direct = block.inodes[j].direct[k];
		if (direct != 0)
		{
		    block.inodes[j].direct[k] = 0;
		    union fs_block data_block;
		    disk_read(direct, data_block.data);
		    for (int m=0; m < DISK_BLOCK_SIZE; m++)
		    {
			data_block.data[m] = 0; // set all data to 0 in data block
		    }
		    disk_write(direct, data_block.data);
		}
		// set all direct pointers to 0
	    }

	    // indirect pointers
	    union fs_block indirect;
	    if (block.inodes[j].indirect > 0)
	    {
		disk_read(block.inodes[j].indirect, indirect.data);
		for (int m=0; m < POINTERS_PER_BLOCK; m++)
		{
		    if (indirect.pointers[m] > 0)
		    {
			union fs_block pointer;
			disk_read(indirect.pointers[m], pointer.data);
			for (int n=0; n < DISK_BLOCK_SIZE; n++)
			{
			    pointer.data[n] = 0;
			}
			disk_write(indirect.pointers[m], pointer.data);
			indirect.pointers[m] = 0;
		    }
		}
		disk_write(block.inodes[j].indirect, indirect.data);
		block.inodes[j].indirect = 0;
		
	    }
	}
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

    int i; // increments through all blocks
    int inodes = block.super.ninodeblocks+1;

    // look through inode blocks
    for (i=1; i < inodes; i++)
    {
	disk_read(i, block.data);
	for (int j = 0; j < INODES_PER_BLOCK; j++)
	{
	    
	    if(block.inodes[j].isvalid)
	    {
		printf("inode %d:\n", j+(INODES_PER_BLOCK)*(i-1));
		printf("    size: %d\n", block.inodes[j].size);
		printf("    direct blocks:");
		
		for (int k=0; k < POINTERS_PER_INODE; k++)
		{
		    if (block.inodes[j].direct[k] != 0)
		    {
			printf(" %d", block.inodes[j].direct[k]);
		    }
		}
		printf("\n");

		union fs_block indirect;
		if (block.inodes[j].indirect > 0)
		{
		    printf("	indirect block: %d\n", block.inodes[j].indirect);

		    // read indirect block data
		    printf("	indirect data blocks:");
		    disk_read(block.inodes[j].indirect, indirect.data);
		    for (int m=0; m < POINTERS_PER_BLOCK; m++)
		    {
			if (indirect.pointers[m] > 0)
			{
			    printf(" %d", indirect.pointers[m]);
			}
		    }
		    printf("\n");
		}
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
    bitmap = malloc(nblocks*sizeof(int));
    int inodes = block.super.ninodeblocks+1;
    for(int i=0; i < inodes; i++)
    {
	bitmap[i] = 1; // superblock and inode blocks filled
    }

    for(int i=1; i < inodes; i++)
    {
	disk_read(i, block.data);
	for (int j=0; j < INODES_PER_BLOCK; j++)
	{
	    if (block.inodes[j].isvalid)
	    {
		for (int k=0; k < POINTERS_PER_INODE; k++)
		{
		    if (block.inodes[j].direct[k] > 0)
		    {
			bitmap[block.inodes[j].direct[k]] = 1;
		    }
		}

		// indirection
		union fs_block indirect;
		if (block.inodes[j].indirect > 0)
		{
		    bitmap[block.inodes[j].indirect] = 1;
		    disk_read(block.inodes[j].indirect, indirect.data);
		    for (int m=0; m < POINTERS_PER_BLOCK; m++)
		    {
			if (indirect.pointers[m] > 0)
			{
			    bitmap[indirect.pointers[m]] = 1;
			}
		    }
		}
	    }
	}
    }

    disk.mounted = 1; 
    return 1;
}

/* create a new inode of zero length, returns number of inode */
int fs_create()
{
    union fs_block block;
    disk_read(0, block.data);
    int inodes = block.super.ninodeblocks;
    int node = 0;
    int blck = 0;
    
    for (int i=1; i < inodes+1; i++)
    {
	blck = i;
	disk_read(i, block.data);
	for (int j=0; j < INODES_PER_BLOCK; j++)
	{
	    if (!block.inodes[j].isvalid)
	    {
		node = j+(INODES_PER_BLOCK)*(i-1);
		if(node == 0 && i == 1)
		{
		    continue;
		}
		break;
	    }
	}
	
	if (node != 0)
	{
	    break;
	}
    }


    if (node == 0)
    {
	// all nodes occupied
	return 0;
    }

    // initilize inode
    block.inodes[node].isvalid = 1;
    block.inodes[node].size = 0;
    
    union fs_block inode;


    for (int i=0; i < POINTERS_PER_INODE; i++)
    {
	if (block.inodes[node].direct[i] > 0)
	{
	    disk_read(block.inodes[node].direct[i], inode.data);
	    for (int j=0; j < DISK_BLOCK_SIZE; j++)
	    {
		inode.data[j] = 0;
	    }
	    disk_write(block.inodes[node].direct[i], inode.data);
	}
	block.inodes[node].direct[i] = 0;
    
    
	// TODO: Indirection
    }
    disk_write(blck, block.data);

    printf("node: %d\n", node);

    return node;
}

/* delete the inode indicated by the number */
int fs_delete( int inumber )
{
    union fs_block block;
    disk_read(0, block.data);
    if (inumber > block.super.ninodes)
    {
	return 0;
    }

    int nblock = 1;
    while (inumber > INODES_PER_BLOCK)
    {
	nblock++;
    }

    disk_read(nblock, block.data);
    if(!block.inodes[inumber].isvalid)
    {
	return 0;
    }

    block.inodes[inumber].isvalid = 0;
    for (int k=0; k < POINTERS_PER_INODE; k++)
    {
	if(block.inodes[inumber].direct[k] > 0)
	{
	    int direct = block.inodes[inumber].direct[k];
	    bitmap[direct] = 0;
	}
    }

    // TODO: Indirection
    disk_write(nblock, block.data);

    return 1;
}

/* return the logical size of of the given inode (bytes) */
int fs_getsize( int inumber )
{
    union fs_block block;
    disk_read(0, block.data);
    if (inumber > block.super.ninodes)
    {
	return -1;
    }

    int nblock = 1;
    while (inumber > INODES_PER_BLOCK)
    {
	nblock++;
    }

    disk_read(nblock, block.data);
    if(!block.inodes[inumber].isvalid)
    {
	return -1;
    }

    return block.inodes[inumber].size;
}

int fs_read( int inumber, char *data, int length, int offset )
{
    union fs_block block;
    struct fs_inode inode;

    disk_read(0, block.data);
    
    if(inumber > block.super.ninodes || inumber < 0){
        //returns an error for the invalid inode number 
        return 0;
    }
    
    int totalinodes = (block.super.ninodeblocks*INODES_PER_BLOCK);

    int i, j;
    int current_byte = 0;
    int first = 0;
    
    disk_read((int) (inumber/INODES_PER_BLOCK) + 1, block.data);
   
    //FAILING HERE --SAYS THAT BLOCK DOES NOT HAVE FIELD INODE**********
    *inodes = block.inode[inumber%INODES_PER_BLOCK];

    if (inode.isvalid == 0) {
        //return error if the value is 0 -- needs to be valid 
        return 0;
    }
    if(offset >= inode.size){
        return 0;
    }

    int startBlock = (int)(offset/DISK_BLOCK_SIZE);
    //Find offset per block -- by moding for 4096 -- find block 
    int current_offset = offset%4096;
    for(i = startBlock; i < POINTERS_PER_INODE; i++){
        if(inode.direct[i]){
            if (first == 0) {
                disk_read(inode.direct[i], block.data);
                for(j = 0; j+current_offset < DISK_BLOCK_SIZE; j++){
                    if(block.data[j+current_offset]){
                        data[current_byte] = block.data[j+current_offset];
                        current_byte++;
                        if(current_byte+offset >= inode.size){
                            return current_byte;
                        }
                    }
                    else{
                        return current_byte;
                    }
			//At the end 
                    if (current_byte == length){
                        return current_byte;
                    }
                } 
                first = 1;
            }
	//Without the offset 
            else{
                disk_read(inode.direct[i], block.data);
                for(j = 0; j < DISK_BLOCK_SIZE; j++){
                    if(block.data[j]){
                        data[current_byte] = block.data[j];
                        current_byte++;
                        if(current_byte+offset >= inode.size){
                            return current_byte;
                        }

                    }
                    else{
                        return current_byte;
                    }
                    if (current_byte == length){
                        return current_byte;
                    }
                } 
            }
        }
    }

    //Indirect nodes get the data 
    //create an indirect block

    union fs_block indirectBlock;
    printf("%d\n", startBlock);
    int startIndirect = startBlock - 5;
    if(inode.indirect)
    {
        disk_read(inode.indirect, indirectBlock.data);
        for(i = startIndirect; i < POINTERS_PER_BLOCK; i++)
        {
            if (indirectBlock.pointers[i])
            {
                disk_read(indirectBlock.pointers[i], block.data);
                //do same thingfor read 
		for(j = 0; j < DISK_BLOCK_SIZE; j++){
                    if(block.data[j]){
                        data[current_byte] = block.data[j];
                        current_byte++;
                        if(current_byte+offset >= inode.size){
                            return current_byte;
                        }

                    }
                    else{
                        return current_byte;
                    }
                    if (current_byte == length){
                        return current_byte;
                    }
                } 
            }
        }
    }
    return current_byte;
}



/* write data to a valie inode */
int fs_write( int inumber, const char *data, int length, int offset )
{
    return 0;
}
