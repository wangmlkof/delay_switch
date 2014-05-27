#include "tick.h"
#include "cpu.h"
#include "thread.h"
#include "send_ring.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <sys/time.h>
#include <tmc/cpus.h>
#include <sys/mman.h>
void init_beat()
{
	struct itimerval value, ovalue; //(1)
	signal(SIGALRM, beat);
	value.it_value.tv_sec = 1;
	value.it_value.tv_usec = 0;
	value.it_interval.tv_sec = 0;
	value.it_interval.tv_usec =TIME_UNIT/1000;
	setitimer(ITIMER_REAL, &value, &ovalue); //(2)
}

void beat()
{
	tmc_spin_rwlock_wrlock(&cur_lock);
	//printf("get write lock\n");
	cur=(cur+1)%RING_LEN;
	tmc_spin_rwlock_wrunlock(&cur_lock);
}

void * start_beat(void *arg)
{
	//set exec cpu
	tmc_cpus_set_my_cpu(tmc_cpus_find_nth_cpu(&(cpu.set),cpu_tick));
	//wait self thread
	tmc_spin_barrier_wait(&(tick_barrier));

	printf("tick cpu: %d\n",cpu_tick);
	init_beat();
	printf("start to beat!\n");
	//wait for signal 
	while(!done)
	{
		mydelay(1000,1);
	}
	return (void*)NULL;
}

void mydelay(double delay,double multiple)
{
	double m,i,j;
	for(m = 0;m < multiple;m++)
	{
		for(i = 0;i < delay;i++)
			for(j = 0;j < delay;j++)
			{
				continue;
			}
	}
}

