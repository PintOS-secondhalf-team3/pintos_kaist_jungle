#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "filesys/fat.h"


/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

/* On-disk inode.
 * Must be exactly DISK_SECTOR_SIZE bytes long. */
struct inode_disk
{
	disk_sector_t start;  /* First data sector. */
	off_t length;		  /* File size in bytes. */
	unsigned magic;		  /* Magic number. */
	uint32_t unused[125]; /* Not used. */
};

/* Returns the number of sectors to allocate for an inode SIZE
 * bytes long. */
/*  byte를 pos로 받는다. 그래서 그에 해당하는 sector가 어디인지를 검색한다. 
즉, 어떤 파일이 3,4,5,6 sector를 차지한다고 하자. 그리고 이 파일의 765byte는 어느 sector에 있을까?
이 file의 start는 3이고 765byte는 2번째 섹터에 있을 것이므로 3+1 = 4가 되서, 4를 반환한다. 
문제는 FAT은 연속할당이 아니라서 아마 inode_disk의 배열에서 찾아야될 것 같다.  */
static inline size_t
bytes_to_sectors(off_t size)
{
	return DIV_ROUND_UP(size, DISK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode
{
	// memory 상의 inode 구조체이다. elem이 있으니 어딘가에 inode를 관리하는 list가 있지 않을까..?

	struct list_elem elem; /* Element in inode list. */

	// inode_disk가 있는 곳을 말하는 것일듯?
	disk_sector_t sector; /* Sector number of disk location. */
	int open_cnt;		  /* Number of openers. */

	// removed는 inode가 지워졌는지 아닐까..
	bool removed;			/* True if deleted, false otherwise. */
	int deny_write_cnt;		/* 0: writes ok, >0: deny writes. */
	struct inode_disk data; /* Inode content. */
};

/* Returns the disk sector that contains byte offset POS within
 * INODE.
 * Returns -1 if INODE does not contain data for a byte at offset
 * POS. */
/* byte를 pos로 받는다. 그래서 그에 해당하는 sector가 어디인지를 검색한다.
	byte_to_sector는, inode와 offset을 받았을 때, 해당 offset에 해당하는 sector를 반환 */
static disk_sector_t
byte_to_sector(const struct inode *inode, off_t pos)
{
	/* ---------------------------- >> Project.4 FAT >> ---------------------------- */
	// 여기 구현해야 함
	ASSERT(inode != NULL);
	if(pos < inode->data.length / DISK_SECTOR_SIZE){
		cluster_t cur_cluster = inode->data.start;
		uint32_t cnt = pos / DISK_SECTOR_SIZE;
		for (int i = 0 ; i < cnt ; i++){
			cur_cluster = fat_get(cur_cluster); // next_cluster를 cur_cluster로 업데이트
		}
		return cur_cluster;
	}
	else{
		return -1;
	}
	/* ---------------------------- << Project.4 FAT << ---------------------------- */

	// if (pos < inode->data.length)
	// 	return inode->data.start + pos / DISK_SECTOR_SIZE;
	// else
	// 	return -1;
}

/* List of open inodes, so that opening a single inode twice
 * returns the same `struct inode'. */

// 열려진 inode들을 관리하는 list이다.
// inode의 elem을 여기다 넣으면 될 것 같다.
static struct list open_inodes;

/* Initializes the inode module. */
void inode_init(void)
{
	init(&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
 * writes the new inode to sector SECTOR on the file system
 * disk.
 * Returns true if successful.
 * Returns false if memory or disk allocation fails. */
bool inode_create(disk_sector_t sector, off_t length)
{
	//  file의 길이를 받으면, 그거를 sector수로 반환하고
	// (bytes_to_sectors() 함수가 있다!) 비어있는 sector에 그것을 쓴다.
	// 문제는 이 역시 연속 할당이 아니기 때문에 비어있는 sector를 가져와서 넣는 식으로 해야될 것이다.

	struct inode_disk *disk_inode = NULL;
	bool success = false;

	ASSERT(length >= 0);

	/* If this assertion fails, the inode structure is not exactly
	 * one sector in size, and you should fix that. */
	ASSERT(sizeof *disk_inode == DISK_SECTOR_SIZE);

	disk_inode = calloc(1, sizeof *disk_inode);
	if (disk_inode != NULL)
	{
		size_t sectors = bytes_to_sectors(length);
		disk_inode->length = length;
		disk_inode->magic = INODE_MAGIC;

		
		if (free_map_allocate(sectors, &disk_inode->start))
		{
			disk_write(filesys_disk, sector, disk_inode);
			if (sectors > 0)
			{
				static char zeros[DISK_SECTOR_SIZE];
				size_t i;

				for (i = 0; i < sectors; i++)
					disk_write(filesys_disk, disk_inode->start + i, zeros);
			}
			success = true;
		}
		free(disk_inode);
	}
	return success;
}

/* Reads an inode from SECTOR
 * and returns a `struct inode' that contains it.
 * Returns a null pointer if memory allocation fails. */
struct inode *
inode_open(disk_sector_t sector)
{ // inode_disk가 있는 sector에서 데이터를 읽어와서 inode에 저장하는 함수이다.
	struct list_elem *e;
	struct inode *inode;

	/* Check whether this inode is already open. */
	for (e = list_begin(&open_inodes); e != list_end(&open_inodes);
		 e = list_next(e))
	{
		inode = list_entry(e, struct inode, elem);
		if (inode->sector == sector)
		{
			inode_reopen(inode);
			return inode;
		}
	}

	/* Allocate memory. */
	inode = malloc(sizeof *inode);
	if (inode == NULL)
		return NULL;

	/* Initialize. */
	list_push_front(&open_inodes, &inode->elem);
	inode->sector = sector;
	inode->open_cnt = 1;
	inode->deny_write_cnt = 0;
	inode->removed = false;
	disk_read(filesys_disk, inode->sector, &inode->data);
	return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen(struct inode *inode)
{
	if (inode != NULL)
		inode->open_cnt++;
	return inode;
}

/* Returns INODE's inode number. */
disk_sector_t
inode_get_inumber(const struct inode *inode)
{
	return inode->sector;
}

/* Closes INODE and writes it to disk.
 * If this was the last reference to INODE, frees its memory.
 * If INODE was also a removed inode, frees its blocks. */
// inode를 닫고, 갱신사항을 inode_disk에 적는다.
void inode_close(struct inode *inode)
{

	/* Ignore null pointer. */
	if (inode == NULL)
		return;

	/* Release resources if this was the last opener. */
	if (--inode->open_cnt == 0)
	{
		/* Remove from inode list and release lock. */
		list_remove(&inode->elem);

		/* Deallocate blocks if removed. */
		if (inode->removed)
		{
			free_map_release(inode->sector, 1);
			free_map_release(inode->data.start,
							 bytes_to_sectors(inode->data.length));
		}

		free(inode);
	}
}

/* Marks INODE to be deleted when it is closed by the last caller who
 * has it open. */
//  inode가 삭제된다면 이를 체크하고, 이를 이용해서 close할 때, free까지 해준다.
void inode_remove(struct inode *inode)
{
	ASSERT(inode != NULL);
	inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
 * Returns the number of bytes actually read, which may be less
 * than SIZE if an error occurs or end of file is reached. */
/* byte를 pos로 받는다. 그래서 그에 해당하는 sector가 어디인지를 검색한다.
	byte_to_sector는, inode와 offset을 받았을 때, 해당 offset에 해당하는 sector를 반환
	반환받은 그 sector에서부터 size만큼 disk에 쓴다. 하지만 project4에선 연속할당이 아니기
	때문에 sector_idx를 고쳐야 함 */
off_t inode_read_at(struct inode *inode, void *buffer_, off_t size, off_t offset)
{ // 요약하면 disk에 있는 file을 inode로 찾아서, 해당 offset에서 size만큼 읽어온다는 뜻이다.

	uint8_t *buffer = buffer_;
	off_t bytes_read = 0;
	uint8_t *bounce = NULL;

	while (size > 0)
	{
		/* Disk sector to read, starting byte offset within sector. */
		disk_sector_t sector_idx = byte_to_sector(inode, offset);
		int sector_ofs = offset % DISK_SECTOR_SIZE;

		/* Bytes left in inode, bytes left in sector, lesser of the two. */
		// inode_length : inode가 가리키는 file의 총 크기를 의미
		off_t inode_left = inode_length(inode) - offset;
		int sector_left = DISK_SECTOR_SIZE - sector_ofs;
		int min_left = inode_left < sector_left ? inode_left : sector_left;

		/* Number of bytes to actually copy out of this sector. */
		int chunk_size = size < min_left ? size : min_left;
		if (chunk_size <= 0)
			break;

		if (sector_ofs == 0 && chunk_size == DISK_SECTOR_SIZE)
		{
			/* Read full sector directly into caller's buffer. */
			disk_read(filesys_disk, sector_idx, buffer + bytes_read);
		}
		else
		{
			/* Read sector into bounce buffer, then partially copy
			 * into caller's buffer. */
			if (bounce == NULL)
			{
				bounce = malloc(DISK_SECTOR_SIZE);
				if (bounce == NULL)
					break;
			}
			disk_read(filesys_disk, sector_idx, bounce);
			memcpy(buffer + bytes_read, bounce + sector_ofs, chunk_size);
		}

		/* Advance. */
		size -= chunk_size;
		offset += chunk_size;
		bytes_read += chunk_size;
	}
	free(bounce);

	return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
 * Returns the number of bytes actually written, which may be
 * less than SIZE if end of file is reached or an error occurs.
 * (Normally a write at end of file would extend the inode, but
 * growth is not yet implemented.) */
off_t inode_write_at(struct inode *inode, const void *buffer_, off_t size,
					 off_t offset)
{ // inode_read_at이랑 비슷하겠지만 file 뒷부분이나 중간에 data가
	// 새로 들어오는 것 까지 고려해야 한다.
	const uint8_t *buffer = buffer_;
	off_t bytes_written = 0;
	uint8_t *bounce = NULL;

	if (inode->deny_write_cnt)
		return 0;

	while (size > 0)
	{
		/* Sector to write, starting byte offset within sector. */
		disk_sector_t sector_idx = byte_to_sector(inode, offset);
		int sector_ofs = offset % DISK_SECTOR_SIZE;

		/* Bytes left in inode, bytes left in sector, lesser of the two. */
		off_t inode_left = inode_length(inode) - offset;
		int sector_left = DISK_SECTOR_SIZE - sector_ofs;
		int min_left = inode_left < sector_left ? inode_left : sector_left;

		/* Number of bytes to actually write into this sector. */
		int chunk_size = size < min_left ? size : min_left;
		if (chunk_size <= 0)
			break;

		if (sector_ofs == 0 && chunk_size == DISK_SECTOR_SIZE)
		{
			/* Write full sector directly to disk. */
			disk_write(filesys_disk, sector_idx, buffer + bytes_written);
		}
		else
		{
			/* We need a bounce buffer. */
			if (bounce == NULL)
			{
				bounce = malloc(DISK_SECTOR_SIZE);
				if (bounce == NULL)
					break;
			}

			/* If the sector contains data before or after the chunk
			   we're writing, then we need to read in the sector
			   first.  Otherwise we start with a sector of all zeros. */
			if (sector_ofs > 0 || chunk_size < sector_left)
				disk_read(filesys_disk, sector_idx, bounce);
			else
				memset(bounce, 0, DISK_SECTOR_SIZE);
			memcpy(bounce + sector_ofs, buffer + bytes_written, chunk_size);
			disk_write(filesys_disk, sector_idx, bounce);
		}

		/* Advance. */
		size -= chunk_size;
		offset += chunk_size;
		bytes_written += chunk_size;
	}
	free(bounce);

	return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void inode_deny_write(struct inode *inode)
{
	inode->deny_write_cnt++;
	ASSERT(inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
 * Must be called once by each inode opener who has called
 * inode_deny_write() on the inode, before closing the inode. */
void inode_allow_write(struct inode *inode)
{
	// printf("====================inode_allow_write =%d======================",inode->deny_write_cnt);
	ASSERT(inode->deny_write_cnt > 0);
	ASSERT(inode->deny_write_cnt <= inode->open_cnt);

	// deny_write_cnt > inode->open_cnt
	inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t inode_length(const struct inode *inode)
{
	return inode->data.length;
}
