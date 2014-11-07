#include<stdio.h>
#include<stdlib.h>
#include<pthread.h>
#include<time.h>
#include<unistd.h>

#include "cuckoo.h"

#define BATCH_SIZE 8

typedef struct {
	pthread_spinlock_t lock;
	int index;
	long long pad[7];
} lock_t;

int *keys;						/**< Read-only */
struct cuckoo_bkt *ht_index;	/**< Read-only */
lock_t *locks;

void *cuckoo_thread(void *ptr);

int main(int argc, char **argv)
{
	int i;
	int tid[NUM_THREADS];
	pthread_t thread[NUM_THREADS];

	/**< Ensure that locks are cacheline aligned */
	assert(sizeof(lock_t) == 64);
	
	red_printf("main: Initializing cuckoo hash table\n");
	cuckoo_init(&keys, &ht_index);

	/**< Allocate the striped spinlocks */
	red_printf("Allocting %d locks\n", NUM_LOCKS);
	locks = (lock_t *) malloc(NUM_LOCKS * sizeof(lock_t));
	assert(locks != NULL);
	
	for(i = 0; i < NUM_LOCKS; i++) {
		pthread_spin_init(&locks[i].lock, 0);
	}

	/**< Launch several threads */
	for(i = 0; i < NUM_THREADS; i++) {
		tid[i] = i;
		red_printf("Launching reader thread with tid = %d\n", tid[i]);
		pthread_create(&thread[i], NULL, cuckoo_thread, &tid[i]);
	}

	for(i = 0; i < NUM_THREADS; i++) {
		pthread_join(thread[i], NULL);
	}

	return 0;
}


void *cuckoo_thread(void *ptr)
{
	struct timespec start, end;
	int tid = *((int *) ptr);
	//uint64_t seed = 0xdeadbeef + tid;
	int sum = 0;

	int I;	/**< Batch iterator */
	int j;	/**< Slot iterator */

	int bkt_1[BATCH_SIZE], bkt_2[BATCH_SIZE], key[BATCH_SIZE];
	int success[BATCH_SIZE] = {0};

	/**< The node and lock to use in an iteration */
	int lock_id[BATCH_SIZE];

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

		/**< Issue prefetch for the 1st bucket*/
		for(I = 0; I < BATCH_SIZE; I ++) {
			key[I] = keys[(num_iters + I) & NUM_KEYS_];

			bkt_1[I] = __hash(key[I]) & NUM_BKT_;
			lock_id[I] = bkt_1[I] & NUM_LOCKS_;
			__builtin_prefetch(&ht_index[bkt_1[I]], 0, 0);
			__builtin_prefetch(&locks[lock_id[I]], 0, 0);
		}
			
		/**< Try the 1st bucket. If it fails, issue prefetch for bkt #2 */
		for(I = 0; I < BATCH_SIZE; I ++) {

			pthread_spin_lock(&locks[lock_id[I]].lock);

			for(j = 0; j < 8; j ++) {
				if(ht_index[bkt_1[I]].slot[j].key == key[I]) {
					sum += ht_index[bkt_1[I]].slot[j].value;
					ht_index[bkt_1[I]].slot[j].value ++;

					success[I] = 1;
					break;
				}
			}

			pthread_spin_unlock(&locks[lock_id[I]].lock);
		
			if(success[I] == 0) {
				bkt_2[I] = __hash(bkt_1[I]) & NUM_BKT_;
				lock_id[I] = bkt_2[I] & NUM_LOCKS_;
				__builtin_prefetch(&ht_index[bkt_2[I]], 0, 0);
				__builtin_prefetch(&locks[lock_id[I]], 0, 0);
			}
		}

		/**< For failed batch elements, try the 2nd bucket */
		for(I = 0; I < BATCH_SIZE; I ++) {

			if(success[I] == 0) {

				pthread_spin_lock(&locks[lock_id[I]].lock);

				for(j = 0; j < 8; j ++) {
					if(ht_index[bkt_2[I]].slot[j].key == key[I]) {
						sum += ht_index[bkt_2[I]].slot[j].value;
						break;
					}
				}

				pthread_spin_unlock(&locks[lock_id[I]].lock);
			}
		}

		num_iters += BATCH_SIZE;
	}
}
