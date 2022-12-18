#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "devices/disk.h"
#include "filesys/fat.h"
#include "threads/thread.h"

/* The disk that contains the file system. */
struct disk *filesys_disk;

static void do_format(void);

/* Initializes the file system module.
 * If FORMAT is true, reformats the file system. */
void filesys_init(bool format)
{
	filesys_disk = disk_get(0, 1);
	if (filesys_disk == NULL)
		PANIC("hd0:1 (hdb) not present, file system initialization failed");

	inode_init();

#ifdef EFILESYS
	fat_init();

	if (format)
		do_format();

	fat_open();
	//------project4-start---------------------------------------------------
	// 루트디렉토리 설정
	thread_current()->cur_dir = dir_open_root(); // dir_open_root: 루트 디렉토리 정보 반환
												 //------project4-end-----------------------------------------------------

#else
	/* Original FS */
	free_map_init();

	if (format)
		do_format();

	free_map_open();
#endif
}

/* Shuts down the file system module, writing any unwritten data
 * to disk. */
void filesys_done(void)
{
	/* Original FS */
#ifdef EFILESYS
	fat_close();
#else
	free_map_close();
#endif
}

/* Creates a file named NAME with the given INITIAL_SIZE.
 * Returns true if successful, false otherwise.
 * Fails if a file named NAME already exists,∏
 * or if internal memory allocation fails. */
/* 지정된 INITIAL_SIZE를 사용하여 NAME이라는 이름의 파일을 만든다.
   성공하면 true를 반환하고, 그렇지 않으면 false를 반환
   이름이 NAME인 파일이 이미 있거나 내부 메모리 할당이 실패한 경우 실패 */
bool filesys_create(const char *name, off_t initial_size)
{
	//------project4-start------------------------
	cluster_t new_cluster = fat_create_chain(0); // inode를 위한 새로운 cluster 만들기
	if (new_cluster == 0)
		return false;
	disk_sector_t inode_sector = cluster_to_sector(new_cluster); // 새로 만든 cluster의 disk sector

	// 메모리에 root 디렉터리 inode 자료구조 생성
	struct dir *dir = dir_open_root(); // 수정 해야 함????????????

	bool success = (dir != NULL && inode_create(inode_sector, initial_size, 0) && dir_add(dir, name, inode_sector));
	// dir_add :inode 만들고, dir에 inode 추가
	if (!success)
	{
		fat_remove_chain(new_cluster, 0); // 성공 못했을 시 예외처리
	}

	dir_close(dir);
	//------project4-end--------------------------

	////// 기존 코드 start
	// disk_sector_t inode_sector = 0;
	// struct dir *dir = dir_open_root();
	// bool success = (dir != NULL && free_map_allocate(1, &inode_sector) && inode_create(inode_sector, initial_size) && dir_add(dir, name, inode_sector));
	// if (!success && inode_sector != 0)
	// 	free_map_release(inode_sector, 1);
	// dir_close(dir);
	////// 기존 코드 end

	return success;
}

/* Opens the file with the given NAME.
 * Returns the new file if successful or a null pointer
 * otherwise.
 * Fails if no file named NAME exists,
 * or if an internal memory allocation fails. */
/* 성공하면 새 파일을 반환하고 그렇지 않으면 null 포인터를 반환
   이름이 NAME인 파일이 없거나 내부 메모리 할당이 실패할 경우 실패*/
struct file *
filesys_open(const char *name)
{
	// printf("[filesys_open] 들어옴\n");
	struct dir *dir = dir_open_root();
	struct inode *inode = NULL;

	if (dir != NULL)
		dir_lookup(dir, name, &inode);
	dir_close(dir);

	// printf("[filesys_open] 나간다\n");
	return file_open(inode);
}

/* Deletes the file named NAME.
 * Returns true if successful, false on failure.
 * Fails if no file named NAME exists,
 * or if an internal memory allocation fails. */
bool filesys_remove(const char *name)
{
	struct dir *dir = dir_open_root();
	bool success = dir != NULL && dir_remove(dir, name);
	dir_close(dir);

	return success;
}

