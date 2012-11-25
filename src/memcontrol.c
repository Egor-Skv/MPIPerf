/*
 * memcontrol.c
 *
 *  Created on: 05.11.2012
 *      Author: egor
 */

#include <malloc.h>
#include "mpiperf.h"
#include <pthread.h>
#include "hashtab.h"
#include "bench_coll.h"

//malloc_hook isn't thread safe :(
pthread_mutex_t mpi_lock = PTHREAD_MUTEX_INITIALIZER;

/* Prototypes for my hooks.  */
static void *my_malloc_hook(size_t, const void *);
static void *my_realloc_hook(void *ptr, size_t size, const void *caller);
static void my_free_hook(void*, const void *);

static void (*old_free_hook) __MALLOC_PMT ((void *__ptr, __const __malloc_ptr_t));
static void *(*old_malloc_hook) __MALLOC_PMT ((size_t __size, __const __malloc_ptr_t));
static void *(*old_realloc_hook) __MALLOC_PMT ((void *__ptr, size_t __size, __const __malloc_ptr_t));

/* Override initializing hook from the C library. */
// i.e. set hook for whole program
//void (*__malloc_initialize_hook) (void) = init_memory_hook;


// Call counters
static int malloc_call_cnt = 0;
static int realloc_call_cnt = 0;
static int free_call_cnt = 0;
static int urealloc_call_cnt = 0;
static int ufree_call_cnt = 0;

// Mem counters
static int bytes_allocated = 0;
static int bytes_freed = 0;

//hash table
hashtab_t *test_ht = NULL;
//~

/* Hook operation functions */
static void restore_hooks(void)
{
    __malloc_hook = old_malloc_hook;
    __realloc_hook = old_realloc_hook;
    __free_hook = old_free_hook;
}

static void save_underlying_hooks(void)
{
    old_malloc_hook = __malloc_hook;
    old_realloc_hook = __realloc_hook;
    old_free_hook = __free_hook;
}

static void set_my_hooks(void)
{
	pthread_mutex_lock(&mpi_lock);

    __malloc_hook = my_malloc_hook;
    __realloc_hook = my_realloc_hook;
    __free_hook = my_free_hook;

    pthread_mutex_unlock(&mpi_lock);
}
/* END: hook operation functions */


void init_memory_hook(void)
{
	test_ht = ht_init(1024, NULL);

	save_underlying_hooks();
	set_my_hooks();
}

void deinit_memory_hook(void)
{
	restore_hooks();

	ht_destroy(test_ht);
}

void free_all_counters(void)
{
	malloc_call_cnt = 0;
	realloc_call_cnt = 0;
	free_call_cnt = 0;

	urealloc_call_cnt=0;
	ufree_call_cnt=0;

	bytes_allocated = 0;
	bytes_freed = 0;
}

void print_memtest_header(char *benchname) {
	printf("# Characteristics of measurements:\n");
	printf("# Procs - total number of processes\n");
	printf("# Rank - Rank (number) of this processes\n");
	printf("# Malloc called - malloc called\n");
	printf("# Realloc called - realloc called\n");
	printf("# Free called - free called\n");
	printf("# Unknown free - free called to unknown pointer\n");
	printf("# Unknown realloc - realloc called to unknown pointer\n");
	printf("# Free called - free called\n");
	printf("# Allocated - Total bytes allocated\n");
	printf("# Freed  - Total bytes freed\n");
	printf("# Benchmark: %s\n", benchname);

	printf("# [Procs]  [Rank]    [Malloc called]    [Realloc called]    [Free called]    [Unknown free]    [Unknown realloc]    [Allocated]    [Freed]\n");
}

void report_mem_usage_results(int nprocs)
{

	if (!mpiperf_logmaster_only || IS_MASTER_RANK) {

		printf("   %-7d  %-7d   %-7d            %-7d             %-7d          %-7d           %-7d              %-7d        %-7d\n",
				nprocs, mpiperf_rank, malloc_call_cnt, realloc_call_cnt, free_call_cnt,
				ufree_call_cnt,	urealloc_call_cnt, bytes_allocated, bytes_freed);
	}

	free_all_counters();

}


/* My memory functions hooks */
static void *my_malloc_hook(size_t size, const void *caller)
{
    void *result;
    /* Restore all old hooks */
    restore_hooks();

    /* Call recursively */
    result = malloc (size);

    /* Save underlying hooks */
    save_underlying_hooks();

    /*Save data to the hash table*/
    memht_insert(test_ht, &result, &size);

    malloc_call_cnt++;
    bytes_allocated += size;

    /* Restore our own hooks */
    set_my_hooks();
    return result;
}

static void *my_realloc_hook(void *ptr, size_t size, const void *caller)
{
    void *result;
    void *hash_data = NULL;
    int freed, alloc = size;

    /* Restore all old hooks */
    restore_hooks();

    /* Call recursively */
    result = realloc (ptr, size);

    /* Save underlying hooks */
    save_underlying_hooks();

    hash_data = memht_search(test_ht, &ptr);

    if(hash_data) {
    	freed = *((int*)hash_data);
    	memht_remove(test_ht, &ptr);
        memht_insert(test_ht, &result, &size);

        realloc_call_cnt++;
        if((alloc-freed) > 0){
        	bytes_allocated += alloc - freed;
        }
        else
        	bytes_freed += freed - alloc;
    }
    else
    {
    	//Realloc called on unknown pointer
    	urealloc_call_cnt++;
    }

    /* Restore our own hooks */
    set_my_hooks();
    return result;
}

static void my_free_hook(void *ptr, const void *caller)
{
	void *hash_data = NULL;

    /* Restore all old hooks */
	restore_hooks();

    /* Call recursively */
    free (ptr);

    /* Save underlying hooks */
    save_underlying_hooks();

    hash_data = memht_search(test_ht, &ptr);

    if(hash_data) {
    	bytes_freed += *((int*)hash_data);
    	memht_remove(test_ht, &ptr);
    	free_call_cnt++;
    }
    else {
    	//Free called on unknown pointer
    	ufree_call_cnt++;
    }


    /* Restore our own hooks */
    set_my_hooks();
}
/* END: My memory functions hooks */

