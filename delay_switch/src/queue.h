#ifndef QUEUE_HEADER 
#define QUEUE_HEADER 

#include <gxio/mpipe.h>
#include <tmc/spin.h>
struct itemq
{
	gxio_mpipe_idesc_t * pac;
	struct itemq * next;	
};
struct queue 
{
	struct itemq * head;
	struct itemq * tail;
	tmc_spin_mutex_t mutex;
	int len;
};
void queue_init(struct queue * q);
void queue_insert(struct queue * q,gxio_mpipe_idesc_t * pac);
int queue_try_peek(struct queue * q,gxio_mpipe_idesc_t ** packets);
void queue_append(struct queue * qh,struct queue * qt);
#endif
