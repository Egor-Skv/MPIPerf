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

	bytes_allocated = 0;
	bytes_freed = 0;
}

void report_mem_usage_results(collbench_t *bench, colltest_params_t *params)
{
	if (IS_MASTER_RANK) { //Maybe we need to print this data for each process??

		printf("Memory usage results (the same for each process)\n");
		printf("function:\t\t %s\ncomm size\t\t %d\n",bench->name, params->nprocs);
		printf("Malloc called:\t\t %d times\n", malloc_call_cnt);
		printf("Realloc called:\t\t %d times\n", realloc_call_cnt);
		printf("Free called:\t\t %d times\n", free_call_cnt);

		printf("Bytes allocated:\t %d\n", bytes_allocated);
		printf("Bytes freed:\t\t %d\n\n", bytes_freed);
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


    /* printf might call malloc, so protect it too. */
    //if (IS_MASTER_RANK)
    //printf ("malloc (%u) returns %p caller %p\n", (unsigned int) size, result, caller);

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
    /* Restore all old hooks */
    restore_hooks();

    /* Call recursively */
    result = realloc (ptr, size);

    /* Save underlying hooks */
    save_underlying_hooks();

    /* printf might call malloc, so protect it too. */
    printf ("realloc  pointer %p (%u) returns %p\n", ptr, (unsigned int) size, result);

    //TODO: store realloc in hash and recount allocated bytes

    /* Restore our own hooks */
    set_my_hooks();
    return result;
}

static void my_free_hook(void *ptr, const void *caller)
{
    /* Restore all old hooks */
	restore_hooks();

    /* Call recursively */
    free (ptr);

    /* Save underlying hooks */
    save_underlying_hooks();

    void *hash_data = NULL;

    hash_data = memht_search(test_ht, &ptr);

    if(hash_data) {
    	bytes_freed += *((int*)hash_data);
    }
    else
    	printf("Can't find data for key %d! \n", (int)ptr);

    free_call_cnt++;

    /* Restore our own hooks */
    set_my_hooks();
}
/* END: My memory functions hooks */

