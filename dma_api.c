#include <stdio.h>
#include <semaphore.h>
#include <pthread.h>
#include <stddef.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include "dma.h"
#include "fw_log.h"

/*
 * DMA Observation Task:
 *
 * (1) Hardware Error:
 *     - Log relevant src address, dest address, and size in log file
 *       with timestamp whenever DMA_ERROR is observed on status register
 *     - FW log runs on separate thread and doesn't affect DMA performance
 *
 * (2) DMA Latency Measurement:
 *     - Record PTP register value before starting DMA transfer and after
 *       receiving interrupt for transfer completion
 *     - Calculate and log latency with transfer details
 *
 * (3) SG DMA Descriptor Validation:
 *     - Read and log all descriptors when SG DMA transfer completes
 *       with error for debugging purposes
 *
 * (4) SG DMA Resource Management:
 *     - Maintain a pool of SG descriptors and allocation tracking list
 *       to manage resources for asynchronous SG DMA transfers
 *
 * (5) DMA Transfer Statistics:
 *     - Maintain statistics for successful and failed DMA transfers
 *       to monitor performance and identify issues
 *
 * (6) DMA Transfer Sequence Tracking:
 *     - Maintain sequence number for each DMA transfer to correlate
 *       logs and identify specific transfers in case of issues
 *
 * (7) DMA Transfer Size Validation:
 *     - Validate transfer size for each DMA transfer
 *     - Log any transfers exceeding maximum supported size or zero size
 *
 * (8) DMA Alignment Validation:
 *     - Validate source and destination addresses for proper alignment
 *       based on platform requirements
 *     - Log any misaligned transfers
 *
 * (9) DMA Busy Timeout Handling:
 *     - Implement timeout mechanism when waiting for DMA hardware
 *       to become idle before starting new transfer
 *     - Log any occurrences of busy timeouts
 *
 * (10) DMA Silent/Dead Failure Handling:
 *      - Implement mechanism to detect silent failures (e.g., AXI bus
 *        errors, timeouts) without explicit error status
 *      - Log detected failures for further analysis
 *
 * (11) DMA Resource Leak Detection:
 *      - Detect potential resource leaks in SG descriptor allocations
 *        by tracking active allocations and completion status
 *      - Log any suspected leaks for further analysis
 *
 * (12) DMA Error Recovery:
 *      - Implement recovery mechanism from DMA errors (e.g., reset
 *        DMA controller and re-initiate transfer)
 *      - Log recovery actions for further analysis
 */

/* define the global DMA register instance referenced by dma.h */

/*To measure DMA transfer latency*/
// #define DMATRANSFER_MEASURE_LATENCY   1

/* DMA basic Configuration that needs for all platform*/
#define DMA_ALIGNMENT 8
#define DMA_MAX_TRANSFER_SIZE (1L << 24)
#define BUSY_RETRY_COUNT 1000

static sem_t dma_sem;

/* Transfer Debug variable */
static uint64_t current_src = 0;
static uint64_t current_dst = 0;
static uint32_t current_size = 0;

static uint64_t transfer_sequence = 0;
static uint64_t success_count = 0;
static uint64_t error_count = 0;

static sg_descriptor *sg_pool = NULL;
uint32_t sg_used_mask = 0;

/* protect SG descriptor pool and allocation list */
static pthread_mutex_t sg_mutex;

sg_allocation_t sg_allocations[MAX_SG_ALLOCATIONS];
int sg_allocation_count = 0;
#define SG_DESC_SIZE_POOL 24

#ifdef DMATRANSFER_MEASURE_LATENCY
static struct timespec start_time;
#endif

/*Note: Actual platform need cache flush before DMA transfer on embedded platform*/
static void dma_cache_flush(void *addr, size_t size)
{
    char *begin = (char *)addr;
    char *end = begin + size;

    __builtin___clear_cache(begin, end);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
}

/*Note: concurrent system need memeory barrier for ARC and ARM platform for  weakly ordered memeory model*/
/*This is just a concept here as Linux kernel use wmb(), smp_wmb() rmb() smp_rmb()*/
static inline void dma_memory_barrier(void)
{
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
}

