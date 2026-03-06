#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include <unistd.h> /* for usleep */
#include "dma.h"

#if DMA_WATCHDOG_ENABLED
extern _Atomic int watchdog_triggered;
void dma_set_watchdog_timeout(int seconds);
#endif

dma_register dma_regs =
    {
        .DMA_HW_SRC_REG_LOWER = 0,
        .DMA_HW_SRC_REG_UPPER = 0,
        .DMA_HW_DST_REG_LOWER = 0,
        .DMA_HW_DST_REG_UPPER = 0,
        .DMA_HW_SIZE_REG = 0,
        .DMA_HW_CMD_REG = 0,
        .DMA_HW_STATUS = DMA_IDLE,
        .DMA_HW_INTERRUPT_STATUS_REG = 0};

/* Scatter-Gather DMA Register instance */
sg_dma_register sg_dma_regs =
    {
        .SG_CURRENT_DESC_LOWER = 0,
        .SG_CURRENT_DESC_UPPER = 0,
        .SG_TAIL_DESC_LOWER = 0,
        .SG_TAIL_DESC_UPPER = 0,
        .SG_CMD_REG = 0,
        .SG_STATUS = DMA_IDLE,
};

/* helper to run firmware_start_dma in separate thread */
struct thread_args
{
    uint64_t src;
    uint64_t dst;
    uint32_t size;
    int result;
};

/* helper to run firmware_start_dma in separate thread */
struct sg_thread_args
{
    struct sg_descriptor *desc;
    uint32_t num_descriptors;
    int result;
};

static void *dma_call_thread(void *arg)
{
    struct thread_args *t = arg;
    t->result = firmware_start_dma(t->src, t->dst, t->size);
    return NULL;
}

static void *sg_dma_call_thread(void *arg)
{
    struct sg_thread_args *t = arg;
    t->result = firmware_sg_dma_start(t->desc, t->num_descriptors);
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

/* Test helper: Reset SG descriptor pool (for unit testing) */
void reset_sg_descriptor_pool(void)
{
    sg_used_mask = 0;
    sg_allocation_count = 0;
}

/* Helper to reset SG descriptor pool for testing */
static void reset_sg_pool()
{
    reset_sg_descriptor_pool();
}

/* Test: Initialize DMA firmware */
void test_dma_fw_init()
{
    dma_fw_init();
    TEST_ASSERT(dma_regs.DMA_HW_STATUS == DMA_IDLE);
}

#if DMA_WATCHDOG_ENABLED
/* Test: Watchdog timeout detection */
void test_watchdog_timeout()
{
    dma_fw_init();
    /* shorten timeout for quicker test */
    dma_set_watchdog_timeout(1);

    /* ensure hardware idle */
    dma_regs.DMA_HW_STATUS = DMA_IDLE;

    watchdog_triggered = 0;

    struct thread_args args = {
        .src = 0x1000,
        .dst = 0x2000,
        .size = 1024,
        .result = -999};

    pthread_t t;
    pthread_create(&t, NULL, dma_call_thread, &args);

    /* wait longer than the 1s timeout */
    usleep(1500000);

    TEST_ASSERT(watchdog_triggered);

    /* now simulate successful completion to unblock thread */
    simulate_success();
    pthread_join(t, NULL);
    TEST_ASSERT_EQUAL(args.result, DMA_SUCCESS);
}
#endif

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
        .result = -999};

    pthread_t t;
    pthread_create(&t, NULL, dma_call_thread, &args);

    /* give thread time to enter firmware_start_dma and block on semaphore */
    usleep(10000);

    /* simulate a successful interrupt */
    simulate_success();

    pthread_join(t, NULL);
    TEST_ASSERT_EQUAL(args.result, DMA_SUCCESS);
    /* ensure hardware idle */
    dma_regs.DMA_HW_STATUS = DMA_IDLE;
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

/* Test: Get free SG descriptors */
void test_get_free_sg_descriptor()
{
    dma_fw_init();
    reset_sg_pool();

    sg_descriptor *desc1;
    int ret1 = get_free_sg_descriptor(&desc1, 3);
    TEST_ASSERT_EQUAL(ret1, DMA_SUCCESS);
    TEST_ASSERT(desc1 != NULL);

    sg_descriptor *desc2;
    int ret2 = get_free_sg_descriptor(&desc2, 2);
    TEST_ASSERT_EQUAL(ret2, DMA_SUCCESS);
    TEST_ASSERT(desc2 != NULL);
    TEST_ASSERT(desc2 != desc1); /* Should be different allocation */

    /* Test zero descriptors request */
    sg_descriptor *desc3;
    int ret3 = get_free_sg_descriptor(&desc3, 0);
    TEST_ASSERT(ret3 != DMA_SUCCESS);

    /* Test requesting more than available */
    sg_descriptor *desc4;
    int ret4 = get_free_sg_descriptor(&desc4, 25); /* More than pool size */
    TEST_ASSERT(ret4 != DMA_SUCCESS);
}

