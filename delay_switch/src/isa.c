#include "isa.h"
#include "port.h"
#include "send_ring.h"
#include<stdio.h>
void handle(struct ISA instr)
{
	int index;
	switch(instr.func)
	{
		case DROP:
			break;
		case FORWARD:
			tmc_spin_rwlock_rdlock(&cur_lock);
			index=(cur+instr.time/TIME_UNIT)%RING_LEN;
			tmc_spin_mutex_lock(&(ring[instr.port].arr[index].mutex));
			//printf("index:%d\n",index);
			queue_insert(&(ring[instr.port].arr[index]),instr.packet);
			tmc_spin_mutex_unlock(&(ring[instr.port].arr[index].mutex));
			tmc_spin_rwlock_rdunlock(&cur_lock);

			break;
		case STORE:
			break;
		case REPORT:
			break;
	}
}
