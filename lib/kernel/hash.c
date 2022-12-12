/* Hash table.

   This data structure is thoroughly documented in the Tour of
   Pintos for Project 3.

   See hash.h for basic information. */

#include "hash.h"
#include "../debug.h"
#include "threads/malloc.h"
#include "vm/vm.h"

#define list_elem_to_hash_elem(LIST_ELEM)                       \
	list_entry(LIST_ELEM, struct hash_elem, list_elem)

static struct list *find_bucket (struct hash *, struct hash_elem *);
static struct hash_elem *find_elem (struct hash *, struct list *,
		struct hash_elem *);
static void insert_elem (struct hash *, struct list *, struct hash_elem *);
static void remove_elem (struct hash *, struct hash_elem *);
static void rehash (struct hash *);

/* Initializes hash table H to compute hash values using HASH and
   compare hash elements using LESS, given auxiliary data AUX. */
bool
hash_init (struct hash *h,
		hash_hash_func *hash, hash_less_func *less, void *aux) {
			// 인자로 받는 함수들로 해시 테이블의 해시들을 초기화 함
	h->elem_cnt = 0;
	h->bucket_cnt = 4;

	// 성공하면 메모리로 할당받고, 실패하면 메모리에 할당안됨
	h->buckets = malloc (sizeof *h->buckets * h->bucket_cnt);
	h->hash = hash;
	h->less = less;
	h->aux = aux;

	if (h->buckets != NULL) {
		hash_clear (h, NULL);
		return true;
	} else
		return false;
}

/* Removes all the elements from H.

   If DESTRUCTOR is non-null, then it is called for each element
   in the hash.  DESTRUCTOR may, if appropriate, deallocate the
   memory used by the hash element.  However, modifying hash
   table H while hash_clear() is running, using any of the
   functions hash_clear(), hash_destroy(), hash_insert(),
   hash_replace(), or hash_delete(), yields undefined behavior,
   whether done in DESTRUCTOR or elsewhere. */
/* hash에서 모든 elem 제거 ( 반드시 이 hash는 hash_init()을 거쳤어야 함)
*/
void
hash_clear (struct hash *h, hash_action_func *destructor) { 
	size_t i;

	for (i = 0; i < h->bucket_cnt; i++) {
		struct list *bucket = &h->buckets[i];

		if (destructor != NULL)
			while (!list_empty (bucket)) {
				struct list_elem *list_elem = list_pop_front (bucket);
				struct hash_elem *hash_elem = list_elem_to_hash_elem (list_elem);
				destructor (hash_elem, h->aux);
			}

		list_init (bucket);
	}

	h->elem_cnt = 0;
}

/* Destroys hash table H.

   If DESTRUCTOR is non-null, then it is first called for each
   element in the hash.  DESTRUCTOR may, if appropriate,
   deallocate the memory used by the hash element.  However,
   modifying hash table H while hash_clear() is running, using
   any of the functions hash_clear(), hash_destroy(),
   hash_insert(), hash_replace(), or hash_delete(), yields
   undefined behavior, whether done in DESTRUCTOR or
   elsewhere. */
/* 인자 action이 존재한다면, hash안의 각 elem마다 action을 호출한다. 
   hash_clear와 다른 점은, hash 자체를 free시킨다는 점이다.
*/
void
hash_destroy (struct hash *h, hash_action_func *destructor) {
	if (destructor != NULL)
		hash_clear (h, destructor);
	free (h->buckets);
}

/* hash_destructor: hash_destroy의 action func로 인자로 받은 page를 destroy하고, free시킨다.
*/
void hash_destructor(struct hash_elem *e, void* aux) {
	struct page *free_page = hash_entry(e, struct page, hash_elem);
	vm_dealloc_page(free_page);	// destroy & free
}

/* Inserts NEW into hash table H and returns a null pointer, if
   no equal element is already in the table.
   If an equal element is already in the table, returns it
   without inserting NEW. */
struct hash_elem *
hash_insert (struct hash *h, struct hash_elem *new) {
	struct list *bucket = find_bucket (h, new); //적절한 버켓을 찾는다.
	struct hash_elem *old = find_elem (h, bucket, new);// 인자로 준 해시에서 해당 해쉬엘렘을 찾고 찾으면 해당 해시엘렘 반환, 못 찾으면 null포인터 반환.

	if (old == NULL)
		insert_elem (h, bucket, new);

	rehash (h);

	return old;
}

/* Inserts NEW into hash table H, replacing any equal element
   already in the table, which is returned. */
struct hash_elem *
hash_replace (struct hash *h, struct hash_elem *new) {
	struct list *bucket = find_bucket (h, new);
	struct hash_elem *old = find_elem (h, bucket, new);

	if (old != NULL)
		remove_elem (h, old);
	insert_elem (h, bucket, new);

	rehash (h);

	return old;
}

/* Finds and returns an element equal to E in hash table H, or a
   null pointer if no equal element exists in the table. */
