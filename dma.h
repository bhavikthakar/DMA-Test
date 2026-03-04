#ifndef DMA_H
#define DMA_H

#include <stdint.h>

/* ================= Hardware Registers ================= */
/*Generally DMA/S-G DMA operation is discriptor based but for considering this test using Direct mode*/
/*Note: In actual HW platform register written like this example: DMA_HW_SRC_REG = (DMA_BASE_ADDRESS + offset)*/
/* using pointer to access that address and write values in register */
typedef struct
{
    volatile uint32_t DMA_HW_SRC_REG;
    volatile uint32_t DMA_HW_DST_REG;
    volatile uint32_t DMA_HW_SIZE_REG;
    volatile uint8_t DMA_HW_CMD_REG;
    volatile uint8_t reserved_0[2]; //for padding 32-bit word aligned register spacing
    volatile uint8_t DMA_HW_STATUS;
    volatile uint8_t reserved_1[2];
    volatile uint8_t DMA_HW_INTERRUPT_STATUS_REG;
} dma_register;
/*Global declaring object*/
extern dma_register dma_regs;

/*Scatter-gather DMA mode*/
/*typedef struct 
{
    volatile uint32_t nxt_des;
    volatile uint32_t src_adr;
    volatile uint32_t dest_adr;
    volatile uint32_t control;
    volatile uint32_t status;
} sg_dma;  */



/* DMA States */
#define DMA_IDLE 0x00
#define DMA_BUSY 0x01
#define DMA_DONE 0x02
#define DMA_ERROR 0x03

/* Error Codes */
#define DMA_ERR_BUSY_TIMEOUT -1
#define DMA_ERR_HW_FAILURE -2
#define DMA_ERR_UNKNOWN_STATE -9
#define DMA_ERR_SIZE_ZERO -6
#define DMA_ERR_SRC_ALIGN -7
#define DMA_ERR_DST_ALIGN -8
#define DMA_ERR_SIZE_EXCEEDED -11

/* API */
void dma_fw_init(void);
void dma_fw_deinit(void);

int firmware_start_dma(uint32_t src, uint32_t dst, uint32_t size);

uint8_t firmware_read_status(void);
void dma_interrupt_handler(void);

#endif
