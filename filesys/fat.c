#include "filesys/fat.h"
#include "devices/disk.h"
#include "filesys/filesys.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include <stdio.h>
#include <string.h>

/* Should be less than DISK_SECTOR_SIZE */
struct fat_boot {	// booting시 fat 정보를 담는 구조체
	unsigned int magic;					// overflow 감지
	unsigned int sectors_per_cluster; /* Fixed to 1 */
	unsigned int total_sectors;			// disk의 모든 sector 수
	unsigned int fat_start;				// fat가 시작하는 sector number
	unsigned int fat_sectors; /* Size of FAT in sectors. */	// fat가 차지하는 sector 수
	unsigned int root_dir_cluster;		// root dir의 clst number
};
       
/* FAT FS */
struct fat_fs {	// fat 파일시스템 정보를 담고 있는 구조체
	struct fat_boot bs;
	unsigned int *fat;		// calloc으로 fat_length 크기만큼 할당받은 fat 배열의 주소
	unsigned int fat_length;
	disk_sector_t data_start;
	cluster_t last_clst;
	struct lock write_lock;
};

static struct fat_fs *fat_fs;

void fat_boot_create (void);
void fat_fs_init (void);

void
fat_init (void) {
	fat_fs = calloc (1, sizeof (struct fat_fs));
	if (fat_fs == NULL)
		PANIC ("FAT init failed");

	// Read boot sector from the disk
	unsigned int *bounce = malloc (DISK_SECTOR_SIZE);
	if (bounce == NULL)
		PANIC ("FAT init failed");
	disk_read (filesys_disk, FAT_BOOT_SECTOR, bounce);
	memcpy (&fat_fs->bs, bounce, sizeof (fat_fs->bs));
	free (bounce);

	// Extract FAT info
	if (fat_fs->bs.magic != FAT_MAGIC)
		fat_boot_create ();
	fat_fs_init ();
}

void
fat_open (void) {
	fat_fs->fat = calloc (fat_fs->fat_length, sizeof (cluster_t));
	if (fat_fs->fat == NULL)
		PANIC ("FAT load failed");

	// Load FAT directly from the disk
	uint8_t *buffer = (uint8_t *) fat_fs->fat;
	off_t bytes_read = 0;
	off_t bytes_left = sizeof (fat_fs->fat);
	const off_t fat_size_in_bytes = fat_fs->fat_length * sizeof (cluster_t);
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
	fat_boot_create ();
	fat_fs_init ();

	// Create FAT table
	fat_fs->fat = calloc (fat_fs->fat_length, sizeof (cluster_t));
	if (fat_fs->fat == NULL)
		PANIC ("FAT creation failed");

	// Set up ROOT_DIR_CLST
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
	    .magic = FAT_MAGIC,
	    .sectors_per_cluster = SECTORS_PER_CLUSTER,
	    .total_sectors = disk_size (filesys_disk),
	    .fat_start = 1,
	    .fat_sectors = fat_sectors,
	    .root_dir_cluster = ROOT_DIR_CLUSTER,
	};
}

//------project4-start---------------------------------------------------
void
fat_fs_init (void) {
	/* TODO: Your code goes here. */
	// fat_fs`의 `fat_length`와 `data_start` 필드를 초기화
	// fat_length`는 파일 시스템의 클러스터 수를 저장함 
	// `data_start`는 파일이 들어있는 시작 섹터를 저장합니다. 
	// fat_fs→bs`에 저장된 일부 값을 이용, 이 함수에서 다른 유용한 데이터들을 초기화할 수도 있음

	// 전체 cluster 수 -> (sector per cluster가 1임)
	fat_fs->fat_length = (fat_fs->bs.fat_sectors * DISK_SECTOR_SIZE) / (sizeof(cluster_t) * SECTORS_PER_CLUSTER);
	// fat_fs->fat_length = fat_fs->bs.total_sectors/SECTORS_PER_CLUSTER;	
	
	// 파일이 들어있는 시작 섹터 -> data 저장하는 시작지점
	fat_fs->data_start = fat_fs->bs.fat_start + fat_fs->bs.fat_sectors;		
}

