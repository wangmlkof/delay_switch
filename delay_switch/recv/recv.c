// Copyright 2013 Tilera Corporation. All Rights Reserved.
//
//   The source code contained or described herein and all documents
//   related to the source code ("Material") are owned by Tilera
//   Corporation or its suppliers or licensors.  Title to the Material
//   remains with Tilera Corporation or its suppliers and licensors. The
//   software is licensed under the Tilera MDE License.
//
//   Unless otherwise agreed by Tilera in writing, you may not remove or
//   alter this notice or any other notice embedded in Materials by Tilera
//   or Tilera's suppliers or licensors in any way.
//
//

#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include <tmc/alloc.h>

#include <arch/sim.h>

#include <gxio/mpipe.h>

#include <tmc/cpus.h>
#include <tmc/task.h>

typedef struct
{
	unsigned int magic_number;
	unsigned short version_major;
	unsigned short version_minor;
	unsigned int this_zone;
	unsigned int sig_figs;
	unsigned int snap_len;
	unsigned int network;
} pcap_file_header_t;

//! Bytes preceding each header in a .pcap file.
typedef struct
{
	unsigned int ts_sec;
	unsigned int ts_usec;
	unsigned int incl_len;
	unsigned int orig_len;
} pcap_packet_header_t;

// Define this to verify a bunch of facts about each packet.
#define PARANOIA

// Align "p" mod "align", assuming "p" is a "void*".
#define ALIGN(p, align) do { (p) += -(long)(p) & ((align) - 1); } while(0)

#define VERIFY(VAL, WHAT)                                       \
	do {                                                          \
		long long __val = (VAL);                                    \
		if (__val < 0)                                              \
		tmc_task_die("Failure in '%s': %lld: %s.",                \
				(WHAT), __val, gxio_strerror(__val));        \
	} while (0)

