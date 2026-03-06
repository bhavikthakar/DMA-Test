#ifndef FW_LOG_H
#define FW_LOG_H

#include <stdint.h>

typedef enum
{
    LOG_DMA_SUCCESS,
    LOG_DMA_ERROR

} log_event_type_t;

typedef struct
{
    log_event_type_t type;

    uint64_t transfer_seq;

    uint64_t src;
    uint64_t dst;
    uint32_t size;

    uint64_t last_success_src;
    uint64_t last_success_dst;
    uint32_t last_success_size;
    char     last_success_time[64];

    uint64_t success_count;
    uint64_t error_count;

} log_event_t;

void fw_log_init(void);
void fw_log_event(log_event_t *event);
void fw_log_async(const char *message);
void fw_log_shutdown(void);

#endif