/* Test: SG DMA start with valid descriptors */
void test_sg_dma_start_valid()
{
    dma_fw_init();
    reset_sg_pool();

    /* Allocate descriptors */
    sg_descriptor *desc;
    int ret = get_free_sg_descriptor(&desc, 2);
    TEST_ASSERT_EQUAL(ret, DMA_SUCCESS);

    /* Setup descriptor data */
    desc[0].src_addr = 0x1000;
    desc[0].dst_addr = 0x2000;
    desc[0].transfer_size = 1024;
    desc[0].next_descriptor = (uint64_t)&desc[1];

    desc[1].src_addr = 0x3000;
    desc[1].dst_addr = 0x4000;
    desc[1].transfer_size = 512;
    desc[1].next_descriptor = 0;

    /* Ensure SG DMA is idle */
    sg_dma_regs.SG_STATUS = DMA_IDLE;
    /* Start SG DMA transfer */
    struct sg_thread_args args = {
        .desc = desc,
        .num_descriptors = 2,
        .result = -999};

    pthread_t t;
    pthread_create(&t, NULL, sg_dma_call_thread, &args);

    /* give thread time to enter firmware_start_dma and block on semaphore */
    usleep(1000);

    /* Check that registers were set */
    TEST_ASSERT(sg_dma_regs.SG_CURRENT_DESC_LOWER != 0);
    TEST_ASSERT(sg_dma_regs.SG_TAIL_DESC_LOWER != 0);
    TEST_ASSERT_EQUAL(sg_dma_regs.SG_CMD_REG, 1);
    sg_dma_regs.SG_STATUS = DMA_DONE;
    dma_interrupt_handler();
    pthread_join(t, NULL);
}

/* Test: SG DMA start with invalid parameters */
void test_sg_dma_start_invalid()
{
    dma_fw_init();
    reset_sg_pool();

    /* Test NULL descriptor list */
    int ret1 = firmware_sg_dma_start(NULL, 1);
    TEST_ASSERT(ret1 != DMA_SUCCESS);

    /* Test zero descriptors */
    sg_descriptor desc;
    int ret2 = firmware_sg_dma_start(&desc, 0);
    TEST_ASSERT(ret2 != DMA_SUCCESS);

    /* Test invalid descriptor (zero size) */
    sg_descriptor *desc_ptr;
    get_free_sg_descriptor(&desc_ptr, 1);
    desc_ptr[0].src_addr = 0x1000;
    desc_ptr[0].dst_addr = 0x2000;
    desc_ptr[0].transfer_size = 0; /* Invalid */

    /* Start SG DMA transfer */
    struct sg_thread_args args = {
        .desc = &desc_ptr[0],
        .num_descriptors = 2,
        .result = -999};

    pthread_t t;
    pthread_create(&t, NULL, sg_dma_call_thread, &args);
    usleep(1000);
    TEST_ASSERT(args.result != DMA_SUCCESS);
    pthread_join(t, NULL);
}

