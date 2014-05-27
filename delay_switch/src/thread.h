#ifndef THREAD_HEADER
#define THREAD_HEADER
#include "port.h" 
void init_all_threads();
void create_port_thread();
void create_tick_thread();
void wait_threads();
extern pthread_t threads_in[PORT_NUM][WORKER_NUM];
extern pthread_t thread_out[PORT_NUM];
extern pthread_t thread_tick;

extern tmc_spin_barrier_t in_barrier[PORT_NUM][WORKER_NUM];
extern tmc_spin_barrier_t out_barrier[PORT_NUM];
extern tmc_spin_barrier_t tick_barrier;
extern tmc_spin_barrier_t all_barrier;

extern int done;
#endif
