#include "filesys/fat.h"
#include "devices/disk.h"
#include "filesys/filesys.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include <stdio.h>
#include <string.h>

/* Should be less than DISK_SECTOR_SIZE */
struct fat_boot {
	unsigned int magic;
	unsigned int sectors_per_cluster; /* Fixed to 1 */
	unsigned int total_sectors;
	unsigned int fat_start;
	unsigned int fat_sectors; /* Size of FAT in sectors. */
	unsigned int root_dir_cluster;
};

/* FAT FS */
// 한 파일 내에서 사용하는 FAT 라고 추정..
struct fat_fs {
	struct fat_boot bs; // 전체 파일 시스템의 정보가 담겨있는 듯?
	unsigned int *fat;
	unsigned int fat_length; // FS안에 들어가있는 sector의 갯수
	disk_sector_t data_start; // 비어있는 첫 sector?
	cluster_t last_clst; // 파일이 할당받은 cluster중, 마지막 cluster?
	struct lock write_lock;
};

static struct fat_fs *fat_fs;

void fat_boot_create (void);
void fat_fs_init (void);

void
fat_init (void) {
	// fat_fs는 메모리 상에 올라가 있다. 
	// 따라서 calloc (임의 사이즈를 여러번 malloc 받을 수 있도록) 을 받는다.
	fat_fs = calloc (1, sizeof (struct fat_fs));
	if (fat_fs == NULL)
		PANIC ("FAT init failed");

	// Read boot sector from the disk
	unsigned int *bounce = malloc (DISK_SECTOR_SIZE);
	if (bounce == NULL)
		PANIC ("FAT init failed");
	
	// FAT_BOOT_SECTOR 있는 내용을 bounce로 옮긴다.
	disk_read (filesys_disk, FAT_BOOT_SECTOR, bounce);
	memcpy (&fat_fs->bs, bounce, sizeof (fat_fs->bs));
	free (bounce);

	// Extract FAT info
	if (fat_fs->bs.magic != FAT_MAGIC)
		fat_boot_create (); // fat_fs -> bs 를 만들어 줌
	fat_fs_init (); // fat_fs 를 만듬
}

void
fat_open (void) {
	fat_fs->fat = calloc (fat_fs->fat_length, sizeof (cluster_t));
	if (fat_fs->fat == NULL)
		PANIC ("FAT load failed");

	// Load FAT directly from the disk
	uint8_t *buffer = (uint8_t *) fat_fs->fat; // buffer와 fat을 일치시킴
	off_t bytes_read = 0;
	off_t bytes_left = sizeof (fat_fs->fat);
	const off_t fat_size_in_bytes = fat_fs->fat_length * sizeof (cluster_t); // 말 그대로 fat의 크기
	for (unsigned i = 0; i < fat_fs->bs.fat_sectors; i++) {
		bytes_left = fat_size_in_bytes - bytes_read;
		if (bytes_left >= DISK_SECTOR_SIZE) {
			disk_read (filesys_disk, fat_fs->bs.fat_start + i,
			           buffer + bytes_read);
			bytes_read += DISK_SECTOR_SIZE;
		} else {
			uint8_t *bounce = malloc (DISK_SECTOR_SIZE);
			if (bounce == NULL)
				PANIC ("FAT load failed");
			disk_read (filesys_disk, fat_fs->bs.fat_start + i, bounce);
			memcpy (buffer + bytes_read, bounce, bytes_left);
			bytes_read += bytes_left;
			free (bounce);
		}
	}
}

void
fat_close (void) {
	// Write FAT boot sector
	uint8_t *bounce = calloc (1, DISK_SECTOR_SIZE);
	if (bounce == NULL)
		PANIC ("FAT close failed");

	// bounce를 FAT_BOOT_SECTOR에 옮겨쓴다. 
	// 이러면 FAT_BOOT_SECTOR는 bs와 동일한 것이라는 것을 알 수 있다.
	memcpy (bounce, &fat_fs->bs, sizeof (fat_fs->bs));
	disk_write (filesys_disk, FAT_BOOT_SECTOR, bounce);
	free (bounce);

	// Write FAT directly to the disk
	uint8_t *buffer = (uint8_t *) fat_fs->fat;
	off_t bytes_wrote = 0;
	off_t bytes_left = sizeof (fat_fs->fat);
	const off_t fat_size_in_bytes = fat_fs->fat_length * sizeof (cluster_t);
	for (unsigned i = 0; i < fat_fs->bs.fat_sectors; i++) {
		bytes_left = fat_size_in_bytes - bytes_wrote;
		if (bytes_left >= DISK_SECTOR_SIZE) {
			disk_write (filesys_disk, fat_fs->bs.fat_start + i,
			            buffer + bytes_wrote);
			bytes_wrote += DISK_SECTOR_SIZE;
		} else {
			bounce = calloc (1, DISK_SECTOR_SIZE);
			if (bounce == NULL)
				PANIC ("FAT close failed");
			memcpy (bounce, buffer + bytes_wrote, bytes_left);
			disk_write (filesys_disk, fat_fs->bs.fat_start + i, bounce);
			bytes_wrote += bytes_left;
			free (bounce);
		}
	}
}

