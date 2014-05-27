#include "thread.h"
#include "match_in.h"
#include "send_out.h"
#include "tick.h"
#include <stdio.h>
#include <pthread.h>
#include <tmc/task.h>

pthread_t threads_in[PORT_NUM][WORKER_NUM];
pthread_t thread_out[PORT_NUM];
pthread_t thread_tick;

tmc_spin_barrier_t in_barrier[PORT_NUM][WORKER_NUM];
tmc_spin_barrier_t out_barrier[PORT_NUM];
tmc_spin_barrier_t all_barrier;
tmc_spin_barrier_t tick_barrier;

int done;

void init_all_threads()
{
	int i,j;
	for(i=0;i<PORT_NUM;i++)
	{
		for(j=0;j<WORKER_NUM;j++)
		{
			tmc_spin_barrier_init(&(in_barrier[i][j]),2);
		}
		tmc_spin_barrier_init(&(out_barrier[i]),2);
	}
	tmc_spin_barrier_init(&tick_barrier,2);
	tmc_spin_barrier_init(&all_barrier,PORT_NUM*(WORKER_NUM+1)+1);
	done=0;
}
void create_port_thread()
{
	int i,j;
	int rank[2];
	for(i=0;i<PORT_NUM;i++)
	{
		rank[0]=i;
		//init ingress ptheard 
		for(j=0;j<WORKER_NUM;j++)
		{
			rank[1]=j;
			if (pthread_create(&(threads_in[i][j]), NULL, match_in, (void*)rank) != 0)
				tmc_task_die("Failure in 'pthread_create()'.");
			//wait for the thread to finish creating
			tmc_spin_barrier_wait(&(in_barrier[i][j]));

		}
		//init egress ptheard 
		if (pthread_create(&(thread_out[i]), NULL, send_out, (void*)rank) != 0)
			tmc_task_die("Failure in 'pthread_create()'.");
		tmc_spin_barrier_wait(&(out_barrier[i]));
	}
}
void create_tick_thread()
{
	if (pthread_create(&thread_tick,NULL, start_beat, NULL) != 0)
		tmc_task_die("Failure in 'pthread_create()'.");
	tmc_spin_barrier_wait(&(tick_barrier));
}

void wait_threads()
{
	int i,j;
	for (i = 0; i < PORT_NUM; i++)
	{
		for(j=0;j<WORKER_NUM;j++)
		{
			if (pthread_join(threads_in[i][j], NULL) != 0)
				tmc_task_die("Failure in 'pthread_join()'.");
		}
		if (pthread_join(thread_out[i], NULL) != 0)
			tmc_task_die("Failure in 'pthread_join()'.");
	}
	printf("port thread done!\n");
	pthread_join(thread_tick,NULL);
	printf("tick done!\n");
}