/*Address must be aligned and alignment based on platform architecture*/
static int is_aligned(uint64_t addr)
{
    return ((addr % DMA_ALIGNMENT) == 0);
}

/*Get descriptor index from descriptor pointer*/
static int get_sg_descriptor_index(sg_descriptor *desc)
{
    if (sg_pool == NULL || desc == NULL)
        return -1;

    if (desc < sg_pool || desc >= &sg_pool[SG_DESC_SIZE_POOL])
        return -1;

    return (int)(desc - sg_pool);
}

/*Free SG descriptors by marking them as available*/
static int free_sg_descriptors(int start_idx, uint32_t count)
{
    if (start_idx < 0 || start_idx >= SG_DESC_SIZE_POOL || count == 0 || start_idx + count > SG_DESC_SIZE_POOL)
        return -1;

    for (uint32_t i = 0; i < count; i++)
    {
        sg_used_mask &= ~(1u << (start_idx + i));
    }
    return 0;
}

/* This is basically interrupt handler, get called when interrupt arrived*/
/*register a interrupt handler callback function that the system calls when the DMA hardware asserts the interrupt line*/
void dma_interrupt_handler(void)
{
    /*Clear the Interrupt Status*/
    dma_regs.DMA_HW_INTERRUPT_STATUS_REG = 1;
    sem_post(&dma_sem);
}

/*DMA success handle for direct mode and scatter-gather mode*/
static int handle_dma_success(uint8_t state, uint8_t sg_state, log_event_t *event)
{
    success_count++;

#ifdef DMATRANSFER_MEASURE_LATENCY
    struct timespec end_time;
    clock_gettime(CLOCK_MONOTONIC, &end_time);

    uint64_t start_us = (start_time.tv_sec * 1000000ULL) +
                        (start_time.tv_nsec / 1000ULL);

    uint64_t end_us = (end_time.tv_sec * 1000000ULL) +
                      (end_time.tv_nsec / 1000ULL);

    uint64_t latency_us = end_us - start_us;

    char latency_log[256];
    snprintf(latency_log, sizeof(latency_log),
             "DMA LATENCY: Transfer=%llu Size=%u Latency=%llu us",
             (unsigned long long)transfer_sequence,
             current_size,
             (unsigned long long)latency_us);

    fw_log_async(latency_log);
#endif
    /*store last successful details for debugging*/
    success_count++;

    /* If SG DMA completed, free the descriptors */
    if (sg_state == DMA_DONE && sg_allocation_count > 0)
    {
        /* Find active SG allocation and free it */
        pthread_mutex_lock(&sg_mutex);
        for (int k = 0; k < sg_allocation_count; k++)
        {
            if (sg_allocations[k].active)
            {
                free_sg_descriptors(get_sg_descriptor_index(sg_allocations[k].start_desc), sg_allocations[k].count);
                /* Remove from allocation list */
                for (int m = k; m < sg_allocation_count - 1; m++)
                {
                    sg_allocations[m] = sg_allocations[m + 1];
                }
                sg_allocation_count--;
                break;
            }
        }
        pthread_mutex_unlock(&sg_mutex);
        return 0;
    }

    event->type = LOG_DMA_SUCCESS;
    event->transfer_seq = transfer_sequence;
    event->src = current_src;
    event->dst = current_dst;
    event->size = current_size;
    event->success_count = success_count;
    event->error_count = error_count;

    // We can take PTP register value and store in log event on actual platform
    // Generally we dont store all successful transfer but this is just of debugging purpose.
    fw_log_event(event);
    return 0;
}

