#include "cpu.h"
#include<tmc/cpus.h>
#include <stdio.h>
void *controller(void *arg)
{
	tmc_cpus_set_my_cpu(tmc_cpus_find_nth_cpu(&(cpu.set),0));
	printf("contorller cpu: %d\n",1);
	return (void*)NULL;
}
