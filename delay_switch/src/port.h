#ifndef PORT_HEADER
#define PORT_HEADER
#include <gxio/mpipe.h>
#include <tmc/cpus.h>
#include <tmc/spin.h>
#define PORT_NUM 2
#define WORKER_NUM 4
struct PORT
{
	char * name;//the port name: xgbe*
	gxio_mpipe_context_t context;
	gxio_mpipe_iqueue_t** iqueues;
	gxio_mpipe_equeue_t *equeue;
	gxio_mpipe_equeue_t  equeue_body;
	int cpu_in[WORKER_NUM];
	int cpu_out;
	int in_num[WORKER_NUM];
	int out_num;
	int num_workers;
	unsigned char mac[6];
};
void init_port(struct PORT * port,char * name,unsigned char * mac,int num_workers);
void init_all_ports();
void show_all_ports();
int num_recv();
void create_stack(gxio_mpipe_context_t* context, int stack_idx,gxio_mpipe_buffer_size_enum_t buf_size, int num_buffers);
extern struct PORT ports[PORT_NUM];
extern int cpu_tick;
extern int cpu_main;
extern unsigned char mac1[6];
extern unsigned char mac2[6];
extern unsigned char mac3[6];
extern unsigned char mac4[6];
extern unsigned char mac5[6];
extern unsigned char mac6[6];
extern unsigned char mac7[6];
extern unsigned char mac8[6];
extern unsigned char macb[6];


#endif
