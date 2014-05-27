#ifndef CPU_HEADER
#define CPU_HEADER

#include <tmc/cpus.h>
struct CPU
{
	cpu_set_t set;
	int cur;
};
void init_cpus();
extern struct CPU cpu;
#endif
