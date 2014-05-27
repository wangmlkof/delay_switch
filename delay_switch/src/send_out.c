#include "port.h"
#include "cpu.h"
#include "send_out.h"
#include "send_ring.h"
#include "thread.h"
#include <stdlib.h>
#include <stdio.h>
#include <tmc/mem.h>
void * send_out(void * arg)
{
	int port_num=((int *)arg)[0];
	//set exec cpu
	tmc_cpus_set_my_cpu(tmc_cpus_find_nth_cpu(&(cpu.set),ports[port_num].cpu_out));

	
	//wait self thread
	tmc_spin_barrier_wait(&(out_barrier[port_num]));
	printf("port:%d out cpu: %d\n",port_num,ports[port_num].cpu_out);
	//wait other thread
	tmc_spin_barrier_wait(&all_barrier);

	gxio_mpipe_idesc_t * packets[BATCH];
	while(!done)
	{	
		int result=-1;
		int num=0;

		result=tmc_spin_rwlock_tryrdlock(&cur_lock);
		if(result==0)//success lock on read cur
		{
			result=tmc_spin_mutex_trylock(&(ring[port_num].arr[cur].mutex));
			if(result==0)
			{
				num=queue_try_peek(&(ring[port_num].arr[cur]),packets);
				tmc_spin_mutex_unlock(&(ring[port_num].arr[cur].mutex));
			}			
			tmc_spin_rwlock_rdunlock(&cur_lock);
		}
		if(num!=0)
		{

			//tmc_mem_prefetch(packets,num*sizeof(*packets));
			gxio_mpipe_edesc_t edescs[BATCH];
			for(int i=0;i<num;i++)
			{
				gxio_mpipe_idesc_t* idesc = packets[i];
				gxio_mpipe_edesc_t* edesc = &edescs[i];
				// Prepare to forward (or drop).
				gxio_mpipe_edesc_copy_idesc(edesc, idesc);
				void* start = gxio_mpipe_idesc_get_l2_start(idesc);
				// Flip source/dest mac addresses.
				void* dmac = start + 0;
				void* smac = start + 6;
				memcpy(dmac, macb, 6);
				//memcpy(smac, mac5, 6);
				memcpy(smac,ports[port_num].mac, 6);

			}

			__insn_mf();
			// Reserve slots.  NOTE: This might spin.
			long slot = gxio_mpipe_equeue_reserve_fast(ports[port_num].equeue, num);
			// Egress the packets.
			for (int i = 0; i < num; i++)
			{
				gxio_mpipe_equeue_put_at(ports[port_num].equeue, edescs[i], slot+i);
			}
			for (int i = 0; i < num; i++)
			{
				free(packets[i]);
			}
			//printf("forward  packet of size %d.\n",n);
			ports[port_num].out_num+=num;
		}
	}
	__insn_mf();
	return (void*)NULL;
}

