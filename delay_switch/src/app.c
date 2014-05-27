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
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

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


// Maximum "batch" size.
#define BATCH 16


// Help synchronize thread creation.
static tmc_sync_barrier_t sync_barrier;
static tmc_spin_barrier_t spin_barrier;

// True if "--flip" was specified.
static bool flip;

// True if "--jumbo" was specified.
static bool jumbo;

// The number of packets to process.
static int num_packets = 1000;

// The flag to indicate packet forward is done.
static volatile bool done = false;

// The number of workers to use.
#ifdef NUM_WORKERS
static unsigned int num_workers = NUM_WORKERS;
#else
static unsigned int num_workers = 1;
#endif

// The number of entries in the equeue ring - 2K (2048).
static unsigned int equeue_entries = GXIO_MPIPE_EQUEUE_ENTRY_2K;

// The initial affinity.
static cpu_set_t cpus;

// The mPIPE instance.
static int instance;

// The mpipe context (shared by all workers).
static gxio_mpipe_context_t context_body;
static gxio_mpipe_context_t* const context = &context_body;

// The ingress queues (one per worker).
static gxio_mpipe_iqueue_t** iqueues;

// The egress queue (shared by all workers).
static gxio_mpipe_equeue_t equeue_body;
static gxio_mpipe_equeue_t* const equeue = &equeue_body;

// The total number of packets forwarded by all workers.
// Reserve a cacheline for "total" to eliminate the false sharing.
#define total total64.v
struct {
  volatile unsigned long v __attribute__ ((aligned(CHIP_L2_LINE_SIZE())));
} total64 = { 0 };

// Help check for errors.
#define VERIFY(VAL, WHAT)                                       \
  do {                                                          \
    long long __val = (VAL);                                    \
    if (__val < 0)                                              \
      tmc_task_die("Failure in '%s': %lld: %s.",                \
                   (WHAT), __val, gxio_strerror(__val));        \
  } while (0)


// Allocate memory for a buffer stack and its buffers, initialize the
// stack, and push buffers onto it.
//
static void
create_stack(gxio_mpipe_context_t* context, int stack_idx,
             gxio_mpipe_buffer_size_enum_t buf_size, int num_buffers)
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
  result = gxio_mpipe_init_buffer_stack(context, stack_idx, buf_size,
                                        mem, stack_bytes, 0);
  VERIFY(result, "gxio_mpipe_init_buffer_stack()");

  // Register the buffer pages.
  for (int i = 0; i < pages; i++)
  {
    result = gxio_mpipe_register_page(context, stack_idx,
                                      mem + i * pagesize, pagesize, 0);
    VERIFY(result, "gxio_mpipe_register_page()");
  }

  // Push the actual buffers.
  mem += stack_bytes;
  for (int i = 0; i < num_buffers; i++)
  {
    gxio_mpipe_push_buffer(context, stack_idx, mem);
    mem += size;
  }
}


