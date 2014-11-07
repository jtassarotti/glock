#include<stdio.h>
#include<stdlib.h>
#include<assert.h>
#include<pthread.h>
#include<stdint.h>
#include<errno.h>

#include "util.h"
#include "params.h"

#define BATCH_SIZE 5

typedef struct {
	long long a;
	long long b;
} node_t;

typedef struct {
	pthread_spinlock_t lock;
	int index;
	long long pad[7];
} lock_t;

void *reader(void *ptr);
void *writer(void *ptr);

/** < Only shared variables here */
node_t *nodes;
lock_t *locks;

int main()
{
	int i;
	int tid[NUM_THREADS];
	pthread_t thread[NUM_THREADS];

	/** < Ensure that locks are cacheline aligned */
	assert(sizeof(lock_t) == 64);

	/** < Allocate the shared nodes */
	red_printf("Allocting %d nodes\n", NUM_NODES);
	nodes = (node_t *) malloc(NUM_NODES * sizeof(node_t));
	assert(nodes != NULL);
	
	for(i = 0; i < NUM_NODES; i ++) {
		nodes[i].a = rand();
		nodes[i].b = nodes[i].a + 1;
	}

	/** < Allocate the striped spinlocks */
	red_printf("Allocting %d locks\n", NUM_LOCKS);
	locks = (lock_t *) malloc(NUM_LOCKS * sizeof(lock_t));
	assert(locks != NULL);
	
	for(i = 0; i < NUM_LOCKS; i++) {
		pthread_spin_init(&locks[i].lock, 0);
	}
	
	/** < Launch several reader threads and a writer thread */
	for(i = 0; i < NUM_THREADS; i++) {
		tid[i] = i;
		red_printf("Launching reader thread with tid = %d\n", tid[i]);
		pthread_create(&thread[i], NULL, reader, &tid[i]);
	}

	for(i = 0; i < NUM_THREADS; i++) {
		pthread_join(thread[i], NULL);
	}

	exit(0);
}

void *reader( void *ptr)
{
	struct timespec start, end;
	int tid = *((int *) ptr);
	uint64_t seed = 0xdeadbeef + tid;
	int sum = 0, i;

	/** < The node and lock to use in an iteration */
	int node_id[BATCH_SIZE], lock_id[BATCH_SIZE], I;

	/** < Total number of iterations (for measurement) */
	int num_iters = 0;
	srand(tid);

	clock_gettime(CLOCK_REALTIME, &start);

	while(1) {
		if(num_iters >= ITERS_PER_MEASUREMENT) {
			clock_gettime(CLOCK_REALTIME, &end);
			double seconds = (end.tv_sec - start.tv_sec) + 
				(double) (end.tv_nsec - start.tv_nsec) / 1000000000;
		
			printf("Reader thread %d: rate = %.2f M/s. Sum = %d\n", tid, 
				num_iters / (1000000 * seconds), sum);
				
			num_iters = 0;
			clock_gettime(CLOCK_REALTIME, &start);
		}

		for(I = 0; I < BATCH_SIZE; I ++) {
			node_id[I] = rand() & NUM_NODES_;
			lock_id[I] = node_id[I] & NUM_LOCKS_;
			__builtin_prefetch(&nodes[node_id[I]], 0, 0);
			__builtin_prefetch(&locks[lock_id[I]], 0, 0);
		}

		for(I = 0; I < BATCH_SIZE; I ++) {
			pthread_spin_lock(&locks[lock_id[I]].lock);
			
			/** < Critical section begin */
			nodes[node_id[I]].a ++;
			nodes[node_id[I]].b ++;
			/** < Critical section end */

			pthread_spin_unlock(&locks[lock_id[I]].lock);
	
			num_iters ++;
		}
	}
}

