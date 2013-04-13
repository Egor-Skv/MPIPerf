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
#include "memcontrol.h"

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


struct memstat_st {

	// Call counters
	int malloc_call_cnt;
	int realloc_call_cnt;
	int free_call_cnt;
	int urealloc_call_cnt;
	int ufree_call_cnt;

	// Mem counters
	int bytes_allocated;
	int bytes_freed;
};


static memstat_t memstat;

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

void free_all_counters(memstat_t *mstat)
{
	mstat->malloc_call_cnt = 0;
	mstat->realloc_call_cnt = 0;
	mstat->free_call_cnt = 0;

	mstat->urealloc_call_cnt=0;
	mstat->ufree_call_cnt=0;

	mstat->bytes_allocated = 0;
	mstat->bytes_freed = 0;
}

void init_memtest_stat(void)
{
	free_all_counters(&memstat);
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

	printf("# [Procs]    [Malloc called]    [Realloc called]    [Free called]    [Unknown free]    [Unknown realloc]    [Allocated]    [Freed]\n");
}

void report_mem_usage_results(int nprocs, MPI_Comm comm)
{
	int i, size = 0;
	int line_len = 0;
	memstat_t memstat_total;

	free_all_counters(&memstat_total);
    MPI_Reduce(&memstat.bytes_allocated, &memstat_total.bytes_allocated, 1, MPI_INT, MPI_SUM, 0,  comm);
    MPI_Reduce(&memstat.bytes_freed, &memstat_total.bytes_freed, 1, MPI_INT, MPI_SUM, 0,  comm);
    MPI_Reduce(&memstat.free_call_cnt, &memstat_total.free_call_cnt, 1, MPI_INT, MPI_SUM, 0,  comm);
    MPI_Reduce(&memstat.malloc_call_cnt, &memstat_total.malloc_call_cnt, 1, MPI_INT, MPI_SUM, 0,  comm);
    MPI_Reduce(&memstat.realloc_call_cnt, &memstat_total.realloc_call_cnt, 1, MPI_INT, MPI_SUM, 0,  comm);
    MPI_Reduce(&memstat.ufree_call_cnt, &memstat_total.ufree_call_cnt, 1, MPI_INT, MPI_SUM, 0,  comm);
    MPI_Reduce(&memstat.urealloc_call_cnt, &memstat_total.urealloc_call_cnt, 1, MPI_INT, MPI_SUM, 0,  comm);

	MPI_Comm_size(comm, &size);

	if (IS_MASTER_RANK) {
		printf("   %-7d    %-7d            %-7d             %-7d          %-7d           %-7d              %-7d        %-7d\n",
				nprocs, memstat_total.malloc_call_cnt / nprocs, memstat_total.realloc_call_cnt / nprocs,
				memstat_total.free_call_cnt / nprocs, memstat_total.ufree_call_cnt / nprocs,
				memstat_total.urealloc_call_cnt / nprocs, memstat_total.bytes_allocated / nprocs,
				memstat_total.bytes_freed / nprocs);
	}

	if (!mpiperf_logmaster_only) {
		if (IS_MASTER_RANK)
			printf(" Detailed:\n");

		for(i = 0; i < size; i++) {
			MPI_Barrier(comm);
			if(i == mpiperf_rank) {
				line_len = printf("   %-7d    %-7d            %-7d             %-7d          %-7d           %-7d              %-7d        %-7d\n",
						mpiperf_rank, memstat.malloc_call_cnt, memstat.realloc_call_cnt, memstat.free_call_cnt,
						memstat.ufree_call_cnt,	memstat.urealloc_call_cnt, memstat.bytes_allocated, memstat.bytes_freed);
				if( i == (size - 1)) {
					while(2 + line_len--)
						printf("-");
					printf("\n");
				}
			}
		}
	}

	free_all_counters(&memstat);
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

    memstat.malloc_call_cnt++;
    memstat.bytes_allocated += size;

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

        memstat.realloc_call_cnt++;
        if((alloc-freed) > 0){
        	memstat.bytes_allocated += alloc - freed;
        }
        else
        	memstat.bytes_freed += freed - alloc;
    }
    else
    {
    	//Realloc called on unknown pointer
    	memstat.urealloc_call_cnt++;
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
    	memstat.bytes_freed += *((int*)hash_data);
    	memht_remove(test_ht, &ptr);
    	memstat.free_call_cnt++;
    }
    else {
    	//Free called on unknown pointer
    	memstat.ufree_call_cnt++;
    }


    /* Restore our own hooks */
    set_my_hooks();
}
/* END: My memory functions hooks */

