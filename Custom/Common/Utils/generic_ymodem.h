#ifndef GENERIC_YMODEM_H
#define GENERIC_YMODEM_H

#include <stdint.h>
#include <string.h>
#include <stdio.h>


#define YMODEM_RECEIVE_GLOBAL_TIMEOUT_MS 10000  // 10 second global timeout

// Callback status definition
typedef enum {
    YMODEM_SUCCESS,
    YMODEM_CANCEL_RECEIVED,
    YMODEM_SEND_TIMEOUT,
    YMODEM_RECEIVE_TIMEOUT,
    YMODEM_COMPLETE,
    YMODEM_PACKET_ERROR,
    YMODEM_FILE_ERROR
} YmodemStatus;

// UART operation function pointers
typedef void (*UART_TxFunc)(char data);
typedef int (*UART_RxFunc)(char *date);
typedef int (*FileReadFunc)(const char* filename, uint8_t* buffer, uint32_t size);

// YMODEM control structure
typedef struct {
    UART_TxFunc uart_tx;
    UART_RxFunc uart_rx;
    void (*callback)(YmodemStatus);

    // File operation related function pointers
    void* (*file_fopen)(const char *path, const char *mode);
    int   (*file_fclose)(void *fd);
    int   (*file_fwrite)(void *fd, const void *buf, size_t size);
    int   (*file_fread)(void *fd, void *buf, size_t size);
    int   (*file_fseek)(void *fd, long offset, int whence);
    long  (*file_ftell)(void *fd);
    int (*file_fflush)(void*);

    // Status variables
    uint32_t file_size;
    uint32_t bytes_sent;
    uint8_t  packet_num;
    uint8_t  retry_count;
    uint8_t  cancel_count;
    char file_name[128]; // Currently receiving file name
} YmodemHandler;

void Ymodem_Init(
    UART_TxFunc tx_func,
    UART_RxFunc rx_func,
    void* (*file_fopen)(const char*, const char*),
    int   (*file_fclose)(void*),
    int   (*file_fwrite)(void*, const void*, size_t),
    int   (*file_fread)(void*, void*, size_t),
    int   (*file_fseek)(void*, long, int),
    long  (*file_ftell)(void*),
    void (*cb)(YmodemStatus)
);

void Ymodem_Send_File(const char *filename);
void Ymodem_Receive_File(void);
#endif