/* Formats the file system. */
static void
do_format(void) // 수정 요망 (subdirectory part 구현)
{
	printf("Formatting file system...");

#ifdef EFILESYS
	/* Create FAT and save it to the disk. */
	fat_create(); // root dir 만들어야 함
	//------project4-start------------------------
	if (!dir_create(cluster_to_sector(ROOT_DIR_CLUSTER), 16))
		PANIC("root directory creation failed");

	// 파일 시스템 포맷 시 root 디렉토리 엔트리에 '.', '..'파일을 추가
	struct dir* root_dir = dir_open_root();
	dir_add(root_dir,".",ROOT_DIR_SECTOR);
	dir_add(root_dir,"..",ROOT_DIR_SECTOR);
	dir_close(root_dir);
	
	//------project4-end--------------------------
	fat_close();
#else
	free_map_create();
	if (!dir_create(ROOT_DIR_SECTOR, 16))
		PANIC("root directory creation failed");
	free_map_close();
#endif

	printf("done.\n");
}

//------project4-subdirectory start-----------------------
/* 경로 분석 함수 구현
	return : path_name을 분석하여 작업하는 디렉터리의 정보 포인터를 반환
	*file_name : path_name을 분석하여 파일, 디렉터리의 이름을 포인팅
	path_name의 시작이 ‘/’의 여부에 따라 절대, 상대경로 구분하여 디렉터리 정보를 dir에 저장
	strtok_r() 함수를 이용하여 path_name의 디렉터리 정보와 파일 이름 저장
	file_name에 파일 이름 저장
	dir로 오픈된 디렉터리를 포인팅 
*/
struct dir *parse_path(char *path_name, char *file_name)
{
  // file_name : path_name을 분석하여 파일, 디렉터리의 이름을 
	struct dir *dir;
	// printf("=======================parse_path 진입\n");
	// printf("==============path_name: %s", path_name);
	if (path_name == NULL || file_name == NULL) 
		goto fail;

	// path_name의 절대/상대 경로에 따른 디렉터리 정보 저장
	if (path_name[0] == '/'){
		dir = dir_open_root();
	}
	else {
		dir = dir_reopen(thread_current()->cur_dir);
	}

	/* PATH_NAME의 절대/상대경로에 따른 디렉터리 정보 저장 (구현)*/ 
	char *token, *nextToken, *savePtr;
	token = strtok_r(path_name, "/", &savePtr);
	nextToken = strtok_r(NULL, "/", &savePtr);

    // "/"를 open하려는 케이스
    if(token == NULL) { // file_name이 '/' 일 때 유닉스에서 root로 감
        token = (char*)malloc(2);
		// 현재 디렉토리
        strlcpy(token, ".", 2);
    }

	// "/"를 open하려는 케이스
	if (token == NULL){
		token = (char*)malloc(2);
		strlcpy(token, ".", 2);
	}

	struct inode* inode;
	while (token != NULL && nextToken != NULL){
		/* dir에서 token이름의 파일을 검색하여 inode의 정보를 저장*/ 
		if (!dir_lookup(dir,token, &inode)){
			dir_close(dir);
			return NULL;
		}

		struct inode* inode = dir_get_inode(dir);   //보류 

		/* inode가 파일일 경우 NULL 반환 */
		if(inode_is_dir(inode) == 0) {
			dir_close(dir);
			inode_close(inode);
			return NULL;
		}

		/* dir의 디렉터리 정보를 메모리에서 해지*/
		dir_close(dir);
		
		/* inode의 디렉터리 정보를 dir에 저장 */
		dir = dir_open(inode);
		/* token에 검색할 경로 이름 저장 */
		token = nextToken;
		nextToken = strtok_r(NULL ,"/", &savePtr);
		//token = trtok_r (NULL, "/", &savePtr);

	}
	// token의 파일 이름을 file_name에 저장
	strlcpy(file_name, token, strlen(token) + 1);
	/* dir 정보 반환 */ 
	return dir;
}

//------project4-subdirectory end--------------------------