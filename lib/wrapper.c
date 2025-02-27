#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <bits/pthreadtypes.h>
#include <dlfcn.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <stdarg.h>

#include "../config.h"
#include "../shared/global-const.h"

#include "aleakd.h"

#include "backtrace.h"

#include "server-comm.h"

static int g_bIsInitializing = 0;
static int g_bIsBacktraceInitializing = 0;

static void* (*real_malloc)(size_t) = NULL;
static void* (*real_calloc)(size_t, size_t) = NULL;
static void  (*real_free)(void*) = NULL;
static void* (*real_realloc)(void*, size_t) = NULL;
static int   (*real_posix_memalign)(void** memptr, size_t alignment, size_t size);
static void* (*real_aligned_alloc )(size_t alignment, size_t size);
static void* (*real_memalign)(size_t alignment, size_t size);
static void* (*real_valloc)(size_t size);
static void* (*real_pvalloc)(size_t size);


// Dummy buffer to wrap initialization because dlsym can call malloc function
char tmpbuf[8192];
unsigned long tmppos = 0;
unsigned long tmpallocs = 0;

static int g_bSendBacktrace = 0;
static int g_bUseServerMessage = 1;

void* dummy_malloc(size_t size)
{
    if (tmppos + size >= sizeof(tmpbuf)){
		fprintf(stderr, "[aleakd] dummy_malloc fail due to buffer overflow\n");
    	exit(1);
    }
    void *retptr = tmpbuf + tmppos;
    tmppos += size;
    ++tmpallocs;
    return retptr;
}

void* dummy_calloc(size_t nmemb, size_t size)
{
    void *ptr = dummy_malloc(nmemb * size);
    unsigned int i = 0;
    for (; i < nmemb * size; ++i)
        *((char*)(ptr + i)) = '\0';
    return ptr;
}

void dummy_free(void *ptr)
{

}

int wrapper_init()
{
	int res = 0;

	if(g_bIsInitializing == 0)
	{
		g_bIsInitializing = 1;
		fprintf(stderr, "[aleakd] wrapper_init\n");

		g_bIsBacktraceInitializing = 1;
		backtrace_init();
		g_bIsBacktraceInitializing = 0;

		// Wrap functions
		real_malloc = dlsym(RTLD_NEXT, "malloc");
		if (NULL == real_malloc) {
			fprintf(stderr, "[aleakd] Error in `dlsym`: %s\n", dlerror());
		}
		fprintf(stderr, "[aleakd] malloc=%p\n", real_malloc);

		real_calloc = dlsym(RTLD_NEXT, "calloc");
		if (NULL == real_calloc) {
			fprintf(stderr, "[aleakd] Error in `dlsym`: %s\n", dlerror());
		}

		real_free = dlsym(RTLD_NEXT, "free");
		if (NULL == real_free) {
			fprintf(stderr, "[aleakd] Error in `dlsym`: %s\n", dlerror());
		}

		real_realloc = dlsym(RTLD_NEXT, "realloc");
		if (NULL == real_realloc) {
			fprintf(stderr, "[aleakd] Error in `dlsym`: %s\n", dlerror());
		}

		real_posix_memalign = dlsym(RTLD_NEXT, "posix_memalign");
		if (NULL == real_posix_memalign) {
			fprintf(stderr, "[aleakd] Error in `dlsym`: %s\n", dlerror());
		}

		real_aligned_alloc = dlsym(RTLD_NEXT, "aligned_alloc");
		if (NULL == real_aligned_alloc) {
			fprintf(stderr, "[aleakd] Error in `dlsym`: %s\n", dlerror());
		}

		real_memalign = dlsym(RTLD_NEXT, "memalign");
		if (NULL == real_memalign) {
			fprintf(stderr, "[aleakd] Error in `dlsym`: %s\n", dlerror());
		}

		real_valloc = dlsym(RTLD_NEXT, "valloc");
		if (NULL == real_valloc) {
			fprintf(stderr, "[aleakd] Error in `dlsym`: %s\n", dlerror());
		}

		real_pvalloc = dlsym(RTLD_NEXT, "pvalloc");
		if (NULL == real_pvalloc) {
			fprintf(stderr, "[aleakd] Error in `dlsym`: %s\n", dlerror());
		}

		if(g_bUseServerMessage){
			res = servercomm_init();
		}

		char* szBacktraceEnabled = getenv("ALEAKD_BACKTRACE");
		if(szBacktraceEnabled) {
			g_bSendBacktrace = atoi(szBacktraceEnabled);
		}else {
			g_bSendBacktrace = 1;
		}

		fprintf(stderr, "[aleakd] wrapper_init done\n");
	}

	return res;
}