/*DMA error handler for direct mode and scatter-gather mode*/
static int handle_dma_error(uint8_t state, uint8_t sg_state, log_event_t *event)
{
    error_count++;

    /* If SG DMA error, free the descriptors after logging all details*/
    if (sg_state == DMA_ERROR && sg_allocation_count > 0)
    {
        /* Find active SG allocation and free it */
        pthread_mutex_lock(&sg_mutex);
        for (int k = 0; k < sg_allocation_count; k++)
        {
            if (sg_allocations[k].active)
            {
                char error_log[4096];
                int offset = snprintf(error_log, sizeof(error_log),
                                      "DMA ERROR: Transfer=%llu NumDesc=%u",
                                      (unsigned long long)transfer_sequence,
                                      sg_allocations[k].count);
                for (int i = 0; i < sg_allocations[k].count; i++)
                {
                    sg_descriptor *desc = &sg_allocations[k].start_desc[i];
                    offset += snprintf(error_log + offset, sizeof(error_log) - offset,
                                       " Desc%d: addr=%p src=%llu dst=%llu size=%u",
                                       i, (void *)desc,
                                       (unsigned long long)desc->src_addr,
                                       (unsigned long long)desc->dst_addr,
                                       desc->transfer_size);
                }
                fw_log_async(error_log);
                free_sg_descriptors(get_sg_descriptor_index(sg_allocations[k].start_desc), sg_allocations[k].count);
                /* Remove from allocation list */
                for (int m = k; m < sg_allocation_count - 1; m++)
                {
                    sg_allocations[m] = sg_allocations[m + 1];
                }
                sg_allocation_count--;
                break;
            }
        }
        pthread_mutex_unlock(&sg_mutex);

        return DMA_ERR_HW_FAILURE;
    }
    else if (state == DMA_ERROR)
    {
        char error_log[256];
        snprintf(error_log, sizeof(error_log),
                 "DMA ERROR: Transfer=%llu Src=%llu Dst=%llu Size=%u",
                 (unsigned long long)transfer_sequence,
                 (unsigned long long)current_src,
                 (unsigned long long)current_dst,
                 current_size);
        fw_log_async(error_log);
    }

    event->type = LOG_DMA_ERROR;
    event->transfer_seq = transfer_sequence;
    event->src = current_src;
    event->dst = current_dst;
    event->size = current_size;
    event->success_count = success_count;
    event->error_count = error_count;
    // We can take PTP register value and store in log event on actual platform
    // FW logging works on separate thread and it will not affect to actual performance of DMA transfer
    fw_log_event(event);
    return DMA_ERR_HW_FAILURE;
}

/*wait for receiving signal from Interrupt handler*/
static int wait_for_completion(void)
{
    sem_wait(&dma_sem);

    uint8_t state = firmware_read_status() & 0x03;
    uint8_t sg_state = firmware_sg_dma_read_status() & 0x03;

    log_event_t event = {0};
    if (state == DMA_DONE || sg_state == DMA_DONE)
    {
        return handle_dma_success(state, sg_state, &event);
    }

    if (state == DMA_ERROR || sg_state == DMA_ERROR)
    {
        return handle_dma_error(state, sg_state, &event);
    }

    return DMA_ERR;
}

/*This is a main DMA start function for DMA API*/
int firmware_start_dma(uint64_t src, uint64_t dst, uint32_t size)
{
    if (size == 0)
        return DMA_ERR_SIZE_ZERO;

    if (size > DMA_MAX_TRANSFER_SIZE)
        return DMA_ERR_SIZE_EXCEEDED;

    if (!is_aligned(src))
        return DMA_ERR_SRC_ALIGN;

    if (!is_aligned(dst))
        return DMA_ERR_DST_ALIGN;

    int retry = 0;
    /*Wait for DMA to be idle*/
    while ((firmware_read_status() & 0x03) != DMA_IDLE)
        if (++retry >= BUSY_RETRY_COUNT)
            return DMA_ERR_BUSY_TIMEOUT;

    /*Complete debug and error variable*/
    transfer_sequence++;

    current_src = src;
    current_dst = dst;
    current_size = size;

    /*cache flush*/
    dma_cache_flush((void *)src, size);

    /* Write 64-bit source address to lower and upper 32-bit registers */
    dma_regs.DMA_HW_SRC_REG_LOWER = (uint32_t)(src & 0xFFFFFFFFUL);
    dma_regs.DMA_HW_SRC_REG_UPPER = (uint32_t)((src >> 32) & 0xFFFFFFFFUL);

    /* Write 64-bit destination address to lower and upper 32-bit registers */
    dma_regs.DMA_HW_DST_REG_LOWER = (uint32_t)(dst & 0xFFFFFFFFUL);
    dma_regs.DMA_HW_DST_REG_UPPER = (uint32_t)((dst >> 32) & 0xFFFFFFFFUL);

    dma_regs.DMA_HW_SIZE_REG = size;

    /*weakly ordered memeory model*/
    dma_memory_barrier();

#ifdef DMATRANSFER_MEASURE_LATENCY
    clock_gettime(CLOCK_MONOTONIC, &start_time);
#endif

    dma_regs.DMA_HW_CMD_REG = 1;

    return wait_for_completion();
}