void
fat_create (void) {
	// Create FAT boot
	fat_boot_create (); // fat_fs->bs를 만들어준다.
	fat_fs_init (); // fat_fs를 만들어준다.

	// Create FAT table
	// fat_fs->fat : cluster 크기 * cluster 갯수만큼의 메모리를 할당
	// 즉, fat에는 디스크의 cluster의 주소 정보가 모두 들어간다는 것
	fat_fs->fat = calloc (fat_fs->fat_length, sizeof (cluster_t));
	if (fat_fs->fat == NULL)
		PANIC ("FAT creation failed");

	// Set up ROOT_DIR_CLST
	// ROOT_DIR_CLUSTER : 1
	fat_put (ROOT_DIR_CLUSTER, EOChain);

	// Fill up ROOT_DIR_CLUSTER region with 0
	uint8_t *buf = calloc (1, DISK_SECTOR_SIZE);
	if (buf == NULL)
		PANIC ("FAT create failed due to OOM");
	disk_write (filesys_disk, cluster_to_sector (ROOT_DIR_CLUSTER), buf);
	free (buf);
}

void
fat_boot_create (void) {
	unsigned int fat_sectors =
	    (disk_size (filesys_disk) - 1)
	    / (DISK_SECTOR_SIZE / sizeof (cluster_t) * SECTORS_PER_CLUSTER + 1) + 1;
	fat_fs->bs = (struct fat_boot){
	    .magic = FAT_MAGIC, // 오염 판단 기준
	    .sectors_per_cluster = SECTORS_PER_CLUSTER, // 1
	    .total_sectors = disk_size (filesys_disk),
		// 0번 섹터는 디스크의 정보를 가지고 있음.
		// 1번 섹터는 root_dir를 말하는데 여기서부터 시작해도 되나..?
	    .fat_start = 1,
	    .fat_sectors = fat_sectors, // Size of FAT in sectors
	    .root_dir_cluster = ROOT_DIR_CLUSTER, // 1
	};
}

void
fat_fs_init (void) {
	/* TODO: Your code goes here. */
		/* TODO: Your code goes here. */

	/*fat_length: 파일 시스템에 얼마나 클러스터가 많은 지를 저장*/
	fat_fs->fat_length = fat_fs->bs.total_sectors /SECTORS_PER_CLUSTER;
	/*data_start: 파일 저장 시작할 수 있는 섹터 위치 저장 => DATA Sector 시작 지점*/
	fat_fs->data_start = fat_fs->bs.fat_start + fat_fs->bs.fat_sectors;
}

/*----------------------------------------------------------------------------*/
/* FAT handling                                                               */
/*----------------------------------------------------------------------------*/

/* Add a cluster to the chain.
 * If CLST is 0, start a new chain.
 * Returns 0 if fails to allocate a new cluster. */
// 인자 clst에 새로운 clst 추가
cluster_t
fat_create_chain (cluster_t clst) {
	/* TODO: Your code goes here. */
	/* TODO: Your code goes here. */
	// clst(클러스터 인덱싱 번호)로 특정된 클러스터의 뒤에 클러스터를 추가하여 체인을 확장함
	// 새로 할당된 클러스터의 번호를 반환합니다.

	// clst가 0이면, 새 체인을 만든다 
	if (clst == 0) {
		// 새 체인 만들기
		for (cluster_t i = fat_fs->bs.fat_start; i<fat_fs->fat_length; i++) {// i는 1부터 fat_length만큼 
			if (fat_get(i) == 0) {	// fat에서 값이 0인(빈) 클러스터를 찾아서 새로 체인을 만든다.
				fat_put(i, EOChain);
				return i;
			}
		}
	}

	// clst가 0이 아니면, clst 클러스터 뒤에 클러스터를 추가한다. 
	else {		
		// clst 클러스터는 항상 마지막 클러스터이어야 함
		cluster_t next_clst_idx = fat_get(clst);	
		if (next_clst_idx != EOChain) {	\
			return 0;
		}

		// 빈 클러스터 찾기
		cluster_t new_clst; 
		for (int i = fat_fs->bs.fat_start; i<fat_fs->fat_length; i++) {
			if (fat_get(i) == 0) {	
				new_clst = i;
				break;
			}
		}
		// clst 클러스터 뒤에 클러스터를 추가
		fat_put(clst, new_clst);
		fat_put(new_clst, EOChain);
		return new_clst;	// 새로 할당된 클러스터의 번호를 반환
}

/* Remove the chain of clusters starting from CLST.
 * If PCLST is 0, assume CLST as the start of the chain. */
void
fat_remove_chain (cluster_t clst, cluster_t pclst) {
	/* TODO: Your code goes here. */
}

/* Update a value in the FAT table. */
void
fat_put (cluster_t clst, cluster_t val) {
	/* TODO: Your code goes here. */
}

/* Fetch a value in the FAT table. */
cluster_t
fat_get (cluster_t clst) {
	/* TODO: Your code goes here. */
}

/* Covert a cluster # to a sector number. */
disk_sector_t
cluster_to_sector (cluster_t clst) {
	/* TODO: Your code goes here. */
}
