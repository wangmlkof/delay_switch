#include"port.h"
#include"cpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#include <signal.h>
#include <time.h>
#include <sys/time.h>
#include <sys/mman.h>

#include <tmc/alloc.h>

#include <arch/atomic.h>
#include <arch/sim.h>

#include <tmc/mem.h>
#include <tmc/spin.h>
#include <tmc/sync.h>
#include <tmc/task.h>
// Help check for errors.
#define VERIFY(VAL, WHAT)                                       \
  do {                                                          \
    long long __val = (VAL);                                    \
    if (__val < 0)                                              \
      tmc_task_die("Failure in '%s': %lld: %s.",                \
                   (WHAT), __val, gxio_strerror(__val));        \
  } while (0)


unsigned int equeue_entries = GXIO_MPIPE_EQUEUE_ENTRY_2K;

unsigned char mac1[6]={0x00,0x1a,0xca,0x00,0xd2,0x9e};
unsigned char mac2[6]={0x00,0x1a,0xca,0x00,0xd2,0x9f};
unsigned char mac3[6]={0x00,0x1a,0xca,0x00,0xd2,0xa0};
unsigned char mac4[6]={0x00,0x1a,0xca,0x00,0xd2,0xa1};
unsigned char mac5[6]={0x00,0x1a,0xca,0x00,0xd2,0x9a};
unsigned char mac6[6]={0x00,0x1a,0xca,0x00,0xd2,0x9b};
unsigned char mac7[6]={0x00,0x1a,0xca,0x00,0xd2,0x9c};
unsigned char mac8[6]={0x00,0x1a,0xca,0x00,0xd2,0x9d};
unsigned char macb[6]={0xff,0xff,0xff,0xff,0xff,0xff};

struct PORT ports[PORT_NUM];
int cpu_tick;
int cpu_main;

