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
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include <sys/mman.h>
#include <sys/dataplane.h>

#include <tmc/alloc.h>

#include <arch/atomic.h>
#include <arch/sim.h>

#include <gxio/mpipe.h>

#include <tmc/cpus.h>
#include <tmc/mem.h>
#include <tmc/spin.h>
#include <tmc/sync.h>
#include <tmc/task.h>


// Align "p" mod "align", assuming "p" is a "void*".
#define ALIGN(p, align) do { (p) += -(long)(p) & ((align) - 1); } while(0)


// Help check for errors.
#define VERIFY(VAL, WHAT)                                       \
	do {                                                          \
		long long __val = (VAL);                                    \
		if (__val < 0)                                              \
		tmc_task_die("Failure in '%s': %lld: %s.",                \
				(WHAT), __val, gxio_strerror(__val));        \
	} while (0)


//! The header at the start of each .pcap file.
typedef struct
{
	uint32_t magic_number;
	uint16_t version_major;
	uint16_t version_minor;
	int32_t  this_zone;
	uint32_t sig_figs;
	uint32_t snap_len;
	uint32_t network;
} pcap_file_header_t;


//! Bytes preceding each header in a .pcap file.
typedef struct
{
	uint32_t ts_sec;
	uint32_t ts_usec;
	uint32_t incl_len;
	uint32_t orig_len;
} pcap_packet_header_t;


void mydelay(double delay,double multiple)
{
	double m,i,j;
	for(m = 0;m < multiple;m++)
		for(i = 0;i < delay;i++)
			for(j = 0;j < delay;j++)
			{
				continue;
			}
}

// Read bytes from a file, or die, optionally allowing EOF.
//
	static bool
read_or_die(int fd, void *buf, size_t count, bool allow_eof)
{
	ssize_t n = read(fd, buf, count);

	if (allow_eof && n == 0)
		return true;

	if (n != count)
		tmc_task_die("Failed to read %zd bytes, got %zd bytes, error %d\n",
				count, n, errno);

	return false;
}


// Load a pcap file into memory in a format suitable for blasting.
//
// We process the file once to determine how many pages will be needed
// to hold all the packets, and then we allocate that many pages and
// register them for the global buffer stack, and then we process the
// file again to actually load the packet data into memory.
//
// Each packet is a two byte size, followed by the packet data
// (conveniently aligning its L3 data), plus padding bytes to align
// the next packet to a cache line.
//
// A "size" of zero is special and indicates the end of the data.
//
// NOTE: We assume the file does not change while we are reading it.
//
// ISSUE: Using "FILE*" would actually be more efficient.
//
// FIXME: Using separate memory for the "sizes" might actually be
// more efficient.
//
// NOTE: We could use multiple buffer stacks, with up 16 (smaller)
// pages registered for each, but this would complicate the example.
//
static size_t load_packets(gxio_mpipe_context_t* context, int stack_idx,
		const char* file, uint16_t** sizesp, void** packetsp)
{
	// Total packets in the file.
	size_t num = 0;

	// Total bytes of packet data.
	size_t space = 0;

	int fd = open(file, O_RDONLY);
	if (fd < 0)
		tmc_task_die("Cannot open '%s'.", file);

	// Assume the header is valid.
	pcap_file_header_t fh;
	read_or_die(fd, &fh, sizeof(fh), false);

	// Allow reset below.
	off_t reset = lseek(fd, 0, SEEK_CUR);

	while (1)
	{
		// Read the next packet header.
		pcap_packet_header_t ph;
		if (read_or_die(fd, &ph, sizeof(ph), true))
			break;
		size_t osize = ph.orig_len;
		size_t isize = ph.incl_len;

		// The "edesc.xfer_size" field is only 14 bits.
		if (isize > 16383 || osize > 16383)
			tmc_task_die("Packet %zd is too large.", num);

		// Skip over the packet bytes.
		lseek(fd, isize, SEEK_CUR);

		// Track totals.
		space += osize;
		num++;
	}

	// Allocate at most 16 "pages" of memory for packets, using
	// "huge" (or even "super huge") pages if needed, so we can
	// "register" them all for a single buffer stack below.
	tmc_alloc_t alloc = TMC_ALLOC_INIT;
	tmc_alloc_set_pagesize(&alloc, (space + 15) / 16);
	void* mem = tmc_alloc_map(&alloc, space);
	if (mem == NULL)
		tmc_task_die("Failed to allocate memory for %zd packet bytes.", space);

	// Register the pages.
	size_t pagesize = tmc_alloc_get_pagesize(&alloc);
	size_t pages = (space + pagesize - 1) / pagesize;
	for (int i = 0; i < pages; i++)
	{
		int result =
			gxio_mpipe_register_page(context, stack_idx,
					mem + i * pagesize, pagesize, 0);
		VERIFY(result, "gxio_mpipe_register_page()");
	}

	// Allocate "sizes". */
	uint16_t* sizes = malloc(num * sizeof(uint16_t));
	if (sizes == NULL)
		tmc_task_die("Failed to allocate memory for %zd packet sizes.", num);

	// Rewind and restart.
	lseek(fd, reset, SEEK_SET);

	unsigned char* buf = mem;
	for (int i = 0; i < num; i++)
	{
		// Read the next packet header.
		pcap_packet_header_t ph;
		read_or_die(fd, &ph, sizeof(ph), false);
		size_t osize = ph.orig_len;
		size_t isize = ph.incl_len;

		// Read the packet bytes.
		read_or_die(fd, buf, isize, false);

		// Advance.
		buf += osize;

		// Store the size.
		sizes[i] = osize;
	}

	close(fd);

	// Flush the "packet" writes, so we can egress them properly.
	__insn_mf();

	*sizesp = sizes;
	*packetsp = mem;

	return num;
}



