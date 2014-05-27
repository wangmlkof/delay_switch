#include "queue.h"
#include "send_out.h"
#include <tmc/spin.h>
#include <stdlib.h>

void queue_init(struct queue * q)
{
	q->head=NULL;
	q->tail=NULL;
	q->len=0;
	tmc_spin_mutex_init(&(q->mutex));
}
void queue_insert(struct queue *q, gxio_mpipe_idesc_t * pac)
{
	struct itemq* ins=malloc(sizeof(struct itemq));
	ins->pac=pac;
	ins->next=NULL;
	q->len++;
	if(q->head==NULL)
	{
		q->head=ins;
		q->tail=ins;
	}
	else
	{
		q->tail->next=ins;
		q->tail=ins;
	}
}
int queue_try_peek(struct queue * q,gxio_mpipe_idesc_t ** packets)
{
	if(q->len==0)
	{
		return 0;
	}
	else
	{
		int i;
		struct itemq * tmp;
		if(q->len<BATCH)
		{
			for(i=0;i<q->len;i++)
			{
				tmp=q->head;
				q->head=q->head->next;
				packets[i]=tmp->pac;
				free(tmp);
			}
			q->len=0;
			q->head=NULL;
			q->tail=NULL;
			return i;
		}
		else
		{
			for(i=0;i<BATCH;i++)
			{
				tmp=q->head;
				q->head=q->head->next;
				packets[i]=tmp->pac;
				free(tmp);
			}
			q->len=q->len-BATCH;
			if(q->len==0)
			{
				q->tail=q->head=NULL;
			}
			return BATCH;
		}
	}
}
void queue_append(struct queue * qh,struct queue * qt)
{
	qh->len=qh->len+qt->len;
	if(qh->head==NULL)
	{
		qh->head=qt->head;
		qh->tail=qt->tail;
	}
	else
	{
		qh->tail->next=qt->head;
		qh->tail=qt->tail;
	}
}