struct hash_elem *
hash_find (struct hash *h, struct hash_elem *e) {
	return find_elem (h, find_bucket (h, e), e);
}

/* Finds, removes, and returns an element equal to E in hash
   table H.  Returns a null pointer if no equal element existed
   in the table.

   If the elements of the hash table are dynamically allocated,
   or own resources that are, then it is the caller's
   responsibility to deallocate them. */
struct hash_elem *
hash_delete (struct hash *h, struct hash_elem *e) {
	struct hash_elem *found = find_elem (h, find_bucket (h, e), e);
	if (found != NULL) {
		remove_elem (h, found);
		rehash (h);
	}
	return found;
}

/* Calls ACTION for each element in hash table H in arbitrary
   order.
   Modifying hash table H while hash_apply() is running, using
   any of the functions hash_clear(), hash_destroy(),
   hash_insert(), hash_replace(), or hash_delete(), yields
   undefined behavior, whether done from ACTION or elsewhere. */
void
hash_apply (struct hash *h, hash_action_func *action) {
	size_t i;

	ASSERT (action != NULL);

	for (i = 0; i < h->bucket_cnt; i++) {
		struct list *bucket = &h->buckets[i];
		struct list_elem *elem, *next;

		for (elem = list_begin (bucket); elem != list_end (bucket); elem = next) {
			next = list_next (elem);
			action (list_elem_to_hash_elem (elem), h->aux);
		}
	}
}

/* Initializes I for iterating hash table H.

   Iteration idiom:

   struct hash_iterator i;

   hash_first (&i, h);
   while (hash_next (&i))
   {
   struct foo *f = hash_entry (hash_cur (&i), struct foo, elem);
   ...do something with f...
   }

   Modifying hash table H during iteration, using any of the
   functions hash_clear(), hash_destroy(), hash_insert(),
   hash_replace(), or hash_delete(), invalidates all
   iterators. */
void
hash_first (struct hash_iterator *i, struct hash *h) {
	ASSERT (i != NULL);
	ASSERT (h != NULL);

	i->hash = h;
	i->bucket = i->hash->buckets;
	i->elem = list_elem_to_hash_elem (list_head (i->bucket));
}

/* Advances I to the next element in the hash table and returns
   it.  Returns a null pointer if no elements are left.  Elements
   are returned in arbitrary order.

   Modifying a hash table H during iteration, using any of the
   functions hash_clear(), hash_destroy(), hash_insert(),
   hash_replace(), or hash_delete(), invalidates all
   iterators. */
struct hash_elem *
hash_next (struct hash_iterator *i) {
	ASSERT (i != NULL);

	i->elem = list_elem_to_hash_elem (list_next (&i->elem->list_elem));
	while (i->elem == list_elem_to_hash_elem (list_end (i->bucket))) {
		if (++i->bucket >= i->hash->buckets + i->hash->bucket_cnt) {
			i->elem = NULL;
			break;
		}
		i->elem = list_elem_to_hash_elem (list_begin (i->bucket));
	}

	return i->elem;
}

/* Returns the current element in the hash table iteration, or a
   null pointer at the end of the table.  Undefined behavior
   after calling hash_first() but before hash_next(). */
struct hash_elem *
hash_cur (struct hash_iterator *i) {
	return i->elem;
}

/* Returns the number of elements in H. */
// hash안에 저장되어있는 elem 의 수를 반환함
size_t
hash_size (struct hash *h) {
	return h->elem_cnt;
}

/* Returns true if H contains no elements, false otherwise. */
// hash가 비어있다면 True 반환
bool
hash_empty (struct hash *h) {
	return h->elem_cnt == 0;
}

/* Fowler-Noll-Vo hash constants, for 32-bit word sizes. */
#define FNV_64_PRIME 0x00000100000001B3UL
#define FNV_64_BASIS 0xcbf29ce484222325UL

/* Returns a hash of the SIZE bytes in BUF. */
// 주어진 값을 주어진 size크기로 적당히 변환시키는 함수
uint64_t
hash_bytes (const void *buf_, size_t size) {
	/* Fowler-Noll-Vo 32-bit hash, for bytes. */
	const unsigned char *buf = buf_;
	uint64_t hash;

	ASSERT (buf != NULL);

	hash = FNV_64_BASIS;
	while (size-- > 0)
		hash = (hash * FNV_64_PRIME) ^ *buf++;

	return hash;
}

/* Returns a hash of string S. */
// null 이 제거된 strung s의 hash를 반환
uint64_t
hash_string (const char *s_) {
	const unsigned char *s = (const unsigned char *) s_;
	uint64_t hash;

	ASSERT (s != NULL);

	hash = FNV_64_BASIS;
	while (*s != '\0')
		hash = (hash * FNV_64_PRIME) ^ *s++;

	return hash;
}

/* Returns a hash of integer I. */
uint64_t
hash_int (int i) {
	return hash_bytes (&i, sizeof i);
}

