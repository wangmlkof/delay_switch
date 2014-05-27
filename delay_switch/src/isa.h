#ifndef ISA_HEADER
#define ISA_HEADER

#include<gxio/mpipe.h>

#define op_time_t unsigned int
#define op_port_t unsigned int
enum func_t{DROP,FORWARD,STORE,REPORT};
struct ISA 
{
	enum func_t func;
	gxio_mpipe_idesc_t  * packet;	
	op_time_t time;
	op_port_t port; 	
};
void handle(struct ISA instr);
#endif