void wrapper_dispose()
{
	if(g_bUseServerMessage){
		servercomm_dispose();
	}
}

void backtrace_send(void** listBacktraceAddr, int iBacktraceSize, int iOriginMsgNum)
{
	struct ServerMsgBacktraceV1 bt_msg;
	servercomm_msg_backtrace_init_v1(&bt_msg);
	bt_msg.data.origin_msg_num = iOriginMsgNum;
	servercomm_msg_backtrace_make(&bt_msg, listBacktraceAddr, iBacktraceSize);
	servercomm_msg_backtrace_send_v1(&bt_msg);

	// Send symbol infos if needed
	for(int i=0; i<iBacktraceSize; i++)
	{
		void* addr = listBacktraceAddr[i];

		// Optimize the number of symbol infos sent
		if(!backtrace_check_addr(addr))
		{
			const char *fname = NULL;
			void *fbase = NULL;
			const char *sfname = NULL;
			void *saddr = NULL;
			backtrace_get_infos(addr, &fname, &fbase, &sfname, &saddr);

			int iLen;

			struct ServerMsgSymbolInfosV1 sym_msg;
			servercomm_msg_symbolinfos_init_v1(&sym_msg);

			// Addr
			sym_msg.data.addr = (uint64_t) addr;
			// Object name
			if (fname) {
				iLen = strlen(fname);
				if (iLen > 0) {
					memcpy(sym_msg.data.object_name, fname, iLen);
				}
			}
			// Object addr
			sym_msg.data.object_addr = (uint64_t) fbase;
			// Symbol name
			if (sfname) {
				iLen = strlen(sfname);
				if (iLen > 0) {
					memcpy(sym_msg.data.symbol_name, sfname, iLen);
				}
			}
			// Symbol addr
			sym_msg.data.symbol_addr = (uint64_t) saddr;

			servercomm_msg_symbolinfos_send_v1(&sym_msg);
		}
	}
}

void *malloc(size_t size)
{
	void *p = NULL;

	aleakd_data_incr_alloc_number();
	int alloc_num = aleakd_data_get_alloc_number();

	if(!real_malloc){
		wrapper_init();
	}

	if(real_malloc){
		p = real_malloc(size);
	}else{
		p = dummy_malloc(size);
	}

	if(p){
		if(g_bIsBacktraceInitializing){
			// Backtrace initializing may generate a malloc call, we ignore it
			return p;
		}

		if(g_bUseServerMessage) {
			struct ServerMsgMemoryV1 msg;
			servercomm_msg_memory_init_v1(&msg);
			msg.header.msg_code = ALeakD_MsgCode_malloc;
			msg.data.alloc_num = (int64_t)alloc_num;
			msg.data.alloc_ptr = (int64_t)p;
			msg.data.alloc_size = (int64_t)size;
			servercomm_msg_memory_send_v1(&msg);

			if(g_bSendBacktrace) {
				void *listBacktraceAddr[BACKTRACE_MAX_SIZE];
				int iBacktraceSize = backtrace(listBacktraceAddr, BACKTRACE_MAX_SIZE);
				backtrace_send(listBacktraceAddr, iBacktraceSize, msg.header.msg_num);
			}
		}
	}

	return p;
}

void *calloc(size_t num, size_t size)
{
	aleakd_data_incr_alloc_number();
	int alloc_num = aleakd_data_get_alloc_number();

	if(!real_calloc){
		wrapper_init();
	}

	void *p = NULL;
	if(real_calloc){
		p = real_calloc(num, size);
	}else{
		p = dummy_calloc(num, size);
	}

	if(p){
		if(g_bUseServerMessage) {
			struct ServerMsgMemoryV1 msg;
			servercomm_msg_memory_init_v1(&msg);
			msg.header.msg_code = ALeakD_MsgCode_calloc;
			msg.data.alloc_num = (int64_t)alloc_num;
			msg.data.alloc_ptr = (int64_t)p;
			msg.data.alloc_size = (int64_t)size;
			servercomm_msg_memory_send_v1(&msg);

			if(g_bSendBacktrace) {
				void *listBacktraceAddr[BACKTRACE_MAX_SIZE];
				int iBacktraceSize = backtrace(listBacktraceAddr, BACKTRACE_MAX_SIZE);
				backtrace_send(listBacktraceAddr, iBacktraceSize, msg.header.msg_num);
			}
		}
	}

	return p;
}

