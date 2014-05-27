//an open flow switch which support the delay opreation
//author: Maolin Wang
//email: wangmlkof@sina.com

#include "flow_table.h"
#include "port.h"
#include "cpu.h"
#include "send_ring.h"
#include "thread.h"
#include <stdio.h>
#include <tmc/cpus.h>
#include <unistd.h>

int main(int argc, char * argv[])
{	
	init_cpus();
	//tmc_cpus_set_my_cpu(tmc_cpus_find_nth_cpu(&(cpu.set),cpu_main));
	init_all_ports();
	init_all_rings();
	flow_table_init();
	init_all_threads();
	printf("finish init\n");
	create_tick_thread();
	create_port_thread();

	printf("finish create thread\n");
	//wait all thread
	tmc_spin_barrier_wait(&all_barrier);
	printf("finish waiting\n");
	while(!done)
	{
		if(num_recv()>2950)
			done=1;
	}
	printf("exec done\n");
	//wait for the ptheard
	wait_threads();
	show_all_ports();
	return 0;
}
