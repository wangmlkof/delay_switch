#ifndef SEND_RING_HEADER
#define SEND_RING_HEADER
#include "queue.h"
#include "port.h"
#include<tmc/spin.h>
#define TIME_UNIT 100000
#define RING_LEN 10000
struct send_ring
{
	struct queue arr[RING_LEN];
};
void init_ring(struct send_ring * ring);
void init_all_rings();
extern struct send_ring ring[PORT_NUM];
extern tmc_spin_rwlock_t  cur_lock;
extern int cur;
#endif