uint8_t firmware_read_status(void)
{
    return dma_regs.DMA_HW_STATUS;
}

void dma_fw_init(void)
{
    sem_init(&dma_sem, 0, 0);
    fw_log_init();
    sg_descriptor_pool_init();
}

void dma_fw_deinit(void)
{
    fw_log_shutdown();
    sem_destroy(&dma_sem);
    pthread_mutex_destroy(&sg_mutex);
}

/*Validate SG descriptor*/
static int validate_sg_descriptor(sg_descriptor *desc)
{
    if (desc == NULL)
        return DMA_ERR_DESC_PTR;

    if (desc->transfer_size == 0)
        return DMA_ERR_SIZE_ZERO;

    if (desc->transfer_size > DMA_MAX_TRANSFER_SIZE)
        return DMA_ERR_SIZE_EXCEEDED;

    if (!is_aligned(desc->src_addr))
        return DMA_ERR_SRC_ALIGN;

    if (!is_aligned(desc->dst_addr))
        return DMA_ERR_DST_ALIGN;

    return 0;
}

/*Queue a single SG descriptor to the DMA
int firmware_sg_dma_queue_descriptor(sg_descriptor *desc)
{
   if (validate_sg_descriptor(desc) != 0)
     return DMA_ERR_DESC_PTR;

    //*Cache flush for source data
    dma_cache_flush((void *)desc->src_addr, desc->transfer_size);

    ///*Update tail descriptor pointer to queue new descriptor
    uint64_t desc_addr = (uint64_t)desc;
    sg_dma_regs.SG_TAIL_DESC_LOWER = (uint32_t)(desc_addr & 0xFFFFFFFFUL);
    sg_dma_regs.SG_TAIL_DESC_UPPER = (uint32_t)((desc_addr >> 32) & 0xFFFFFFFFUL);

    ///*Memory barrier for weakly ordered systems
    dma_memory_barrier();

    return 0;
}
*/

