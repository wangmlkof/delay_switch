#include "send_ring.h"
#include <stdlib.h>
int cur;
struct send_ring ring[PORT_NUM];
tmc_spin_rwlock_t  cur_lock;
void init_ring(struct send_ring * ring)
{
	int i;
	for(i=0;i<RING_LEN;i++)
	{
		queue_init(&(ring->arr[i]));
	}
}
void init_all_rings()
{
	int i;
	tmc_spin_rwlock_init(&cur_lock);
	cur=0;
	for(i=0;i<PORT_NUM;i++)
	{
		init_ring(&(ring[i]));
	}
}