// The main loop.
//
	static void
main_loop(gxio_mpipe_equeue_t* equeue, int stack_idx,
		ssize_t limit, size_t num, double delay,uint16_t* sizes, void* packets,FILE * fw,gxio_mpipe_context_t * context)
{
	size_t slot = 0;
	size_t i = 0;
	unsigned char* buf = packets;
	pcap_packet_header_t ph;
	struct timespec ts;
	while (limit < 0 || slot < limit)
	{
		uint16_t size = sizes[i];
		if(size<400)
		{
			// Prepare to egress the packet.
			gxio_mpipe_edesc_t edesc = {{
				.bound = 1,
					.xfer_size = size,
					.va = (long)buf,
					.stack_idx = stack_idx,
					.size = GXIO_MPIPE_BUFFER_SIZE_128
			}};

			//  printf("Packet %lu has been egressed, which has size %d.\n",slot,size);

			// Reserve slots in batches of 128 (for efficiency).  This will
			// block, since we blast faster than the hardware can egress.
			if ((slot & (128 - 1)) == 0)
				gxio_mpipe_equeue_reserve_fast(equeue, 128);

			// Add a delay to adjust the speed
			mydelay(delay,1);

			gxio_mpipe_equeue_put_at(equeue, edesc, slot++);
			gxio_mpipe_get_timestamp(context, &ts);
			ph.ts_sec=ts.tv_sec;
			ph.ts_usec=ts.tv_nsec;
			ph.incl_len=size;
			ph.orig_len=size;
			fwrite(&ph,1,sizeof(ph),fw);
			fwrite(buf,1,(size_t)(size),fw);	

			// Advance, wrapping as needed.
			buf += size;
			if (++i == num)
			{
				i = 0;
				buf = packets;
			}
		}
		else
		{
			printf("big packet size : %d\n",size);
		}
	}

	// Consume any remaining slots.
	gxio_mpipe_edesc_t ns = {{ .ns = 1 }};
	while ((slot & (128 - 1)) != 0)
		gxio_mpipe_equeue_put_at(equeue, ns, slot++);
}


// The mpipe context.
static gxio_mpipe_context_t context_body;
static gxio_mpipe_context_t* const context = &context_body;

// The egress queue.
static gxio_mpipe_equeue_t equeue_body;
static gxio_mpipe_equeue_t* const equeue = &equeue_body;