/* Test: Wait for completion with SG DMA success */
void test_wait_for_completion_sg_success()
{
    dma_fw_init();
    reset_sg_pool();

    /* Test comprehensive SG DMA flow with multiple iterations */
    const int NUM_ITERATIONS = 2;
    const int NUM_DESCRIPTORS = 20; /* Use 20 descriptors to leave some room */

    for (int iter = 0; iter < NUM_ITERATIONS; iter++)
    {
        printf("  Iteration %d/%d\n", iter + 1, NUM_ITERATIONS);

        /* Allocate descriptors */
        sg_descriptor *desc;
        int alloc_ret = get_free_sg_descriptor(&desc, NUM_DESCRIPTORS);
        TEST_ASSERT_EQUAL(alloc_ret, DMA_SUCCESS);
        TEST_ASSERT(desc != NULL);

        /* Setup descriptor data */
        for (int i = 0; i < NUM_DESCRIPTORS; i++)
        {
            desc[i].src_addr = 0x1000 + (i * 0x1000);
            desc[i].dst_addr = 0x2000 + (i * 0x1000);
            desc[i].transfer_size = 1024;
            desc[i].next_descriptor = (i < NUM_DESCRIPTORS - 1) ? (uint64_t)&desc[i + 1] : 0;
        }

        /* Ensure SG DMA is idle */
        sg_dma_regs.SG_STATUS = DMA_IDLE;

        /* Start SG DMA transfer */
        struct sg_thread_args args = {
            .desc = &desc[0],
            .num_descriptors = NUM_DESCRIPTORS,
            .result = -999};

        pthread_t t;
        pthread_create(&t, NULL, sg_dma_call_thread, &args);
        usleep(10000);

        /* Verify allocation tracking */
        TEST_ASSERT_EQUAL(sg_allocation_count, 1);

        /* Check hardware register setup */
        TEST_ASSERT(sg_dma_regs.SG_CURRENT_DESC_LOWER != 0);
        TEST_ASSERT(sg_dma_regs.SG_TAIL_DESC_LOWER != 0);
        TEST_ASSERT_EQUAL(sg_dma_regs.SG_CMD_REG, 1);

        TEST_ASSERT(sg_allocations[0].active);
        TEST_ASSERT_EQUAL(sg_allocations[0].start_desc, desc);
        TEST_ASSERT_EQUAL(sg_allocations[0].count, NUM_DESCRIPTORS);

        /* Simulate completion by triggering regular DMA that will check SG DMA status too */
        sg_dma_regs.SG_STATUS = DMA_DONE; /* SG DMA completed */

        dma_interrupt_handler();
        usleep(10000); /* allow handler to process */
        TEST_ASSERT_EQUAL(args.result, DMA_SUCCESS);

        /* Verify SG DMA descriptors were freed */
        TEST_ASSERT_EQUAL(sg_allocation_count, 0); /* Should be cleaned up */
        pthread_join(t, NULL);

        /* Verify descriptors can be allocated again */
        sg_descriptor *desc2;
        int alloc_ret2 = get_free_sg_descriptor(&desc2, NUM_DESCRIPTORS);
        TEST_ASSERT_EQUAL(alloc_ret2, DMA_SUCCESS);

        /* Clean up for next iteration */
        reset_sg_pool();
    }
}

/* Test: Wait for completion with SG DMA error */
void test_wait_for_completion_sg_error()
{
    dma_fw_init();
    reset_sg_pool();

    /* Allocate descriptors */
    sg_descriptor *desc;
    int alloc_ret = get_free_sg_descriptor(&desc, 2);
    TEST_ASSERT_EQUAL(alloc_ret, DMA_SUCCESS);

    /* Setup descriptor data */
    desc[0].src_addr = 0x1000;
    desc[0].dst_addr = 0x2000;
    desc[0].transfer_size = 1024;
    desc[0].next_descriptor = (uint64_t)&desc[1];

    desc[1].src_addr = 0x3000;
    desc[1].dst_addr = 0x4000;
    desc[1].transfer_size = 512;
    desc[1].next_descriptor = 0;

    /* Ensure SG DMA is idle */
    sg_dma_regs.SG_STATUS = DMA_IDLE;

    /* Start SG DMA transfer */
    struct sg_thread_args args = {
        .desc = &desc[0],
        .num_descriptors = 2,
        .result = -999};

    pthread_t t;
    pthread_create(&t, NULL, sg_dma_call_thread, &args);
    usleep(10000);

    /* Now trigger an SG error and let wait_for_completion handle it via a normal DMA call */
    sg_dma_regs.SG_STATUS = DMA_ERROR;

    /* fire interrupt to wake up waiter */
    dma_interrupt_handler();
    pthread_join(t, NULL);

    TEST_ASSERT(args.result != DMA_SUCCESS);

    /* Verify error handling - descriptors should be freed */
    int found_allocation = 0;
    for (int k = 0; k < sg_allocation_count; k++)
    {
        if (sg_allocations[k].start_desc == desc &&
            sg_allocations[k].count == 2)
        {
            found_allocation = 1;
            break;
        }
    }
    TEST_ASSERT(!found_allocation);
}

/* Run all tests */
int main()
{
    printf("Running DMA Unit Tests...\n\n");

    test_dma_fw_init();
    printf("✓ test_dma_fw_init passed\n");

#if DMA_WATCHDOG_ENABLED
    test_watchdog_timeout();
    printf("✓ test_watchdog_timeout passed\n");
#endif

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

    test_get_free_sg_descriptor();
    printf("✓ test_get_free_sg_descriptor passed\n");

    test_sg_dma_start_valid();
    printf("✓ test_sg_dma_start_valid passed\n");

    test_sg_dma_start_invalid();
    printf("✓ test_sg_dma_start_invalid passed\n");

    test_wait_for_completion_sg_success();
    printf("✓ test_wait_for_completion_sg_success passed\n");

    test_wait_for_completion_sg_error();
    printf("✓ test_wait_for_completion_sg_error passed\n");

    /*test_dma_fw_deinit();
    printf("✓ test_dma_fw_deinit passed\n");*/

    printf("\nAll tests passed!\n");
    return 0;
}