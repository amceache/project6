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
    
    for (int i=0; i < POINTERS_PER_INODE; i++)
    {
	block.inodes[node].direct[i] = 0;
    
	// Indirection
	block.inodes[node].indirect = 0;
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
    
	if(block.inodes[inumber].indirect > 0)
	{
	    union fs_block indirect;
	    disk_read(block.inodes[inumber].indirect, indirect.data);
	    for (int j=0; j < POINTERS_PER_BLOCK; j++)
	    {
		int ptr = indirect.pointers[j];
		if (ptr > 0) 
		{
		    bitmap[ptr] = 0;
		}
		indirect.pointers[j] = 0;
	    }
	    disk_write(block.inodes[inumber].indirect, indirect.data);
	}
    }
    
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

/* read data from a valid inode */
int fs_read( int inumber, char *data, int length, int offset )
{
    union fs_block block;
    struct fs_inode inode;

    disk_read(0, block.data);

    if(inumber > block.super.ninodes || inumber < 0){
        //returns an error for the invalid inode number
	return 0;
    }

    //int totalinodes = (block.super.ninodeblocks*INODES_PER_BLOCK);

    int i, j;
    int current_byte = 0;
    int first = 0;

    disk_read((int) (inumber/INODES_PER_BLOCK) + 1, block.data);

    inode = block.inodes[inumber%INODES_PER_BLOCK];

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
            else {
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

/* write data to a valid inode */
int fs_write( int inumber, const char *data, int length, int offset )
{
    printf("writing\n");
    union fs_block block;
    int i, j;
    int current_byte = 0;

    if (!disk.mounted)
    {
	printf("disk not mounted\n");
	return 0;
    }
    
    // scan for free blocks in bitmap
    int bm_loc = 0;

    disk_read(0, block.data);
    int blocks = block.super.nblocks;

    for (i=0; i < block.super.nblocks; i++)
    {
	if (bitmap[i] == 0)
	{
	    bm_loc = i;
	    break;
	}
    }

    if(inumber > block.super.ninodes || inumber < 0){
        //returns an error for the invalid inode number
	return 0;
    }

    int inode = (int)(inumber%INODES_PER_BLOCK);
    printf("inode = %d\n", inode);
    int num = (int)(inode/INODES_PER_BLOCK) + 1;
    disk_read(num, block.data);


    if (block.inodes[inode].isvalid == 0) {
        printf("valid\n");
	return 0;
    }
    
    // start at starting block
    int startBlock = (int)(offset/DISK_BLOCK_SIZE);


    printf("%d\n", block.inodes[inode].direct[startBlock]);
    if (block.inodes[inode].direct[startBlock] == 0)
    {
	block.inodes[inode].direct[startBlock] = bm_loc;
	printf("bitmap updated\n");
	bitmap[bm_loc] = 1;
    }

    union fs_block direct;

    printf("offset = %d\n", offset); 
    
    printf("startbl %d\n", startBlock);
    int current_offset = offset%4096;
    printf("current offset = %d\n", current_offset);
    for (i = startBlock; i < POINTERS_PER_INODE; i++) {
	if (block.inodes[inode].direct[i] > 0) {
	    printf("inode direct %d exists at %d\n", block.inodes[inode].direct[i], i);
	    disk_read(block.inodes[inode].direct[i], direct.data);	    
	    for (j = 0; j+current_offset < DISK_BLOCK_SIZE; j++) {
		direct.data[current_byte] =  data[j+current_offset];
		current_byte++;
		if (current_byte > length) {
		    current_byte--; // Finish off block with trailing 0s
		}
	    }
	    printf("%d\n", block.inodes[inode].direct[i]);
	    disk_write(block.inodes[inode].direct[i], direct.data);
	}
	else if (i < POINTERS_PER_INODE)
	{
	    printf("look for block\n");
	    if (current_byte == length) {
		printf("End reached\n");
		break;
	    }
	    
	    // Get new block
	    
	    int found_bl = 0;
	    printf("blocks = %d\n", blocks);
	    for (int k=0; k < blocks; k++)
	    {
		printf("bitmap[%d] = %d\n", k, bitmap[k]);
	    }

	    for (int k=0; k < blocks; k++) 
	    {
		if (bitmap[k] == 0)
		{
		    found_bl = 1;
		    bm_loc = k;
		    break;
		}
	    }
	    
	    if (!found_bl)
	    {
		// No free blocks found
		printf("no blocks found\n");
		break;
	    }

	    block.inodes[inode].direct[i] = bm_loc;
	    bitmap[bm_loc] = 1;
	    printf("new block needed to add  = %d\n", bm_loc);
	    disk_write(num, block.data); 
	    
	    printf("inode direct %d exists at %d\n", block.inodes[inode].direct[i], i);
	    disk_read(bm_loc, direct.data);	    
	    
	    printf("read\n");
	    for (j = 0; j+current_offset < DISK_BLOCK_SIZE; j++) {
		if (current_byte <= length) {
		    direct.data[current_byte-(4096*i)] =  data[j+current_offset+(4096&i)];
		}
		current_byte++;
		if (current_byte > length) {
		    direct.data[current_byte-(4096*i)] = 0;
		    current_byte--; // Finish off block with trailing 0s
		}
	    }
	    // printf("%d\n", block.inodes[inode].direct[i]);
	    disk_write(bm_loc, direct.data);
	    disk_read(num, block.data);
	    printf("written\n");
	}
    }
    block.inodes[inode].size = current_byte;
    disk_write(num, block.data);
    return current_byte;
}