// The main function for each worker thread.
//
static void*
main_aux(void* arg)
{
  int result;

  int rank = (long)arg;


  // Bind to a single cpu.
  result = tmc_cpus_set_my_cpu(tmc_cpus_find_nth_cpu(&cpus, rank));
  VERIFY(result, "tmc_cpus_set_my_cpu()");

  mlockall(MCL_CURRENT);
  tmc_sync_barrier_wait(&sync_barrier);
  tmc_spin_barrier_wait(&spin_barrier);

  result = set_dataplane(DP_DEBUG);
  VERIFY(result, "set_dataplane()");

  tmc_spin_barrier_wait(&spin_barrier);

  if (rank == 0)
  {
    // HACK: Pause briefly, to let everyone finish passing the barrier.
    for (int i = 0; i < 10000; i++)
      __insn_mfspr(SPR_PASS);

    // Allow packets to flow.
    sim_enable_mpipe_links(instance, -1);
  }


  // Forward packets.

  gxio_mpipe_iqueue_t* iqueue = iqueues[rank];

  // Local version of "total". The optimization is to save a global variable
  // load (most likely in L3) per iteration and share invalidation.
  unsigned long local_total = 0;

  while (local_total < num_packets || num_packets < 0)
  {
    // Wait for packets to arrive.

    gxio_mpipe_idesc_t* idescs;

    int n = gxio_mpipe_iqueue_try_peek(iqueue, &idescs);
    if (n > BATCH)
      n = BATCH;
    else if (n <= 0)
    {
      if (done)
        goto  L_done;
      continue;
    }
    // Prefetch the actual idescs.
    tmc_mem_prefetch(idescs, n * sizeof(*idescs));

    gxio_mpipe_edesc_t edescs[BATCH];

    for (int i = 0; i < n; i++)
    {
      gxio_mpipe_idesc_t* idesc = &idescs[i];
      gxio_mpipe_edesc_t* edesc = &edescs[i];

      // Prepare to forward (or drop).
      gxio_mpipe_edesc_copy_idesc(edesc, idesc);

      // Drop "error" packets (but ignore "checksum" problems).
      if (idesc->be || idesc->me || idesc->tr || idesc->ce)
        edesc->ns = 1;
    }

    if (flip)
    {
      for (int i = 0; i < n; i++)
      {
        if (!edescs[i].ns)
        {
          void* start = gxio_mpipe_idesc_get_l2_start(&idescs[i]);
          size_t length = gxio_mpipe_idesc_get_l2_length(&idescs[i]);

          // Prefetch entire packet (12 bytes would be sufficient).
          tmc_mem_prefetch(start, length);
        }
      }

      for (int i = 0; i < n; i++)
      {
        if (!edescs[i].ns)
        {
          void* start = gxio_mpipe_idesc_get_l2_start(&idescs[i]);

          // Flip source/dest mac addresses.
          unsigned char tmac[6];
          void* dmac = start + 0;
          void* smac = start + 6;
          memcpy(tmac, dmac, 6);
          memcpy(dmac, smac, 6);
          memcpy(smac, tmac, 6);
        }
      }

      // Flush.
      __insn_mf();
    }

    // Reserve slots.  NOTE: This might spin.
    long slot = gxio_mpipe_equeue_reserve_fast(equeue, n);

    // Egress the packets.
    for (int i = 0; i < n; i++)
      gxio_mpipe_equeue_put_at(equeue, edescs[i], slot + i);

    // Consume the packets.
    for (int i = 0; i < n; i++)
      gxio_mpipe_iqueue_consume(iqueue, &idescs[i]);

    // Count packets (atomically). Let the local_total be the new total
    // value after the atomic add. Note: there is no explicit load to "total".
    local_total = arch_atomic_add(&total, n) + n;
  }

  // Forwarding is done!
  done = true;

  // Make done visible to all workers.
  __insn_mf();

 L_done:

  return (void*)NULL;
}


