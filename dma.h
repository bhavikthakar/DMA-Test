#ifndef DMA_H
#define DMA_H

#include <stdint.h>

/* ================= Hardware Registers ================= */
/*Generally DMA/S-G DMA operation is discriptor based but for considering this test using Direct mode*/
/*Note: In actual HW platform register written like this example: DMA_HW_SRC_REG = (DMA_BASE_ADDRESS + offset)*/
/* using pointer to access that address and write values in register */
typedef struct
{
    volatile uint32_t DMA_HW_SRC_REG_LOWER;  /* Source address lower 32 bits */
    volatile uint32_t DMA_HW_SRC_REG_UPPER;  /* Source address upper 32 bits */
    volatile uint32_t DMA_HW_DST_REG_LOWER;  /* Destination address lower 32 bits */
    volatile uint32_t DMA_HW_DST_REG_UPPER;  /* Destination address upper 32 bits */
    volatile uint32_t DMA_HW_SIZE_REG;
    volatile uint8_t DMA_HW_CMD_REG;
    volatile uint8_t reserved_0[2]; //for padding 32-bit dword aligned register spacing
    volatile uint8_t DMA_HW_STATUS;
    volatile uint8_t reserved_1[2];
    volatile uint8_t DMA_HW_INTERRUPT_STATUS_REG;
} dma_register;
/*Global declaring object*/
extern dma_register dma_regs;

/* SG Descriptor structure for scatter-gather transfers */
typedef struct sg_descriptor
{
    volatile uint64_t src_addr;          /* Source address (64-bit) */
    volatile uint64_t dst_addr;          /* Destination address (64-bit) */
    volatile uint32_t transfer_size;     /* Transfer size in bytes */
    volatile uint64_t next_descriptor;   /* Pointer to next descriptor (0 = end of list) */
} sg_descriptor;

/* SG DMA Register structure */
typedef struct
{
    volatile uint32_t SG_CURRENT_DESC_LOWER;   /* Current descriptor pointer lower 32 bits */
    volatile uint32_t SG_CURRENT_DESC_UPPER;   /* Current descriptor pointer upper 32 bits */
    volatile uint32_t SG_TAIL_DESC_LOWER;      /* Tail descriptor pointer lower 32 bits */
    volatile uint32_t SG_TAIL_DESC_UPPER;      /* Tail descriptor pointer upper 32 bits */
    volatile uint8_t SG_CMD_REG;         /* Command register */
    volatile uint8_t reserved_0[2];      /* Padding for alignment */
    volatile uint8_t SG_STATUS;          /* SG DMA status */
    volatile uint8_t reserved_1[2];      /* Padding for alignment */
} sg_dma_register;

/* Global SG DMA register instance */
extern sg_dma_register sg_dma_regs;

/* DMA States */
#define DMA_IDLE 0x00
#define DMA_BUSY 0x01
#define DMA_DONE 0x02
#define DMA_ERROR 0x03

/* Error Codes */
#define DMA_ERR_BUSY_TIMEOUT -1
#define DMA_ERR_HW_FAILURE -2
#define DMA_ERR_DESC_PTR -9
#define DMA_ERR_SIZE_ZERO -6
#define DMA_ERR_SRC_ALIGN -7
#define DMA_ERR_DST_ALIGN -8
#define DMA_ERR_SIZE_EXCEEDED -11
#define DMA_ERR_DESC_NOT_AVAILABLE -12
#define DMA_ERR -10

/* API */
void dma_fw_init(void);
void dma_fw_deinit(void);

int firmware_start_dma(uint64_t src, uint64_t dst, uint32_t size);

uint8_t firmware_read_status(void);
void dma_interrupt_handler(void);

/* Scatter-Gather DMA API */
int firmware_sg_dma_start(sg_descriptor *descriptor_list, uint32_t num_descriptors);
int firmware_sg_dma_queue_descriptor(sg_descriptor *desc);
uint8_t firmware_sg_dma_read_status(void);

int sg_descriptor_pool_init(void);
int get_free_sg_descriptor(sg_descriptor **desc, uint32_t num_descriptors);

/* Test helper function */
void reset_sg_descriptor_pool(void);

/* SG DMA descriptor allocation tracking for asynchronous mode */
typedef struct {
    sg_descriptor *start_desc;
    uint32_t count;
    int active;  /* 1 if transfer started, 0 if just allocated */
} sg_allocation_t;

#define MAX_SG_ALLOCATIONS 10

/* Extern declarations for test access */
extern uint32_t sg_used_mask;
extern int sg_allocation_count;
extern sg_allocation_t sg_allocations[MAX_SG_ALLOCATIONS];

#endif