int num_recv()
{
	int sum,i,j;
	sum=0;
	for(i=0;i<PORT_NUM;i++)
	{
		for(j=0;j<WORKER_NUM;j++)
		{
			sum+=ports[i].in_num[j];
		}
	}
	return sum;
}
void show_all_ports()
{
	int i,j;
	for(i=0;i<PORT_NUM;i++)
	{
		printf("port:%d\n",i);
		for(j=0;j<WORKER_NUM;j++)
		{
			printf("worker:%d in:%d\n",j,ports[i].in_num[j]);
		}
			printf("out:%d\n",ports[i].out_num);
	}
}
void init_all_ports()
{
	init_port(&ports[0],"xgbe2",mac2,WORKER_NUM);
	init_port(&ports[1],"xgbe5",mac5,WORKER_NUM);
	cpu_tick=cpu.cur;
	cpu.cur++;
	cpu_main=cpu.cur;
	cpu.cur++;
}
void init_port(struct PORT * port,char * name,unsigned char * mac,int num_workers)
{
	int result;
	int instance;
	int i;
	for(i=0;i<6;i++)
	{
		port->mac[i]=mac[i];
	}
	port->name=name;
	gxio_mpipe_context_t* context=&(port->context);
	port->equeue=&(port->equeue_body);
	port->num_workers=num_workers;
	port->out_num=0;
	for(i=0;i<num_workers;i++)
	{
		port->in_num[i]=0;
	}

	// Get the instance.
	instance = gxio_mpipe_link_instance(port->name);
	if (instance < 0)
		tmc_task_die("Link '%s' does not exist.", port->name);

	// Start the driver.
	result = gxio_mpipe_init(NULL, instance);
	VERIFY(result, "gxio_mpipe_init()");
	context = GXIO_MPIPE_CONTEXT(instance);

	gxio_mpipe_link_t link;
	result = gxio_mpipe_link_open(&link, context, port->name, 0);
	VERIFY(result, "gxio_mpipe_link_open()");

	int channel = gxio_mpipe_link_channel(&link);

	// Allocate some iqueues.
	port->iqueues = calloc(num_workers, sizeof(*(port->iqueues)));
	if (port->iqueues == NULL)
		tmc_task_die("Failure in 'calloc()'.");

	// Allocate some NotifRings.
	result = gxio_mpipe_alloc_notif_rings(context, num_workers, 0, 0);
	VERIFY(result, "gxio_mpipe_alloc_notif_rings()");
	unsigned int ring = result;

	// Init the NotifRings.
	size_t notif_ring_entries = 512;
	size_t notif_ring_size = notif_ring_entries * sizeof(gxio_mpipe_idesc_t);
	size_t needed = notif_ring_size + sizeof(gxio_mpipe_iqueue_t);
	for (int i = 0; i < num_workers; i++)
	{
		tmc_alloc_t alloc = TMC_ALLOC_INIT;
		tmc_alloc_set_home(&alloc, tmc_cpus_find_nth_cpu(&(cpu.set), cpu.cur));
		port->cpu_in[i]=cpu.cur;
		cpu.cur++;
		// The ring must use physically contiguous memory, but the iqueue
		// can span pages, so we use "notif_ring_size", not "needed".
		tmc_alloc_set_pagesize(&alloc, notif_ring_size);
		void* iqueue_mem = tmc_alloc_map(&alloc, needed);
		if (iqueue_mem == NULL)
			tmc_task_die("Failure in 'tmc_alloc_map()'.");
		gxio_mpipe_iqueue_t* iqueue = iqueue_mem + notif_ring_size;
		result = gxio_mpipe_iqueue_init(iqueue, context, ring + i,
				iqueue_mem, notif_ring_size, 0);
		VERIFY(result, "gxio_mpipe_iqueue_init()");
		port->iqueues[i] = iqueue;
	}


	// Allocate a NotifGroup.
	result = gxio_mpipe_alloc_notif_groups(context, 1, 0, 0);
	VERIFY(result, "gxio_mpipe_alloc_notif_groups()");
	int group = result;

	// Allocate some buckets. The default mPipe classifier requires
	// the number of buckets to be a power of two (maximum of 4096).
	int num_buckets = 1024;
	result = gxio_mpipe_alloc_buckets(context, num_buckets, 0, 0);
	VERIFY(result, "gxio_mpipe_alloc_buckets()");
	int bucket = result;

	// Init group and buckets, preserving packet order among flows.
	gxio_mpipe_bucket_mode_t mode = GXIO_MPIPE_BUCKET_DYNAMIC_FLOW_AFFINITY;
	result = gxio_mpipe_init_notif_group_and_buckets(context, group,
			ring, num_workers,
			bucket, num_buckets, mode);
	VERIFY(result, "gxio_mpipe_init_notif_group_and_buckets()");


	// Initialize the equeue.

	result = gxio_mpipe_alloc_edma_rings(context, 1, 0, 0);
	VERIFY(result, "gxio_mpipe_alloc_edma_rings");
	uint ering = result;
	size_t edescs_size = equeue_entries * sizeof(gxio_mpipe_edesc_t);
	tmc_alloc_t edescs_alloc = TMC_ALLOC_INIT;
	tmc_alloc_set_home(&edescs_alloc, tmc_cpus_find_nth_cpu(&(cpu.set), cpu.cur));
	port->cpu_out=cpu.cur;
	cpu.cur++;
	tmc_alloc_set_pagesize(&edescs_alloc, edescs_size);
	void* edescs = tmc_alloc_map(&edescs_alloc, edescs_size);
	if (edescs == NULL)
		tmc_task_die("Failed to allocate equeue memory.");
	result = gxio_mpipe_equeue_init(port->equeue, context, ering, channel,
			edescs, edescs_size, 0);
	VERIFY(result, "gxio_gxio_equeue_init()");

	// Use enough small/large buffers to avoid ever getting "idesc->be".
	unsigned int num_bufs = equeue_entries + num_workers * notif_ring_entries;

	// Allocate small/large/jumbo buffer stacks.
	result = gxio_mpipe_alloc_buffer_stacks(context, 2, 0, 0);
	VERIFY(result, "gxio_mpipe_alloc_buffer_stacks()");
	int stack_idx = result;

	// Initialize small/large stacks.
	create_stack(context, stack_idx + 0, GXIO_MPIPE_BUFFER_SIZE_128, num_bufs);
	create_stack(context, stack_idx + 1, GXIO_MPIPE_BUFFER_SIZE_1664, num_bufs);

	// Register for packets.
	gxio_mpipe_rules_t rules;
	gxio_mpipe_rules_init(&rules,context);
	gxio_mpipe_rules_begin(&rules, bucket, num_buckets, NULL);
	result = gxio_mpipe_rules_commit(&rules);
	VERIFY(result, "gxio_mpipe_rules_commit()");
}

void create_stack(gxio_mpipe_context_t* context, int stack_idx,gxio_mpipe_buffer_size_enum_t buf_size, int num_buffers)
{
	int result;

	// Extract the actual buffer size from the enum.
	size_t size = gxio_mpipe_buffer_size_enum_to_buffer_size(buf_size);

	// Compute the total bytes needed for the stack itself.
	size_t stack_bytes = gxio_mpipe_calc_buffer_stack_bytes(num_buffers);

	// Round up so that the buffers will be properly aligned.
	stack_bytes += -(long)stack_bytes & (128 - 1);

	// Compute the total bytes needed for the stack plus the buffers.
	size_t needed = stack_bytes + num_buffers * size;

	// Allocate up to 16 pages of the smallest suitable pagesize.
	tmc_alloc_t alloc = TMC_ALLOC_INIT;
	tmc_alloc_set_pagesize(&alloc, needed / 16);
	size_t pagesize = tmc_alloc_get_pagesize(&alloc);
	int pages = (needed + pagesize - 1) / pagesize;
	void* mem = tmc_alloc_map(&alloc, pages * pagesize);
	if (mem == NULL)
		tmc_task_die("Could not allocate buffer pages.");

	// Initialize the buffer stack.
	result = gxio_mpipe_init_buffer_stack(context, stack_idx, buf_size,mem, stack_bytes, 0);

	// Register the buffer pages.
	for (int i = 0; i < pages; i++)
	{
		result = gxio_mpipe_register_page(context, stack_idx,
				mem + i * pagesize, pagesize, 0);
	}

	// Push the actual buffers.
	mem += stack_bytes;
	for (int i = 0; i < num_buffers; i++)
	{
		gxio_mpipe_push_buffer(context, stack_idx, mem);
		mem += size;
	}
}
