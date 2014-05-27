#include "match_in.h"
#include "thread.h"
#include "flow_table.h"
#include "port.h"
#include "cpu.h"
#include<tmc/cpus.h>
#include<tmc/mem.h>
#include <stdio.h>
#include <stdlib.h>
#define MATCH_MAX 16
void * match_in(void *arg)
{
	int port_num=((int *)arg)[0];
	int work_num=((int *)arg)[1];
	//set exec cpu
	tmc_cpus_set_my_cpu(tmc_cpus_find_nth_cpu(&(cpu.set),ports[port_num].cpu_in[work_num]));
	//wait self thread
	tmc_spin_barrier_wait(&(in_barrier[port_num][work_num]));
	printf("port:%d  worker:%d cpu: %d\n",port_num,work_num,ports[port_num].cpu_in[work_num]);
	//printf("port:%d  worker:%d wait done\n",port_num,work_num);
	//wait other thread
	tmc_spin_barrier_wait(&all_barrier);
	gxio_mpipe_iqueue_t* iqueue = ports[port_num].iqueues[work_num];

	while(!done)
	{
		gxio_mpipe_idesc_t* idescs;
		int n = gxio_mpipe_iqueue_try_peek(iqueue, &idescs);
		if(n>MATCH_MAX)
		{
			n=MATCH_MAX;
		}
		else if(n<=0)
		{
			continue;
		}
		// Prefetch the actual idescs.
		tmc_mem_prefetch(idescs, n * sizeof(*idescs));

		for (int i = 0; i < n; i++)
		{
			struct ISA instr=flow_table_match(&idescs[i]);
			instr.packet=malloc(sizeof(gxio_mpipe_idesc_t));
			memcpy(instr.packet,&idescs[i],sizeof(gxio_mpipe_idesc_t));
			//show_instr(&instr);
			handle(instr);
			gxio_mpipe_iqueue_consume(iqueue, &idescs[i]);
			ports[port_num].in_num[work_num]++;
		}
	}
	__insn_mf();
	return (void*)NULL;
}
