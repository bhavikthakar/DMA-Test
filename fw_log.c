#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <semaphore.h>
#include "fw_log.h"

#define LOG_QUEUE_SIZE 32

typedef struct {
    int type; // 0 for event, 1 for string
    union {
        log_event_t event;
        char string[4096];
    } data;
} log_item_t;

static FILE *log_file;
static pthread_t log_thread;
static sem_t log_sem;
static pthread_mutex_t log_mutex;

static log_item_t log_queue[LOG_QUEUE_SIZE];
static int head = 0;
static int tail = 0;
static int running = 1;

/*We need a time stemp for debugging*/
static void format_timestamp(char *buffer, size_t len)
{
    struct timespec ts;
    struct tm tm_info;

    clock_gettime(CLOCK_REALTIME, &ts);
    // This code is CPU consuming but in embedded platform, we can take PTP register value.
    localtime_r(&ts.tv_sec, &tm_info);

    strftime(buffer, len, "%Y-%m-%d %H:%M:%S", &tm_info);

    size_t used = strlen(buffer);
    snprintf(buffer + used, len - used,
             ".%06ld",
             ts.tv_nsec / 1000);
}

static void *log_worker(void *arg)
{
    while (running)
    {
        sem_wait(&log_sem);

        pthread_mutex_lock(&log_mutex);

        if (head == tail)
        {
            pthread_mutex_unlock(&log_mutex);
            continue;
        }
        /*Logging generally use circular buffer so need to know tail and head*/
        log_item_t item = log_queue[tail];
        tail = (tail + 1) % LOG_QUEUE_SIZE;

        pthread_mutex_unlock(&log_mutex);

        if (item.type == 0) {
            log_event_t event = item.data.event;

            char time_str[64];
            format_timestamp(time_str, sizeof(time_str));

            if (event.type == LOG_DMA_SUCCESS)
            {
                fprintf(log_file,
                        "[%s] DMA SUCCESS\n"
                        "Transfer Sequence : %llu\n"
                        "SRC  : 0x%016llX\n"
                        "DST  : 0x%016llX\n"
                        "SIZE : %u bytes\n\n",
                        time_str,
                        (unsigned long long)event.transfer_seq,
                        (unsigned long long)event.src,
                        (unsigned long long)event.dst,
                        event.size);
            }
            else if (event.type == LOG_DMA_ERROR)
            {
                fprintf(log_file,
                        "\n==================================================\n"
                        "[%s] DMA ERROR OCCURRED\n\n"

                        "FAILED TRANSFER DETAILS:\n"
                        "Transfer Sequence   : %llu\n"
                        "Source Address      : 0x%016llX\n"
                        "Destination Address : 0x%016llX\n"
                        "Transfer Size       : %u bytes\n\n"

                        "Total Successful Transfers : %llu\n"
                        "Total Error Count          : %llu\n"
                        "==================================================\n\n",
                        time_str,
                        (unsigned long long)event.transfer_seq,
                        (unsigned long long)event.src,
                        (unsigned long long)event.dst,
                        event.size,
                        (unsigned long long)event.success_count,
                        (unsigned long long)event.error_count);
            }

            fflush(log_file);
        } else {
            fprintf(log_file, "%s\n", item.data.string);
            fflush(log_file);
        }
    }

    return NULL;
}

void fw_log_init(void)
{
    log_file = fopen("dma_fw.log", "a");

    sem_init(&log_sem, 0, 0);
    pthread_mutex_init(&log_mutex, NULL);

    pthread_create(&log_thread, NULL, log_worker, NULL);
}

void fw_log_event(log_event_t *event)
{
    /* Multiple thread may access log event so synchronisation needed*/
    pthread_mutex_lock(&log_mutex);

    log_queue[head].type = 0;
    log_queue[head].data.event = *event;
    head = (head + 1) % LOG_QUEUE_SIZE;

    pthread_mutex_unlock(&log_mutex);

    sem_post(&log_sem);
}

void fw_log_async(const char *message)
{
    pthread_mutex_lock(&log_mutex);

    log_queue[head].type = 1;
    strncpy(log_queue[head].data.string, message, sizeof(log_queue[head].data.string) - 1);
    log_queue[head].data.string[sizeof(log_queue[head].data.string) - 1] = '\0';
    head = (head + 1) % LOG_QUEUE_SIZE;

    pthread_mutex_unlock(&log_mutex);

    sem_post(&log_sem);
}

void fw_log_shutdown(void)
{
    running = 0;
    sem_post(&log_sem);

    pthread_join(log_thread, NULL);

    fclose(log_file);
    pthread_mutex_destroy(&log_mutex);
    sem_destroy(&log_sem);
}