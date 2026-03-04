#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include <unistd.h> /* for usleep */
#include "dma.h"

dma_register dma_regs =
{
    .DMA_HW_SRC_REG = 0,
    .DMA_HW_DST_REG = 0,
    .DMA_HW_SIZE_REG = 0,
    .DMA_HW_CMD_REG = 0,
    .DMA_HW_STATUS = DMA_IDLE,
    .DMA_HW_INTERRUPT_STATUS_REG = 0
};

/* helper to run firmware_start_dma in separate thread */
struct thread_args {
    uint32_t src;
    uint32_t dst;
    uint32_t size;
    int result;
};

static void *dma_call_thread(void *arg)
{
    struct thread_args *t = arg;
    t->result = firmware_start_dma(t->src, t->dst, t->size);
    return NULL;
}

/* helpers to simulate hardware behaviour */
static void simulate_success(void)
{
    dma_regs.DMA_HW_STATUS = DMA_DONE;
    dma_interrupt_handler();
}

static void simulate_error(void)
{
    dma_regs.DMA_HW_STATUS = DMA_ERROR;
    dma_interrupt_handler();
}

/* Test helper macros */
#define TEST_ASSERT(condition) assert(condition)
#define TEST_ASSERT_EQUAL(actual, expected) assert((actual) == (expected))
#define DMA_SUCCESS 0

/* Test: Initialize DMA firmware */
void test_dma_fw_init()
{
    dma_fw_init();
    TEST_ASSERT(dma_regs.DMA_HW_STATUS == DMA_IDLE);
}

/* Test: Valid DMA transfer with correct parameters (requires interrupt simulation) */
void test_valid_dma_transfer()
{
    dma_fw_init();

    /* ensure hw is idle before starting */
    dma_regs.DMA_HW_STATUS = DMA_IDLE;

    struct thread_args args = {
        .src = 0x1000,
        .dst = 0x2000,
        .size = 1024,
        .result = -999
    };

    pthread_t t;
    pthread_create(&t, NULL, dma_call_thread, &args);

    /* give thread time to enter firmware_start_dma and block on semaphore */
    usleep(10000);

    /* simulate a successful interrupt */
    simulate_success();

    pthread_join(t, NULL);
    TEST_ASSERT_EQUAL(args.result, DMA_SUCCESS);
}

/* Test: DMA transfer with zero size */
void test_dma_transfer_zero_size()
{
    dma_fw_init();
    int ret = firmware_start_dma(0x1000, 0x2000, 0);
    TEST_ASSERT(ret != DMA_SUCCESS);
}

/* Test: DMA transfer with size exceeding limit */
void test_dma_transfer_size_exceed()
{
    dma_fw_init();
    int ret = firmware_start_dma(0x1000, 0x2000, 90000000);
    TEST_ASSERT(ret != DMA_SUCCESS);
}

/* Test: DMA transfer with misaligned source address */
void test_dma_transfer_misaligned_src()
{
    dma_fw_init();
    int ret = firmware_start_dma(0x1003, 0x2000, 1024);
    TEST_ASSERT(ret != DMA_SUCCESS);
}

/* Test: DMA transfer with misaligned destination address */
void test_dma_transfer_misaligned_dst()
{
    dma_fw_init();
    int ret = firmware_start_dma(0x1000, 0x2003, 1024);
    TEST_ASSERT(ret != DMA_SUCCESS);
}


/* Test: DMA firmware deinitialization */
/*void test_dma_fw_deinit()
{
    dma_fw_init();
    dma_fw_deinit();
    TEST_ASSERT_EQUAL(dma_regs.DMA_HW_STATUS, DMA_IDLE);
}*/

/* Run all tests */
int main()
{
    printf("Running DMA Unit Tests...\n\n");

    test_dma_fw_init();
    printf("✓ test_dma_fw_init passed\n");

    test_valid_dma_transfer();
    printf("✓ test_valid_dma_transfer passed\n");

    test_dma_transfer_zero_size();
    printf("✓ test_dma_transfer_zero_size passed\n");

    test_dma_transfer_size_exceed();
    printf("✓ test_dma_transfer_size_exceed passed\n");

    test_dma_transfer_misaligned_src();
    printf("✓ test_dma_transfer_misaligned_src passed\n");

    test_dma_transfer_misaligned_dst();
    printf("✓ test_dma_transfer_misaligned_dst passed\n");

    /*test_dma_fw_deinit();
    printf("✓ test_dma_fw_deinit passed\n");*/

    printf("\nAll tests passed!\n");
    return 0;
}