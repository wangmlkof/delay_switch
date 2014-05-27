#include "flow_table.h"
#include "send_ring.h"
#include<stdio.h>
struct FLOW_TABLE flow_table;
struct flow_table_entry entry_list[FLOW_TABLE_MAXSIZE];

void flow_table_init()
{
	flow_table.tbp=entry_list;
	int i;
	for(i=0;i<FLOW_ENTRY_SIZE;i++)
	{
		flow_table.tbp[0].entry[i]=0;
		flow_table.tbp[0].mask[i]=0;
		flow_table.tbp[0].instr.func=FORWARD;
		flow_table.tbp[0].instr.packet=NULL;
		flow_table.tbp[0].instr.time=4000*TIME_UNIT;
		flow_table.tbp[0].instr.port=1;
	}
	flow_table.size=1;
}
void show_instr(struct ISA * ins)
{
	printf("forward port:%d\n",ins->port);
	printf("forward time:%d\n",ins->time);
	printf("forward length:%d\n",ins->packet->l2_size);
}
struct ISA flow_table_match(gxio_mpipe_idesc_t * idesc)
{
	struct ISA ins;
	//the default operation of unmatched packet
	ins.func=DROP;
	ins.packet=NULL;
	unsigned char * start=(unsigned char *) gxio_mpipe_idesc_get_l2_start(idesc);
	int i,j;
	for(i=0;i<flow_table.size;i++)
	{	int match=1;	
		for(j=0;j<FLOW_ENTRY_SIZE;j++)
		{
			if((start[j]&(flow_table.tbp[i].mask[j]))!=flow_table.tbp[i].entry[j])
				{
					match=0;
					break;
				}
		}
		if(match==1)
		{
			ins=flow_table.tbp[i].instr;	
			break;
		}
	}
	return ins;
}