int main(int argc, char** argv)
{
	int result;

	char* link_name = "gbe0";

	char * outname="send_record.pcap";
	const char * file = NULL;

	int link_flags = 0;

	ssize_t limit = -1;

	double delay = 0;

	int prepare =15;
	cpu_set_t dataplane;
	tmc_cpus_get_dataplane_cpus(&dataplane);

	// Parse args.
	for (int i = 1; i < argc; i++)
	{
		char* arg = argv[i];

		if (!strcmp(arg, "--link") && i + 1 < argc)
		{
			link_name = argv[++i];
		}
		else if (!strcmp(arg, "--wait"))
		{
			link_flags = GXIO_MPIPE_LINK_WAIT;
		}
		else if (!strcmp(arg, "-o"))
		{
			outname=argv[++i];
		}
		else if (!strcmp(arg, "-n") && i + 1 < argc)
		{
			limit = atoi(argv[++i]);
		}
		else if (!strcmp(arg, "--dataplane"))
		{
			if (tmc_cpus_set_my_affinity(&dataplane) != 0)
				tmc_task_die("Cannot move to a dataplane tile.");
		}
		else if (!strcmp(arg, "--tile") && i + 1 < argc)
		{
			int cpu = atoi(argv[++i]);
			if (tmc_cpus_set_my_cpu(cpu) != 0)
				tmc_task_die("Cannot move to cpu %d.", cpu);
		}
		else if(!strcmp(arg,"--delay") && i + 1 < argc)
		{
			delay = atof(argv[++i]);
		}
		else if(!strcmp(arg,"--prepare") && i + 1 < argc)
		{
			prepare = atof(argv[++i]);
		}
		else if (!strcmp(arg, "--"))
		{
			i++;
			break;
		}
		else if (arg[0] == '-')
		{
			tmc_task_die("Unknown option '%s'.", arg);
		}
		else
		{
			if (i + 1 == argc)
				file = arg;
			break;
		}
	}

	if (file == NULL)
		tmc_task_die("Need exactly one pcap file.");
	//get prepared record of the packet of sending
	FILE * fw=fopen(outname,"w");
	pcap_file_header_t fh;
	fh.magic_number=0xa1b2c3d4;
	fh.version_major=0x0002;
	fh.version_minor=0x0004;
	fh.this_zone=0x00000000;
	fh.sig_figs=0x00000000;
	fh.snap_len=0x0000ffff;
	fh.network=0x00000001;
	fwrite(&fh,1,sizeof(fh),fw);	

	// Get the mPIPE instance for the link.
	int instance = gxio_mpipe_link_instance(link_name);
	if (instance < 0)
		tmc_task_die("Link '%s' does not exist.", link_name);

	// Start the driver.
	result = gxio_mpipe_init(context, instance);
	VERIFY(result, "gxio_mpipe_init()");

	struct timeval now;
	struct timespec ts;
	gettimeofday(&now, NULL);
	ts.tv_sec = now.tv_sec;
	ts.tv_nsec = now.tv_usec * 1000;
	gxio_mpipe_set_timestamp(context, &ts);

	gxio_mpipe_link_t lnk;
	result = gxio_mpipe_link_open(&lnk, context, link_name, link_flags);
	VERIFY(result, "gxio_mpipe_link_open()");

	int channel = gxio_mpipe_link_channel(&lnk);


	// Initialize our edma ring.
	result = gxio_mpipe_alloc_edma_rings(context, 1, 0, 0);
	VERIFY(result, "gxio_mpipe_alloc_edma_rings");
	uint edma = result;
	unsigned int equeue_entries = 2048;
	size_t edma_ring_size = equeue_entries * sizeof(gxio_mpipe_edesc_t);
	tmc_alloc_t alloc = TMC_ALLOC_INIT;
	tmc_alloc_set_pagesize(&alloc, edma_ring_size);
	void* mem = tmc_alloc_map(&alloc, edma_ring_size);
	if (mem == NULL)
		tmc_task_die("Failed to allocate memory for the edma ring.");
	result = gxio_mpipe_equeue_init(equeue, context, edma, channel,
			mem, edma_ring_size, 0);
	VERIFY(result, "gxio_gxio_equeue_init()");


	// Allocate a buffer stack (but do not "initialize" it).
	result = gxio_mpipe_alloc_buffer_stacks(context, 1, 0, 0);
	VERIFY(result, "gxio_mpipe_alloc_buffer_stacks()");
	int stack_idx = result;


	// Load the packets.
	uint16_t* sizes;
	void* packets;
	size_t num = load_packets(context, stack_idx, file, &sizes, &packets);

	// Enter dataplane mode if locked to a dataplane tile.
	int cpu = tmc_cpus_get_my_cpu();
	if (tmc_cpus_has_cpu(&dataplane, cpu))
	{
		mlockall(MCL_CURRENT);
	}

	// Allow packets to flow.
	sim_enable_mpipe_links(instance, -1);

	//prepare for the switch to ready
	mydelay(10000,prepare);

	// Egress the packets.
	main_loop(equeue, stack_idx, limit, num, delay, sizes, packets,fw,context);
	// Wait until pending egress is "done".
	gxio_mpipe_edesc_t ns = {{ .ns = 1 }};
	for (int i = 0; i < equeue_entries; i++)
		gxio_mpipe_equeue_put(equeue, ns);

	fclose(fw);
	return 0;
}