/*----------------------------------------------------------------------------*/
/* FAT handling                                                               */
/*----------------------------------------------------------------------------*/

/* Add a cluster to the chain.
 * If CLST is 0, start a new chain.
 * Returns 0 if fails to allocate a new cluster. */
cluster_t
fat_create_chain (cluster_t clst) {
	/* TODO: Your code goes here. */
	// clst(클러스터 인덱싱 번호)로 특정된 클러스터의 뒤에 클러스터를 추가하여 체인을 확장함
	// 새로 할당된 클러스터의 번호를 반환합니다.

	// clst가 0이면, 새 체인을 만든다 
	if (clst == 0) {
		// fat에서 값이 0인(빈) 클러스터를 찾아서 새로 체인을 만든다.
		// for (cluster_t i = fat_fs->bs.fat_start; i<fat_fs->fat_length; i++) {// i는 1부터 fat_length만큼 
		for (cluster_t i = 2; i<fat_fs->fat_length; i++) {// i는 2부터 fat_length만큼 
			if (fat_get(i) == 0) {	
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
}

/* Remove the chain of clusters starting from CLST.
 * If PCLST is 0, assume CLST as the start of the chain. */
void
fat_remove_chain (cluster_t clst, cluster_t pclst) {
	/* TODO: Your code goes here. */
	// clst에서 시작하여, 체인에서 클러스터들을 제거합니다. 
	// pclst는 체인에서 clst의 바로 이전 클러스터여야 합니다. 
	// 즉, 이 함수가 실행된 후에,pclst는 업데이트된 체인의 마지막 요소가 될 것입니다. 
	// 만약 clst가 체인의 첫 요소라면, pclst는 0이 되어야 합니다.
	
	if(pclst != 0) {	// clst가 체인의 첫 요소가 아니라면 if문 진입
		// pclst는 체인에서 clst의 바로 이전 클러스터여야 함
		if(fat_get(pclst) != clst) {
			return;
		}
		// pclst가 체인의 마지막 요소가 되어야 함
		fat_put(pclst, EOChain);
	}

	// clst부터 체인에서 클러스터를 제거함
	while(true) {	
		cluster_t next_clst = fat_get(clst);
		fat_put(clst, 0);	// clst의 val을 0으로 바꾼다. 
		if (next_clst == EOChain) break;	// 마지막 클러스터이라면 break
		clst = next_clst;
	}	
}

/* Update a value in the FAT table. */
void
fat_put (cluster_t clst, cluster_t val) {
	/* TODO: Your code goes here. */
	// 클러스터 번호 clst가 가리키는 FAT 엔트리를 val로 업데이트합니다. 
	// FAT의 각 엔트리가 체인의 다음 클러스터를 가리키므로, (존재하는 경우; 그렇지 않다면 EOChain) 
	// 이는 연결을 업데이트하는데 사용할 수 있습니다. 
	fat_fs->fat[clst] = val;
}

/* Fetch a value in the FAT table. */
cluster_t
fat_get (cluster_t clst) {
	/* TODO: Your code goes here. */
	// 주어진 클러스터 clst 가 가리키는 클러스터 번호를 반환합니다.
	return fat_fs->fat[clst];
}

/* Covert a cluster # to a sector number. */
disk_sector_t
cluster_to_sector (cluster_t clst) {
	/* TODO: Your code goes here. */
	// 클러스터 번호 clst를 해당하는 섹터 번호로 변환하고, 반환합니다.
	return fat_fs->data_start + clst*SECTORS_PER_CLUSTER;
}

cluster_t
sector_to_cluster (disk_sector_t disk_sector) {
	return (disk_sector - fat_fs->data_start)/SECTORS_PER_CLUSTER;
}

//------project4-end-----------------------------------------------------