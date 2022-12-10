#ifndef VM_UNINIT_H
#define VM_UNINIT_H
#include "vm/vm.h"

struct page;
enum vm_type;

typedef bool vm_initializer (struct page *, void *aux);

/* Uninitlialized page. The type for implementing the
 * "Lazy loading". */
struct uninit_page { // 제일 처음 만들어진 페이지의 타입
	// page fault가 발생했을 때의 page의 타입이 변화하는데 변화하기 위해 필요한 정보(유전자정보??)를 가지고 있음.
	// page fault 시 page_initializer에 저장된 init 함수 호출
	/* Initiate the contets of the page */
	vm_initializer *init;	// lazy_load_segment()
	enum vm_type type;
	void *aux;
	/* Initiate the struct page and maps the pa to the va */
	bool (*page_initializer) (struct page *, enum vm_type, void *kva);
};

void uninit_new (struct page *page, void *va, vm_initializer *init,
		enum vm_type type, void *aux,
		bool (*initializer)(struct page *, enum vm_type, void *kva));
#endif
