#include "cpu.h"
struct CPU cpu;
void init_cpus()
{
	tmc_cpus_get_my_affinity(&(cpu.set));
	cpu.cur=1;
}