int main(int argc, char** argv)
{
	char* link_name = "gbe0";
	char * outname="recv_record.pcap";

	// There are 1000 packets in "input.pcap".
	int num_packets = 1000;

	struct timespec ts;
	pcap_file_header_t fh;
	pcap_packet_header_t ph;
	FILE * fw=fopen(outname,"w");
	// Parse args.
	for (int i = 1; i < argc; i++)
	{
		char* arg = argv[i];

		if (!strcmp(arg, "--link") && i + 1 < argc)
		{
			link_name = argv[++i];
		}
		else if (!strcmp(arg, "-n") && i + 1 < argc)
		{
			num_packets = atoi(argv[++i]);
		}
		else if (!strcmp(arg, "-o") && i + 1 < argc)
		{
			outname= argv[++i];
		}
		else
		{
			tmc_task_die("Unknown option '%s'.", arg);
		}
	}
	//ready for the receive record file
	fh.magic_number=0xa1b2c3d4;
	fh.version_major=0x0002;
	fh.version_minor=0x0004;
	fh.this_zone=0x00000000;
	fh.sig_figs=0x00000000;
	fh.snap_len=0x0000ffff;
	fh.network=0x00000001;
	fwrite(&fh,1,sizeof(fh),fw);	

	int instance;

	gxio_mpipe_context_t context_body;
	gxio_mpipe_context_t* context = &context_body;

	gxio_mpipe_iqueue_t iqueue_body;
	gxio_mpipe_iqueue_t* iqueue = &iqueue_body;

	int result;
	// Bind to a single cpu.
	cpu_set_t cpus;
	result = tmc_cpus_get_my_affinity(&cpus);
	VERIFY(result, "tmc_cpus_get_my_affinity()");
	result = tmc_cpus_set_my_cpu(tmc_cpus_find_first_cpu(&cpus));
	VERIFY(result, "tmc_cpus_set_my_cpu()");

	// Get the instance.
	instance = gxio_mpipe_link_instance(link_name);
	if (instance < 0)
		tmc_task_die("Link '%s' does not exist.", link_name);

	// Start the driver.
	result = gxio_mpipe_init(context, instance);
	VERIFY(result, "gxio_mpipe_init()");

	struct timeval now;
	struct timespec sts;
	gettimeofday(&now, NULL);
	sts.tv_sec = now.tv_sec;
	sts.tv_nsec = now.tv_usec * 1000;
	gxio_mpipe_set_timestamp(context, &sts);

	gxio_mpipe_link_t lnk;
	result = gxio_mpipe_link_open(&lnk, context, link_name, 0);
	VERIFY(result, "gxio_mpipe_link_open()");

	// Allocate one huge page to hold our buffer stack, notif ring, and
	// packets.  This should be more than enough space.
	size_t page_size = tmc_alloc_get_huge_pagesize();
	tmc_alloc_t alloc = TMC_ALLOC_INIT;
	tmc_alloc_set_huge(&alloc);
	void* page = tmc_alloc_map(&alloc, page_size);
	assert(page);

	void* mem = page;
	// Allocate a NotifRing.
	result = gxio_mpipe_alloc_notif_rings(context, 1, 0, 0);
	VERIFY(result, "gxio_mpipe_alloc_notif_rings()");
	int ring = result;

	// Init the NotifRing.
	size_t notif_ring_entries = 128;
	size_t notif_ring_size = notif_ring_entries * sizeof(gxio_mpipe_idesc_t);
	result = gxio_mpipe_iqueue_init(iqueue, context, ring,
			mem, notif_ring_size, 0);
	VERIFY(result, "gxio_mpipe_iqueue_init()");
	mem += notif_ring_size;


	// Allocate a NotifGroup.
	result = gxio_mpipe_alloc_notif_groups(context, 1, 0, 0);
	VERIFY(result, "gxio_mpipe_alloc_notif_groups()");
	int group = result;

	// Allocate a bucket.
	int num_buckets = 1;
	result = gxio_mpipe_alloc_buckets(context, num_buckets, 0, 0);
	VERIFY(result, "gxio_mpipe_alloc_buckets()");
	int bucket = result;

	// Init group and bucket.
	gxio_mpipe_bucket_mode_t mode = GXIO_MPIPE_BUCKET_ROUND_ROBIN;
	result = gxio_mpipe_init_notif_group_and_buckets(context, group,
			ring, 1,
			bucket, num_buckets, mode);
	VERIFY(result, "gxio_mpipe_init_notif_group_and_buckets()");


	// Allocate a buffer stack.
	result = gxio_mpipe_alloc_buffer_stacks(context, 1, 0, 0);
	VERIFY(result, "gxio_mpipe_alloc_buffer_stacks()");
	int stack_idx = result;

	// Total number of buffers.
	unsigned int num_buffers = notif_ring_entries;

	// Initialize the buffer stack.  Must be aligned mod 64K.
	ALIGN(mem, 0x10000);
	size_t stack_bytes = gxio_mpipe_calc_buffer_stack_bytes(num_buffers);
	gxio_mpipe_buffer_size_enum_t buf_size = GXIO_MPIPE_BUFFER_SIZE_1664;
	result = gxio_mpipe_init_buffer_stack(context, stack_idx, buf_size,
			mem, stack_bytes, 0);
	VERIFY(result, "gxio_mpipe_init_buffer_stack()");
	mem += stack_bytes;

	ALIGN(mem, 0x10000);

	// Register the entire huge page of memory which contains all the buffers.
	result = gxio_mpipe_register_page(context, stack_idx, page, page_size, 0);
	VERIFY(result, "gxio_mpipe_register_page()");

	// Push some buffers onto the stack.
	for (int i = 0; i < num_buffers; i++)
	{
		gxio_mpipe_push_buffer(context, stack_idx, mem);
		mem += 1664;
	}

	// Paranoia.
	assert(mem <= page + page_size);


	// Register for packets.
	gxio_mpipe_rules_t rules;
	gxio_mpipe_rules_init(&rules, context);
	gxio_mpipe_rules_begin(&rules, bucket, num_buckets, NULL);
	result = gxio_mpipe_rules_commit(&rules);
	VERIFY(result, "gxio_mpipe_rules_commit()");


	// Allow packets to flow.
	sim_enable_mpipe_links(instance, -1);


	// Process packets.
	int handled = 0;

	printf("prepare OK!\n");
	while (handled < num_packets)
	{
		// Wait for next packet.
		gxio_mpipe_idesc_t idesc;
		result = gxio_mpipe_iqueue_get(iqueue, &idesc);
		VERIFY(result, "gxio_mpipe_iqueue_get()");
		
		void* start = gxio_mpipe_idesc_get_va(&idesc);
		uint32_t  len2 = gxio_mpipe_idesc_get_xfer_size(&idesc);
		if(len2!=119&&len2!=178)
		{
			gxio_mpipe_get_timestamp(context, &ts);
			ph.ts_sec=ts.tv_sec;
			ph.ts_usec=ts.tv_nsec;
			ph.incl_len=len2;
			ph.orig_len=len2;

			fwrite(&ph,1,sizeof(ph),fw);
			fwrite(start,1,(size_t)(len2),fw);	
			handled++;
		}
		// Just "drop" the packet.
		gxio_mpipe_iqueue_drop(iqueue, &idesc);

	}

	printf("link %s totally recv %d packet!recv OK!\n",link_name,handled);
	fclose(fw);
	return 0;
}