/*Start SG DMA transfer with descriptor list*/
int firmware_sg_dma_start(sg_descriptor *descriptor_list, uint32_t num_descriptors)
{
    if (descriptor_list == NULL || num_descriptors == 0)
        return DMA_ERR_DESC_PTR;

    /*Validate all descriptors in the list*/
    for (uint32_t i = 0; i < num_descriptors; i++)
    {
        if (validate_sg_descriptor(&descriptor_list[i]) != 0)
            return DMA_ERR_DESC_PTR;

        /*Cache flush for each descriptor's source data*/
        /*Note I am assuming the descriptor list is allocated from coherent memeory region so avoid frequent cache flush*/
        dma_cache_flush((void *)descriptor_list[i].src_addr,
                        descriptor_list[i].transfer_size);
    }

    /*Wait for SG DMA to be idle*/
    int retry = 0;
    while ((firmware_sg_dma_read_status() & 0x03) == DMA_BUSY)
        if (++retry >= BUSY_RETRY_COUNT)
            return DMA_ERR_BUSY_TIMEOUT;

    /*Increment transfer sequence for logging*/
    transfer_sequence++;

    /* Find the allocation for this descriptor list and mark it as active */
    pthread_mutex_lock(&sg_mutex);
    for (int k = 0; k < sg_allocation_count; k++)
    {
        if (sg_allocations[k].start_desc == &descriptor_list[0] &&
            sg_allocations[k].count == num_descriptors &&
            sg_allocations[k].active == 0)
        {
            sg_allocations[k].active = 1;
            break;
        }
    }
    pthread_mutex_unlock(&sg_mutex);

    /*Set current descriptor pointer to first descriptor*/
    uint64_t desc_addr = (uint64_t)&descriptor_list[0];
    sg_dma_regs.SG_CURRENT_DESC_LOWER = (uint32_t)(desc_addr & 0xFFFFFFFFUL);
    sg_dma_regs.SG_CURRENT_DESC_UPPER = (uint32_t)((desc_addr >> 32) & 0xFFFFFFFFUL);

    /*Set tail descriptor pointer to last descriptor*/
    desc_addr = (uint64_t)&descriptor_list[num_descriptors - 1];
    sg_dma_regs.SG_TAIL_DESC_LOWER = (uint32_t)(desc_addr & 0xFFFFFFFFUL);
    sg_dma_regs.SG_TAIL_DESC_UPPER = (uint32_t)((desc_addr >> 32) & 0xFFFFFFFFUL);

    /*Memory barrier*/
    dma_memory_barrier();

    /*Issue start command for SG DMA*/
    sg_dma_regs.SG_CMD_REG = 1;

    /*Log SG DMA start*/
    log_event_t event = {0};
    event.type = LOG_DMA_SUCCESS;
    event.transfer_seq = transfer_sequence;
    event.src = descriptor_list[0].src_addr;
    event.dst = descriptor_list[0].dst_addr;
    event.size = num_descriptors; /*Number of descriptors*/
    event.success_count = success_count;
    event.error_count = error_count;

    fw_log_event(&event);

    return 0;
}

/*Read SG DMA status*/
uint8_t firmware_sg_dma_read_status(void)
{
    return sg_dma_regs.SG_STATUS;
}

int sg_descriptor_pool_init(void)
{
    sg_pool = malloc(sizeof(sg_descriptor) * SG_DESC_SIZE_POOL);
    if (!sg_pool)
        return -1;
    for (int i = 0; i < SG_DESC_SIZE_POOL; i++)
    {
        sg_pool[i].next_descriptor = (uint64_t)&sg_pool[(i + 1) % SG_DESC_SIZE_POOL];
    }
    sg_used_mask = 0;

    /* Initialize mutex with priority inheritance to prevent priority inversion */
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setprotocol(&attr, PTHREAD_PRIO_INHERIT);
    pthread_mutex_init(&sg_mutex, &attr);
    pthread_mutexattr_destroy(&attr);

    return 0;
}

int get_free_sg_descriptor(sg_descriptor **desc, uint32_t num_descriptors)
{
    if (num_descriptors == 0 || num_descriptors > SG_DESC_SIZE_POOL)
        return DMA_ERR_DESC_NOT_AVAILABLE;

    pthread_mutex_lock(&sg_mutex);

    /* Find num_descriptors consecutive free descriptors */
    for (int i = 0; i <= SG_DESC_SIZE_POOL - num_descriptors; i++)
    {
        /* Check if the required number of descriptors are free starting from position i */
        int found = 1;
        for (int j = 0; j < num_descriptors; j++)
        {
            if ((sg_used_mask & (1u << (i + j))) != 0)
            {
                found = 0;
                break;
            }
        }

        if (found)
        {
            /* Mark all descriptors as used */
            for (int j = 0; j < num_descriptors; j++)
            {
                sg_used_mask |= (1u << (i + j));
            }
            *desc = &sg_pool[i];

            /* Add to allocation tracking list for asynchronous mode */
            if (sg_allocation_count < MAX_SG_ALLOCATIONS)
            {
                sg_allocations[sg_allocation_count].start_desc = &sg_pool[i];
                sg_allocations[sg_allocation_count].count = num_descriptors;
                sg_allocations[sg_allocation_count].active = 0; /* Not active yet */
                sg_allocation_count++;
            }

            pthread_mutex_unlock(&sg_mutex);
            return 0;
        }
    }

    pthread_mutex_unlock(&sg_mutex);
    return DMA_ERR_DESC_NOT_AVAILABLE;
}
