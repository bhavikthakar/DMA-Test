#include <stdio.h>
#include <semaphore.h>
#include <stddef.h>
#include <time.h>
#include <string.h>
#include "dma.h"
#include "fw_log.h"

/* define the global DMA register instance referenced by dma.h */
/* ================= Hardware Registers ================= */
/*Generally DMA/S-G DMA operation is discriptor based but for considering this test using Direct mode*/
/*Note: In actual HW platform register written like this example: DMA_HW_SRC_REG = (DMA_BASE_ADDRESS + offset)*/
/* using pointer to access that address and write values in register */
dma_register dma_regs =
{
    .DMA_HW_SRC_REG = 0,
    .DMA_HW_DST_REG = 0,
    .DMA_HW_SIZE_REG = 0,
    .DMA_HW_CMD_REG = 0,
    .DMA_HW_STATUS = DMA_IDLE,
    .DMA_HW_INTERRUPT_STATUS_REG = 0
};

/*To measure DMA transfer latency*/
// #define DMATRANSFER_MEASURE_LATENCY   1

/* DMA basic Configuration that needs for all platform*/
#define DMA_ALIGNMENT 4
#define DMA_MAX_TRANSFER_SIZE (1L << 24)
#define BUSY_RETRY_COUNT 10

static sem_t dma_sem;

/* Transfer Debug variable */
static uint64_t current_src = 0;
static uint64_t current_dst = 0;
static uint32_t current_size = 0;

static uint64_t transfer_sequence = 0;
static uint64_t success_count = 0;
static uint64_t error_count = 0;

#ifdef DMATRANSFER_MEASURE_LATENCY
static struct timespec start_time;
#endif

/*Last Successful data transfer datail*/
static uint64_t last_success_src = 0;
static uint64_t last_success_dst = 0;
static uint32_t last_success_size = 0;
static char last_success_time[64] = {0};

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
static int is_aligned(uint32_t addr)
{
    return ((addr % DMA_ALIGNMENT) == 0);
}

/* This is basically interrupt handler, get called when interrupt arrived*/
void dma_interrupt_handler(void)
{
    dma_regs.DMA_HW_INTERRUPT_STATUS_REG = 1;
    sem_post(&dma_sem);
}

/* Note: This function wait for receing signal from interrupt handle semaphore
   Note: Debug mode doesnt affect to actual performance when its disable*/
static int wait_for_completion(void)
{
    sem_wait(&dma_sem);

    uint8_t state = firmware_read_status() & 0x03;

    log_event_t event = {0};
    if (state == DMA_DONE)
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

        event.type = LOG_DMA_SUCCESS;
        event.transfer_seq = transfer_sequence;
        event.src = current_src;
        event.dst = current_dst;
        event.size = current_size;
        event.success_count = success_count;
        event.error_count = error_count;


        //We can take PTP register value and store in log event on actual platform
        //Generally we dont store all successful transfer but this is just of debugging purpose.
        fw_log_event(&event);
        return 0;
    }

    if (state == DMA_ERROR)
    {
        error_count++;

        event.type = LOG_DMA_ERROR;
        event.transfer_seq = transfer_sequence;
        event.src = current_src;
        event.dst = current_dst;
        event.size = current_size;
        event.success_count = success_count;
        event.error_count = error_count;
        //We can take PTP register value and store in log event on actual platform
        //FW logging works on separate thread and it will not affect to actual performance of DMA transfer, 
        //so we can directly call fw_log_event() here without any performance concern.
        fw_log_event(&event);
        return DMA_ERR_HW_FAILURE;
    }

    return DMA_ERR_UNKNOWN_STATE;
}

/*This is a main DMA start function for DMA API*/
int firmware_start_dma(uint32_t src, uint32_t dst, uint32_t size)
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

    dma_regs.DMA_HW_SRC_REG = src;
    dma_regs.DMA_HW_DST_REG = dst;
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
}

void dma_fw_deinit(void)
{
    fw_log_shutdown();
    sem_destroy(&dma_sem);
}