void *realloc(void* ptr, size_t size)
{
	if(!real_realloc){
		wrapper_init();
	}

	aleakd_data_incr_alloc_number();
	int alloc_num = aleakd_data_get_alloc_number();

	void *p = NULL;
	p = real_realloc(ptr, size);
	
	if(p){
		if(g_bUseServerMessage) {
			struct ServerMsgMemoryV1 msg;
			servercomm_msg_memory_init_v1(&msg);
			msg.header.msg_code = ALeakD_MsgCode_realloc;
			msg.data.alloc_num = (int64_t)alloc_num;
			msg.data.alloc_ptr = (int64_t)p;
			msg.data.alloc_size = (int64_t)size;
			msg.data.free_ptr = (int64_t)ptr;
			servercomm_msg_memory_send_v1(&msg);

			if(g_bSendBacktrace) {
				void *listBacktraceAddr[BACKTRACE_MAX_SIZE];
				int iBacktraceSize = backtrace(listBacktraceAddr, BACKTRACE_MAX_SIZE);
				backtrace_send(listBacktraceAddr, iBacktraceSize, msg.header.msg_num);
			}
		}
	}

	return p;
}

void free(void *ptr)
{
	if(!real_free){
		wrapper_init();
	}

	if(ptr != NULL) {
		if(g_bUseServerMessage) {
			struct ServerMsgMemoryV1 msg;
			servercomm_msg_memory_init_v1(&msg);
			msg.header.msg_code = ALeakD_MsgCode_free;
			msg.data.free_ptr = (int64_t)ptr;
			servercomm_msg_memory_send_v1(&msg);

			if(g_bSendBacktrace) {
				void *listBacktraceAddr[BACKTRACE_MAX_SIZE];
				int iBacktraceSize = backtrace(listBacktraceAddr, BACKTRACE_MAX_SIZE);
				backtrace_send(listBacktraceAddr, iBacktraceSize, msg.header.msg_num);
			}
		}
	}

	if(real_free){
		real_free(ptr);
	}else{
		dummy_free(ptr);
	}
}


int posix_memalign(void** memptr, size_t alignment, size_t size)
{
	if(!real_posix_memalign){
		wrapper_init();
	}

	aleakd_data_incr_alloc_number();
	int alloc_num = aleakd_data_get_alloc_number();

	int ret;
	ret = real_posix_memalign(memptr, alignment, size);

	if(ret == 0){
		if(g_bUseServerMessage) {
			struct ServerMsgMemoryV1 msg;
			servercomm_msg_memory_init_v1(&msg);
			msg.header.msg_code = ALeakD_MsgCode_posix_memalign;
			msg.data.alloc_num = (int64_t)alloc_num;
			msg.data.alloc_ptr = (int64_t)*memptr;
			msg.data.alloc_size = (int64_t)size;
			msg.data.free_ptr = (int64_t)NULL;
			servercomm_msg_memory_send_v1(&msg);

			if(g_bSendBacktrace) {
				void *listBacktraceAddr[BACKTRACE_MAX_SIZE];
				int iBacktraceSize = backtrace(listBacktraceAddr, BACKTRACE_MAX_SIZE);
				backtrace_send(listBacktraceAddr, iBacktraceSize, msg.header.msg_num);
			}
		}
	}

	return ret;
}

void* aligned_alloc(size_t alignment, size_t size)
{
	if(!real_aligned_alloc){
		wrapper_init();
	}

	aleakd_data_incr_alloc_number();
	int alloc_num = aleakd_data_get_alloc_number();

	void *p = NULL;
	p = real_aligned_alloc(alignment, size);

	if(p){
		if(g_bUseServerMessage) {
			struct ServerMsgMemoryV1 msg;
			servercomm_msg_memory_init_v1(&msg);
			msg.header.msg_code = ALeakD_MsgCode_aligned_alloc;
			msg.data.alloc_num = (int64_t)alloc_num;
			msg.data.alloc_ptr = (int64_t)p;
			msg.data.alloc_size = (int64_t)size;
			msg.data.free_ptr = (int64_t)NULL;
			servercomm_msg_memory_send_v1(&msg);

			if(g_bSendBacktrace) {
				void *listBacktraceAddr[BACKTRACE_MAX_SIZE];
				int iBacktraceSize = backtrace(listBacktraceAddr, BACKTRACE_MAX_SIZE);
				backtrace_send(listBacktraceAddr, iBacktraceSize, msg.header.msg_num);
			}
		}
	}

	return p;
}

