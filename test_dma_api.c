#include <stdio.h>
#include <stdint.h>
#include <pthread.h>
#include <string.h>
#include "dma.h"

/* ================= Hardware Registers ================= */
/*Generally DMA/S-G DMA operation is discriptor based but for considering this test using Direct mode*/
/*Note: In actual HW platform register written like this example: DMA_HW_SRC_REG = (DMA_BASE_ADDRESS + offset)*/
/* using pointer to access that address and write values in register */
/*Setting register default value for emmulation purpose only*/
dma_register dma_regs =
{
    .DMA_HW_SRC_REG_LOWER = 0,
    .DMA_HW_SRC_REG_UPPER = 0,
    .DMA_HW_DST_REG_LOWER = 0,
    .DMA_HW_DST_REG_UPPER = 0,
    .DMA_HW_SIZE_REG = 0,
    .DMA_HW_CMD_REG = 0,
    .DMA_HW_STATUS = DMA_IDLE,
    .DMA_HW_INTERRUPT_STATUS_REG = 0
};


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


static void reset_dma_emulation()
{
    dma_regs.DMA_HW_SRC_REG_LOWER = 0;
    dma_regs.DMA_HW_SRC_REG_UPPER = 0;
    dma_regs.DMA_HW_DST_REG_LOWER = 0;
    dma_regs.DMA_HW_DST_REG_UPPER = 0;
    dma_regs.DMA_HW_SIZE_REG = 0;
    dma_regs.DMA_HW_CMD_REG = 0;
    dma_regs.DMA_HW_STATUS = DMA_IDLE;
    dma_regs.DMA_HW_INTERRUPT_STATUS_REG = 0;

    sg_dma_regs.SG_CURRENT_DESC_LOWER = 0;
    sg_dma_regs.SG_CURRENT_DESC_UPPER = 0;
    sg_dma_regs.SG_TAIL_DESC_LOWER = 0;
    sg_dma_regs.SG_TAIL_DESC_UPPER = 0;
    sg_dma_regs.SG_CMD_REG = 0;
    sg_dma_regs.SG_STATUS = DMA_IDLE;
}

/* ===================================================== */
/*            for Transfer Thread Function                   */
/* ===================================================== */

void *dma_transfer_thread(void *arg)
{
    printf("Starting DMA transfer in separate thread...\n");

    int ret = firmware_start_dma(0x1000, 0x2000, 1024);

    printf("DMA Thread Completed. Return: %d\n", ret);

    return NULL;
}


void simulate_success()
{
    dma_regs.DMA_HW_STATUS = DMA_DONE;
    dma_interrupt_handler();
}

void simulate_error()
{
    dma_regs.DMA_HW_STATUS = DMA_ERROR;
    dma_interrupt_handler();
}

/* For getting Interrupt manually, firmware_start_dma shoud be running from separat thread */
void test_valid_async_mode()
{
    pthread_t dma_thread;

    pthread_create(&dma_thread, NULL, dma_transfer_thread, NULL);

    printf("\nDMA transfer started.\n");
    printf("Press ENTER to simulate SUCCESS interrupt.\n");
    printf("Type 'e' then press ENTER to simulate ERROR interrupt.\n");

    char input[10];

    fgets(input, sizeof(input), stdin);

    if (input[0] == '\n')
    {
        printf("Simulating SUCCESS interrupt...\n");
        simulate_success();
    }
    else if (input[0] == 'e')
    {
        printf("Simulating ERROR interrupt...\n");
        simulate_error();
    } else {
        printf("Simulating ERROR interrupt...\n");
        simulate_error();
    }

    pthread_join(dma_thread, NULL);
}


void test_size_zero()
{
    printf("Return: %d\n", firmware_start_dma(0x1000, 0x2000, 0));
}

void test_size_exceed()
{
    printf("Return: %d\n", firmware_start_dma(0x1000, 0x2000, 90000000));
}

void test_alignment()
{
    printf("Return: %d\n", firmware_start_dma(0x1003, 0x2000, 1024));
}

void test_error_direct()
{
    pthread_t dma_thread;
    pthread_create(&dma_thread, NULL, dma_transfer_thread, NULL);

    printf("Simulating immediate hardware error...\n");
    simulate_error();

    pthread_join(dma_thread, NULL);
}



int main()
{
    dma_fw_init();

    int choice;

    while (1)
    {
        printf("\n==== DMA TEST MENU ====\n");
        printf("1. Valid Transfer (Manual Interrupt Mode)\n");
        printf("2. Size Zero Test\n");
        printf("3. Size Exceed Test\n");
        printf("4. Alignment Test\n");
        printf("5. Immediate Hardware Error Test\n");
        printf("6. Exit\n");
        printf("Select option: ");

        scanf("%d", &choice);
        getchar(); /* Clear newline from input buffer */

        switch (choice)
        {
        case 1:
            test_valid_async_mode();
            break;

        case 2:
            test_size_zero();
            break;

        case 3:
            test_size_exceed();
            break;

        case 4:
            test_alignment();
            break;

        case 5:
            test_error_direct();
            break;

        case 6:
            dma_fw_deinit();
            return 0;

        default:
            printf("Invalid option\n");
        }
        //If error injected during test then it should do reset
        reset_dma_emulation();
    }
}