int
main(int argc, char** argv)
{
  int result;

  char* link_name = "gbe0";

  // Parse args.
  for (int i = 1; i < argc; i++)
  {
    char* arg = argv[i];

    // --link <link_name>, link_name is for both ingress and egress.
    if (!strcmp(arg, "--link") && i + 1 < argc)
    {
      link_name = argv[++i];
    }
    else if (!strcmp(arg, "-n") && i + 1 < argc)
    {
      num_packets = atoi(argv[++i]);
    }
    else if (!strcmp(arg, "-w") && i + 1 < argc)
    {
      num_workers = atoi(argv[++i]);
    }
    else if (!strcmp(arg, "--flip"))
    {
      flip = true;
    }
    else if (!strcmp(arg, "--jumbo"))
    {
      jumbo = true;
    }
    else
    {
      tmc_task_die("Unknown option '%s'.", arg);
    }
  }


  // Determine the available cpus.
  result = tmc_cpus_get_my_affinity(&cpus);
  VERIFY(result, "tmc_cpus_get_my_affinity()");

  if (tmc_cpus_count(&cpus) < num_workers)
    tmc_task_die("Insufficient cpus.");


  // Get the instance.
  instance = gxio_mpipe_link_instance(link_name);
  if (instance < 0)
    tmc_task_die("Link '%s' does not exist.", link_name);

  // Start the driver.
  result = gxio_mpipe_init(context, instance);
  VERIFY(result, "gxio_mpipe_init()");

  gxio_mpipe_link_t link;
  result = gxio_mpipe_link_open(&link, context, link_name, 0);
  VERIFY(result, "gxio_mpipe_link_open()");

  int channel = gxio_mpipe_link_channel(&link);

  if (jumbo)
  {
    // Allow the link to receive jumbo packets.
    gxio_mpipe_link_set_attr(&link, GXIO_MPIPE_LINK_RECEIVE_JUMBO, 1);
  }


  // Allocate some iqueues.
  iqueues = calloc(num_workers, sizeof(*iqueues));
  if (iqueues == NULL)
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
    tmc_alloc_set_home(&alloc, tmc_cpus_find_nth_cpu(&cpus, i));
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
    iqueues[i] = iqueue;
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
  tmc_alloc_set_pagesize(&edescs_alloc, edescs_size);
  void* edescs = tmc_alloc_map(&edescs_alloc, edescs_size);
  if (edescs == NULL)
    tmc_task_die("Failed to allocate equeue memory.");
  result = gxio_mpipe_equeue_init(equeue, context, ering, channel,
                                  edescs, edescs_size, 0);
  VERIFY(result, "gxio_gxio_equeue_init()");


  // Use enough small/large buffers to avoid ever getting "idesc->be".
  unsigned int num_bufs = equeue_entries + num_workers * notif_ring_entries;

  // Allocate small/large/jumbo buffer stacks.
  result = gxio_mpipe_alloc_buffer_stacks(context, jumbo ? 3 : 2, 0, 0);
  VERIFY(result, "gxio_mpipe_alloc_buffer_stacks()");
  int stack_idx = result;

  // Initialize small/large stacks.
  create_stack(context, stack_idx + 0, GXIO_MPIPE_BUFFER_SIZE_128, num_bufs);
  create_stack(context, stack_idx + 1, GXIO_MPIPE_BUFFER_SIZE_1664, num_bufs);

  if (jumbo)
  {
    // Initialize jumbo stack.  We use 16K buffers, because 4K buffers
    // are too small, and 10K buffers can induce "false chaining".  We
    // use only 4 buffers per worker, because they use a lot of memory,
    // and the risk of "idesc->be" is low.
    create_stack(context, stack_idx + 2, GXIO_MPIPE_BUFFER_SIZE_16384,
                 num_workers * 4);

    // Make sure all "possible" jumbo packets can be egressed safely.
    result = gxio_mpipe_equeue_set_snf_size(equeue, 10384);
    VERIFY(result, "gxio_mpipe_equeue_set_snf_size()");
  }


  // Register for packets.
  gxio_mpipe_rules_t rules;
  gxio_mpipe_rules_init(&rules, context);
  gxio_mpipe_rules_begin(&rules, bucket, num_buckets, NULL);
  result = gxio_mpipe_rules_commit(&rules);
  VERIFY(result, "gxio_mpipe_rules_commit()");


  tmc_sync_barrier_init(&sync_barrier, num_workers);
  tmc_spin_barrier_init(&spin_barrier, num_workers);

  pthread_t threads[num_workers];
  for (int i = 1; i < num_workers; i++)
  {
    if (pthread_create(&threads[i], NULL, main_aux, (void*)(intptr_t)i) != 0)
      tmc_task_die("Failure in 'pthread_create()'.");
  }
  (void)main_aux((void*)(intptr_t)0);
  for (int i = 1; i < num_workers; i++)
  {
    if (pthread_join(threads[i], NULL) != 0)
      tmc_task_die("Failure in 'pthread_join()'.");
  }

  // FIXME: Wait until pending egress is "done".
  for (int i = 0; i < 1000000; i++)
    __insn_mfspr(SPR_PASS);

  return 0;
}