void* memalign(size_t alignment, size_t size)
{
	if(!real_memalign){
		wrapper_init();
	}

	aleakd_data_incr_alloc_number();
	int alloc_num = aleakd_data_get_alloc_number();

	void *p = NULL;
	p = real_memalign(alignment, size);

	if(p){
		if(g_bUseServerMessage) {
			struct ServerMsgMemoryV1 msg;
			servercomm_msg_memory_init_v1(&msg);
			msg.header.msg_code = ALeakD_MsgCode_memalign;
			msg.data.alloc_num = (int64_t)alloc_num;
			msg.data.alloc_ptr = (int64_t)p;
			msg.data.alloc_size = (int64_t)size;
			msg.data.free_ptr = (int64_t)NULL;
			servercomm_msg_memory_send_v1(&msg);

			if(g_bSendBacktrace) {
				void *listBacktraceAddr[BACKTRACE_MAX_SIZE];
				int iBacktraceSize = backtrace(listBacktraceAddr, BACKTRACE_MAX_SIZE);
				backtrace_send(listBacktraceAddr, iBacktraceSize, msg.header.msg_num);
			}
		}
	}

	return p;
}

void* valloc(size_t size)
{
	if(!real_valloc){
		wrapper_init();
	}

	aleakd_data_incr_alloc_number();
	int alloc_num = aleakd_data_get_alloc_number();

	void *p = NULL;
	p = real_valloc(size);

	if(p){
		if(g_bUseServerMessage) {
			struct ServerMsgMemoryV1 msg;
			servercomm_msg_memory_init_v1(&msg);
			msg.header.msg_code = ALeakD_MsgCode_valloc;
			msg.data.alloc_num = (int64_t)alloc_num;
			msg.data.alloc_ptr = (int64_t)p;
			msg.data.alloc_size = (int64_t)size;
			msg.data.free_ptr = (int64_t)NULL;
			servercomm_msg_memory_send_v1(&msg);

			if(g_bSendBacktrace) {
				void *listBacktraceAddr[BACKTRACE_MAX_SIZE];
				int iBacktraceSize = backtrace(listBacktraceAddr, BACKTRACE_MAX_SIZE);
				backtrace_send(listBacktraceAddr, iBacktraceSize, msg.header.msg_num);
			}
		}
	}

	return p;
}

void* pvalloc(size_t size)
{
	if(!real_pvalloc){
		wrapper_init();
	}

	aleakd_data_incr_alloc_number();
	int alloc_num = aleakd_data_get_alloc_number();

	void *p = NULL;
	p = real_pvalloc(size);

	if(p){
		if(g_bUseServerMessage) {
			struct ServerMsgMemoryV1 msg;
			servercomm_msg_memory_init_v1(&msg);
			msg.header.msg_code = ALeakD_MsgCode_pvalloc;
			msg.data.alloc_num = (int64_t)alloc_num;
			msg.data.alloc_ptr = (int64_t)p;
			msg.data.alloc_size = (int64_t)size;
			msg.data.free_ptr = (int64_t)NULL;
			servercomm_msg_memory_send_v1(&msg);

			if(g_bSendBacktrace) {
				void *listBacktraceAddr[BACKTRACE_MAX_SIZE];
				int iBacktraceSize = backtrace(listBacktraceAddr, BACKTRACE_MAX_SIZE);
				backtrace_send(listBacktraceAddr, iBacktraceSize, msg.header.msg_num);
			}
		}
	}

	return p;
}

/*
static void thread_cleanup (void * data)
{
	struct ThreadEntry* pThreadEntry = (struct ThreadEntry* )data;
	fprintf(stderr, "[aleakd] thread %lu (%s): celanup\n",
			pThreadEntry->thread, (pThreadEntry->name ? pThreadEntry->name : ""));

	pthread_key_delete(pThreadEntry->key);
	//ALeakD_MsgCode_print_leaks(idx);

}
*/

int (*real_pthread_create)(pthread_t *, const pthread_attr_t *, void *(*) (void *), void *);
int (*real_pthread_setname_np)(pthread_t thread, const char *name);
//nt (*real_pthread_join)(pthread_t thread, void **retval);
//void (*real_pthread_exit)(void *rval_ptr);
//int (*real_pthread_cancel)(pthread_t thread);

int pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start) (void *), void *arg)
{
	int res;

	if (!real_pthread_create) {
		real_pthread_create = dlsym(RTLD_NEXT, "pthread_create");
	}
	res = real_pthread_create(thread, attr, start, arg);

	if(thread){
		fprintf(stderr, "[aleakd] created thread: %llu\n", (unsigned long long)*thread);

		if(g_bUseServerMessage) {
			struct ServerMsgThreadV1 msg;
			servercomm_msg_thread_init_v1(&msg);
			msg.header.msg_code = ALeakD_MsgCode_pthread_create;
			msg.data.thread_id = (uint64_t)*thread;
			servercomm_msg_thread_send_v1(&msg);
		}
	}

	return res;
}

int pthread_setname_np(pthread_t thread, const char *name)
{
	int res;

	fprintf(stderr, "[aleakd] set thread name: %s\n", name);
	if (!real_pthread_setname_np) {
		real_pthread_setname_np = dlsym(RTLD_NEXT, "pthread_setname_np");
	}
	res = real_pthread_setname_np(thread, name);

	if(thread){
		if(g_bUseServerMessage) {
			struct ServerMsgThreadV1 msg;
			servercomm_msg_thread_init_v1(&msg);
			msg.header.msg_code = ALeakD_MsgCode_pthread_set_name;
			msg.data.thread_id = (uint64_t)thread;
			size_t iMaxLen = strlen(name);
			if(iMaxLen > sizeof (msg.data.thread_name)){
				iMaxLen = sizeof (msg.data.thread_name);
			}
			strncpy(msg.data.thread_name, name, iMaxLen);
			servercomm_msg_thread_send_v1(&msg);
		}
	}

	return res;
}

/*
int pthread_join(pthread_t thread, void **retval)
{
	int res;

	struct ThreadEntryList* pThreadEntryList = aleakd_data_get_thread_list();
	struct ThreadEntry* pThreadEntry;
	int idx = ThreadEntry_getIdx(pThreadEntryList, thread);

	if (!real_pthread_join) {
		real_pthread_join = dlsym(RTLD_NEXT, "pthread_join");
	}
	res =  real_pthread_join(thread, retval);

	if(idx != -1){
		pThreadEntry = aleakd_data_get_thread(idx);
		fprintf(stderr, "[aleakd] thread %lu (%s): join\n",
			pThreadEntry->thread, (pThreadEntry->name ? pThreadEntry->name : ""));

		ALeakD_MsgCode_print_leaks(idx);
	}

	return res;
}

void pthread_exit(void *rval_ptr)
{
	fprintf(stderr, "[aleakd] exit thread\n");
	if (!real_pthread_exit) {
		real_pthread_exit = dlsym(RTLD_NEXT, "pthread_exit");
	}

	real_pthread_exit(rval_ptr);
}

int pthread_cancel(pthread_t thread)
{
	int res;

	fprintf(stderr, "[aleakd] cancel thread\n");
	if (!real_pthread_cancel) {
		real_pthread_cancel = dlsym(RTLD_NEXT, "pthread_cancel");
	}

	return res;
}
*/

int (*real_prctl)(int option, ...);

int prctl(int option, ...)
{
	int res;

	//fprintf(stderr, "[aleakd] prctl option=%d\n", option);
	if (!real_prctl) {
		real_prctl = dlsym(RTLD_NEXT, "prctl");
	}

	unsigned long x[4];
	int i;
	va_list ap;
	va_start(ap, option);
	for (i=0; i<4; i++){
		x[i] = va_arg(ap, unsigned long);
	}
	va_end(ap);

	res = real_prctl(option, x[0], x[1], x[2], x[3]);

	if(res>=0){
		if(g_bUseServerMessage) {
			if(option == PR_SET_NAME) {
				// Send operation message
				struct ServerMsgThreadV1 msg;
				servercomm_msg_thread_init_v1(&msg);
				msg.header.msg_code = ALeakD_MsgCode_pthread_set_name;
				msg.data.thread_id = (uint64_t) msg.header.thread_id;
				const char* szThreadName = (const char*)x[0];
				fprintf(stderr, "[aleakd] prctl set name=%s\n", szThreadName);
				size_t iMaxLen = strlen(szThreadName);
				if(iMaxLen > sizeof (msg.data.thread_name)){
					iMaxLen = sizeof (msg.data.thread_name);
				}
				strncpy(msg.data.thread_name, szThreadName, iMaxLen);
				servercomm_msg_thread_send_v1(&msg);
			}
		}
	}

	return res;
}