/* Returns the bucket in H that E belongs in. */
static struct list *
find_bucket (struct hash *h, struct hash_elem *e) {
	size_t bucket_idx = h->hash (e, h->aux) & (h->bucket_cnt - 1);
	return &h->buckets[bucket_idx];
}

/* Searches BUCKET in H for a hash element equal to E.  Returns
   it if found or a null pointer otherwise. */
static struct hash_elem *
find_elem (struct hash *h, struct list *bucket, struct hash_elem *e) {
	struct list_elem *i;

	for (i = list_begin (bucket); i != list_end (bucket); i = list_next (i)) {
		struct hash_elem *hi = list_elem_to_hash_elem (i);
		if (!h->less (hi, e, h->aux) && !h->less (e, hi, h->aux))
			return hi;
	}
	return NULL;
}

/* Returns X with its lowest-order bit set to 1 turned off. */
static inline size_t
turn_off_least_1bit (size_t x) {
	return x & (x - 1);
}

/* Returns true if X is a power of 2, otherwise false. */
static inline size_t
is_power_of_2 (size_t x) {
	return x != 0 && turn_off_least_1bit (x) == 0;
}

/* Element per bucket ratios. */
#define MIN_ELEMS_PER_BUCKET  1 /* Elems/bucket < 1: reduce # of buckets. */
#define BEST_ELEMS_PER_BUCKET 2 /* Ideal elems/bucket. */
#define MAX_ELEMS_PER_BUCKET  4 /* Elems/bucket > 4: increase # of buckets. */

/* Changes the number of buckets in hash table H to match the
   ideal.  This function can fail because of an out-of-memory
   condition, but that'll just make hash accesses less efficient;
   we can still continue. */
static void
rehash (struct hash *h) {
	size_t old_bucket_cnt, new_bucket_cnt;
	struct list *new_buckets, *old_buckets;
	size_t i;

	ASSERT (h != NULL);

	/* Save old bucket info for later use. */
	old_buckets = h->buckets;
	old_bucket_cnt = h->bucket_cnt;

	/* Calculate the number of buckets to use now.
	   We want one bucket for about every BEST_ELEMS_PER_BUCKET.
	   We must have at least four buckets, and the number of
	   buckets must be a power of 2. */

	new_bucket_cnt = h->elem_cnt / BEST_ELEMS_PER_BUCKET;  //elem의 절반 개수 (elem_cnt / BEST_ELEMS_PER_BUCKET)
	if (new_bucket_cnt < 4) // elem의 절반 개수가 3일 때 까지는 새로운 bucket 개수를 4로 설정.
		new_bucket_cnt = 4;
	while (!is_power_of_2 (new_bucket_cnt)) // elem의 절반 개수를 2의 지수승으로 wile문의 조건으로 넣어줌.
		new_bucket_cnt = turn_off_least_1bit (new_bucket_cnt); // elem의 절반 개수가 2의 지수승이 될 때까지 1bit씩 내려줌.
		// elem개수 15개 bucket개수 4, 16일때 8 ,32일때 16 

	/* Don't do anything if the bucket count wouldn't change. */
	// 새 버킷 개수가 기존의 버킷 개수랑 같다면 더이상 해줄 작업이 없으므로 리턴.
	if (new_bucket_cnt == old_bucket_cnt)
		return;

	/* Allocate new buckets and initialize them as empty. */
	new_buckets = malloc (sizeof *new_buckets * new_bucket_cnt);  // 새 버켓들을 개수에 맞게 메모리 할당 받아줌.
	if (new_buckets == NULL) {
		/* Allocation failed.  This means that use of the hash table will
		   be less efficient.  However, it is still usable, so
		   there's no reason for it to be an error. */
		return;
	}
	for (i = 0; i < new_bucket_cnt; i++)
		list_init (&new_buckets[i]);    // 버켓들을 초기화 시켜줌.

	/* Install new bucket info. */
	h->buckets = new_buckets;
	h->bucket_cnt = new_bucket_cnt;

	/* Move each old element into the appropriate new bucket. */
	// 기존의 버켓 안의 내용들을 새로 만든 버켓들에 넣어줌.
	for (i = 0; i < old_bucket_cnt; i++) {
		struct list *old_bucket;
		struct list_elem *elem, *next;

		old_bucket = &old_buckets[i];
		for (elem = list_begin (old_bucket);
				elem != list_end (old_bucket); elem = next) {
			struct list *new_bucket
				= find_bucket (h, list_elem_to_hash_elem (elem));
			next = list_next (elem);
			list_remove (elem);
			list_push_front (new_bucket, elem);
		}
	}

	free (old_buckets);  //rehash 작업이 끝났으므로 기존 버켓 프리.
}

/* Inserts E into BUCKET (in hash table H). */
static void
insert_elem (struct hash *h, struct list *bucket, struct hash_elem *e) {
	h->elem_cnt++;
	list_push_front (bucket, &e->list_elem);
}

/* Removes E from hash table H. */
static void
remove_elem (struct hash *h, struct hash_elem *e) {
	h->elem_cnt--;
	list_remove (&e->list_elem);
}

