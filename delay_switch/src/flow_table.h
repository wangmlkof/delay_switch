#ifndef FLOW_TABLE_HEADER
#define FLOW_TABLE_HEADER
#include "isa.h"
#define FLOW_TABLE_MAXSIZE 1024
#define FLOW_ENTRY_SIZE 12

#define flow_entry_t unsigned char

struct flow_table_entry
{
	flow_entry_t  entry[FLOW_ENTRY_SIZE];
	flow_entry_t  mask[FLOW_ENTRY_SIZE];
	struct ISA instr;
};
struct FLOW_TABLE
{		
	struct	flow_table_entry * tbp;
	int size;
};
void flow_table_init();
struct ISA flow_table_match(gxio_mpipe_idesc_t * idesc);
void show_instr(struct ISA * ins);
extern struct FLOW_TABLE flow_table;
extern struct flow_table_entry entry_list[FLOW_TABLE_MAXSIZE];

#endif
