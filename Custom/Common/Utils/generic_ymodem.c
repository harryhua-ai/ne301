#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "generic_ymodem.h"
#include "main.h"
#include "debug.h"

// =================== Log macro definition ===================
#define YMODEM_LOG_ENABLE   0  // 1=enable log, 0=disable log

#if YMODEM_LOG_ENABLE
    #define YMODEM_LOG(fmt, ...)  LOG_DRV_ERROR("[YMODEM] " fmt "\r\n", ##__VA_ARGS__)
#else
    #define YMODEM_LOG(fmt, ...)  do{}while(0)
#endif

// =================== Global instance ===================
static YmodemHandler ymodem;

// =================== Initialize YMODEM handler ===================
void Ymodem_Init(
    UART_TxFunc tx_func,
    int   (*rx_func)(char *), 
    void* (*file_fopen)(const char*, const char*),
    int   (*file_fclose)(void*),
    int   (*file_fwrite)(void*, const void*, size_t),
    int   (*file_fread)(void*, void*, size_t),
    int   (*file_fseek)(void*, long, int),
    long  (*file_ftell)(void*),
    void (*cb)(YmodemStatus)
)
{
    ymodem.uart_tx     = tx_func;
    ymodem.uart_rx     = rx_func;
    ymodem.file_fopen  = file_fopen;
    ymodem.file_fclose = file_fclose;
    ymodem.file_fwrite = file_fwrite;
    ymodem.file_fread  = file_fread;
    ymodem.file_fseek  = file_fseek;
    ymodem.file_ftell  = file_ftell;
    ymodem.callback    = cb;

    YMODEM_LOG("Ymodem initialized.");
}

// =================== Calculate 16-bit CRC ===================
static uint16_t Ymodem_CRC16(const uint8_t *data, uint16_t length) 
{
    if (length == 0) return 0; // Handle empty data case
    
    uint16_t crc = 0;
    while (length--) {
        crc = crc ^ ((uint16_t)*data++ << 8);
        for (uint8_t i = 0; i < 8; i++) {
            crc = crc & 0x8000 ? (crc << 1) ^ 0x1021 : crc << 1;
        }
    }
    return crc;
}

// =================== Helper function: wait for ACK ===================
static int Wait_For_ACK(uint32_t timeout)
{
    uint32_t start_time = HAL_GetTick();
    char c;
    while ((HAL_GetTick() - start_time) < timeout) {
        if (ymodem.uart_rx(&c) == 0) {
            if (c == 0x06) {  // ACK
                return 1;
            } else if (c == 0x15) {  // NAK
                YMODEM_LOG("Received NAK");
                return 0;
            } else {
                YMODEM_LOG("Unexpected response: 0x%02X", c);
            }
        }
    }
    YMODEM_LOG("Wait_For_ACK timeout");
    return 0; // Timeout
}

// =================== Send single data packet ===================
static void Send_Packet(uint8_t *data, uint16_t size, uint8_t seq) 
{
    YMODEM_LOG("Send packet: seq=%d, size=%d", seq, size);

    // Packet header: SOH/STX + sequence number + complement sequence number
    uint8_t header_type = (size <= 128) ? 0x01 : 0x02; // SOH or STX
    ymodem.uart_tx(header_type);
    ymodem.uart_tx(seq);
    ymodem.uart_tx(0xFF - seq);
    
    // Data area
    for (uint16_t i = 0; i < size; i++) {
        ymodem.uart_tx(data[i]);
    }
    
    // Fill remaining data area (0x1A)
    uint16_t total_size = (header_type == 0x01) ? 128 : 1024;
    for (uint16_t i = size; i < total_size; i++) {
        ymodem.uart_tx(0x1A);
    }
    
    // CRC16 (high byte first)
    uint16_t crc = Ymodem_CRC16(data, size);
    ymodem.uart_tx((crc >> 8) & 0xFF);
    ymodem.uart_tx(crc & 0xFF);
}

static int Receive_Packet(uint8_t *buffer, uint16_t max_size, uint32_t timeout) 
{
    uint32_t start_time = HAL_GetTick();
    uint16_t idx = 0;
    char c;
    uint16_t expected_size = 0;

    while (1) {
        // Timeout check
        if ((HAL_GetTick() - start_time) > timeout) {
            YMODEM_LOG("Receive packet timeout.");
            return -3;
        }
        
        if (ymodem.uart_rx(&c) != 0) continue; // No data

        // Packet header synchronization
        if (idx == 0) {
            if (c == 0x01) {         // SOH
                expected_size = 133;
            } else if (c == 0x02) {  // STX
                expected_size = 1029;
            } else if (c == 0x04) {  // EOT
                buffer[0] = 0x04;
                return 0;
            } else {
                continue; // Discard invalid bytes
            }
            buffer[idx++] = c;
            continue;
        }

        buffer[idx++] = c;

        // Check sequence number validity (positions 2 and 3)
        if (idx == 3) {
            uint8_t seq = buffer[1];
            uint8_t seq_comp = buffer[2];
            if ((seq + seq_comp) != 0xFF) {
                YMODEM_LOG("Packet seq error: seq=%d, cseq=%d", seq, seq_comp);
                idx = 0; // Resynchronize
                continue;
            }
        }

        // Complete packet received
        if (idx == expected_size) {
            // Verify CRC
            uint8_t header_type = buffer[0];
            uint16_t data_size = (header_type == 0x01) ? 128 : 1024;
            uint16_t recv_crc = (buffer[idx-2] << 8) | buffer[idx-1];
            uint16_t calc_crc = Ymodem_CRC16(&buffer[3], data_size);
            
            if (recv_crc != calc_crc) {
                YMODEM_LOG("CRC error: recv=0x%04X, calc=0x%04X", recv_crc, calc_crc);
                idx = 0; // Resynchronize
                continue;
            }
            return 0;
        }
    }
}


// =================== Send file ===================
void Ymodem_Send_File(const char *filename)
{
    YMODEM_LOG("Send file: %s", filename);

    ymodem.packet_num = 0;
    ymodem.bytes_sent = 0;
    ymodem.retry_count = 0;
    ymodem.cancel_count = 0;

    // Open file
    void *fd = ymodem.file_fopen(filename, "rb");
    if (!fd) {
        YMODEM_LOG("File open failed: %s", filename);
        ymodem.callback(YMODEM_FILE_ERROR);
        return;
    }
    
    // Get file size
    ymodem.file_fseek(fd, 0, SEEK_END);
    ymodem.file_size = ymodem.file_ftell(fd);
    ymodem.file_fseek(fd, 0, SEEK_SET);

    YMODEM_LOG("File size: %lu", (unsigned long)ymodem.file_size);

    // 1. More robust handshake mechanism
    YMODEM_LOG("Waiting for receiver 'C'...");
    int got_c = 0;
    uint32_t start_time = HAL_GetTick();
    while ((HAL_GetTick() - start_time) < 15000) { // 15 second timeout
        char c;
        if (ymodem.uart_rx(&c) == 0) {
            if (c == 'C') {
                YMODEM_LOG("Received 'C' from receiver.");
                got_c = 1;
                break;
            }
            // Also support rz-style handshake
            else if (c == '\r' || c == '\n') {
                YMODEM_LOG("Received line ending, sending 'C' trigger");
                ymodem.uart_tx('C');
            }
        } else {
            // Actively send 'C' every 3 seconds to trigger receiver
            static uint32_t last_c_sent = 0;
            if ((HAL_GetTick() - last_c_sent) > 3000) {
                ymodem.uart_tx('C');
                last_c_sent = HAL_GetTick();
                YMODEM_LOG("Sent 'C' to trigger receiver");
            }
        }
    }
    
    if (!got_c) {
        YMODEM_LOG("Timeout waiting for receiver 'C'");
        ymodem.file_fclose(fd);
        ymodem.callback(YMODEM_SEND_TIMEOUT);
        return;
    }

    // 2. Send file header packet (format optimized)
    uint8_t header[128] = {0};
    // File name (no more than 99 characters)
    size_t name_len = strlen(filename);
    if (name_len > 99) name_len = 99;
    memcpy(header, filename, name_len);
    
    // File size (formatted as string)
    char size_str[20];
    snprintf(size_str, sizeof(size_str), "%lu", (unsigned long)ymodem.file_size);
    // File name followed by 0, then file size
    header[name_len] = '\0';
    strncpy((char*)header + name_len + 1, size_str, sizeof(header) - name_len - 2);
    
    YMODEM_LOG("Sending header: name=%s, size=%s", filename, size_str);
    
    // 3. Send header packet (with retry support)
    int header_retry = 0;
    while (header_retry < 5) {
        Send_Packet(header, 128, 0);
        
        // Wait for ACK
        if (Wait_For_ACK(2000)) { // 2 second timeout
            YMODEM_LOG("Header ACK received");
            break;
        } else {
            header_retry++;
            YMODEM_LOG("Header ACK timeout, retry %d", header_retry);
        }
    }
    
    if (header_retry >= 5) {
        YMODEM_LOG("Header ACK failed after 5 retries");
        ymodem.file_fclose(fd);
        ymodem.callback(YMODEM_PACKET_ERROR);
        return;
    }

    start_time = HAL_GetTick();
    char c = 0;
    while ((HAL_GetTick() - start_time) < 5000) {
        if (ymodem.uart_rx(&c) == 0 && c == 'C') {
            YMODEM_LOG("Received 'C' after header ACK.");
            break;
        }
    }
    if (c != 'C') {
        YMODEM_LOG("Timeout waiting for 'C' after header ACK.");
        ymodem.file_fclose(fd);
        ymodem.callback(YMODEM_SEND_TIMEOUT);
        return;
    }

    // 4. Send data packets
    uint8_t data[1024];
    ymodem.packet_num = 1;
    ymodem.bytes_sent = 0;
    int last_packet_sent = 0; // Mark whether last packet has been sent

    while (ymodem.bytes_sent < ymodem.file_size || !last_packet_sent) {
        long current_pos = ymodem.file_ftell(fd);
        
        // Dynamically determine packet size: prefer 1024-byte packets
        int to_read = 1024;
        if (ymodem.file_size - ymodem.bytes_sent < 1024) {
            to_read = ymodem.file_size - ymodem.bytes_sent;
        }
        
        // If it's the last packet, mark as last packet
        int is_last_packet = (ymodem.bytes_sent + to_read) >= ymodem.file_size;
        
        int read = ymodem.file_fread(fd, data, to_read);
        if (read <= 0) {
            YMODEM_LOG("File read error at %lu bytes.", ymodem.bytes_sent);
            break;
        }

        // Send data packet (always use 1024-byte format)
        Send_Packet(data, is_last_packet ? 1024 : read, ymodem.packet_num);

        // Wait for ACK (increase timeout)
        if (Wait_For_ACK(3000)) { // 3 second timeout
            ymodem.bytes_sent += read;
            YMODEM_LOG("Data packet %d sent, %lu/%lu bytes.", 
                      ymodem.packet_num, ymodem.bytes_sent, ymodem.file_size);
            
            // Mark last packet as sent
            if (is_last_packet) {
                last_packet_sent = 1;
                YMODEM_LOG("Last packet sent");
            }
            
            ymodem.packet_num++;
            ymodem.retry_count = 0;
        } else {
            // Special handling: if 'C' is received, receiver state may be reset
            char c;
            if (ymodem.uart_rx(&c) == 0 && c == 'C') {
                YMODEM_LOG("Received 'C' during data transfer, resetting state");
                ymodem.file_fseek(fd, current_pos, SEEK_SET);
                ymodem.retry_count = 0;
                continue;
            }
            
            // Rewind file pointer on retransmission
            ymodem.file_fseek(fd, current_pos, SEEK_SET);
            
            YMODEM_LOG("Data packet %d send failed, retry=%d.", 
                      ymodem.packet_num, ymodem.retry_count + 1);
                      
            if (++ymodem.retry_count >= 5) {
                YMODEM_LOG("Send packet retry limit reached.");
                ymodem.file_fclose(fd);
                ymodem.callback(YMODEM_SEND_TIMEOUT);
                return;
            }
        }
    }

    // 5. Send EOT and wait for ACK
    YMODEM_LOG("Sending EOT...");
    ymodem.uart_tx(0x04); // EOT
    if (!Wait_For_ACK(3000)) {
        YMODEM_LOG("EOT not acknowledged, sending again");
        ymodem.uart_tx(0x04); // Send EOT again
        Wait_For_ACK(1000);
    }

    // 6. Send end packet (empty file header)
    uint8_t end_packet[128] = {0}; // All zeros indicate end
    Send_Packet(end_packet, 128, 0);
    if (!Wait_For_ACK(1000)) {
        YMODEM_LOG("End packet not acknowledged");
    }

    // 7. Cleanup resources
    ymodem.file_fclose(fd);
    YMODEM_LOG("Send complete.");
    ymodem.callback(YMODEM_COMPLETE);
}


// =================== Receive file ===================
// void Ymodem_Receive_File(void)
// {
//     YMODEM_LOG("Start receiving file...");

//     uint8_t buffer[1029];  // Maximum packet size + header
//     ymodem.cancel_count = 0;
//     void *fd = NULL;
//     int timeout_count = 0;
//     uint32_t received_bytes = 0;
//     int expecting_header = 1; // Initially expect file header
//     int transfer_active = 1;  // Transfer active flag
//     uint8_t expected_seq = 1; // Expected packet sequence (1 after header packet)
//     int waiting_end_packet = 0; // Flag: waiting for end packet (empty header packet)
//     int packet_errors = 0;      // Consecutive packet error counter

//     // 1. Send 'C' to start transfer (with retry)
//     int c_retry = 0;
//     while (c_retry < 10) {
//         ymodem.uart_tx('C');
//         YMODEM_LOG("Sent 'C' to start transfer (attempt %d)", c_retry + 1);

//         int result = Receive_Packet(buffer, sizeof(buffer), 1500);
//         if (result == 0) {
//             YMODEM_LOG("Received first packet");
//             break;
//         } else if (result == -3) {
//             c_retry++;
//         } else {
//             YMODEM_LOG("Error receiving first packet: %d", result);
//             c_retry++;
//         }
        
//         // Increase delay every two retries
//         if (c_retry % 2 == 0) {
//             osDelay(100);
//         }
//     }
    
//     if (c_retry >= 10) {
//         YMODEM_LOG("No response after 10 'C' attempts");
//         ymodem.callback(YMODEM_RECEIVE_TIMEOUT);
//         return;
//     }

//     // Main receive loop
//     while (transfer_active) {
//         uint8_t pkt_type = buffer[0];
//         uint8_t seq = buffer[1];
//         uint8_t seq_comp = buffer[2];

//         // Check user cancellation
//         if (ymodem.cancel_count >= 5) {
//             YMODEM_LOG("Transfer canceled by user");
//             if (fd) ymodem.file_fclose(fd);
//             ymodem.callback(YMODEM_CANCEL_RECEIVED);
//             return;
//         }

//         // Handle EOT (transfer end)
//         if (pkt_type == 0x04) {
//             YMODEM_LOG("Received EOT");
//             ymodem.uart_tx(0x06); // ACK
//             waiting_end_packet = 1; // Next packet should be end packet (empty header)
//             expecting_header = 1;
//             goto receive_next_packet;
//         }

//         // Verify sequence number (non-EOT packets)
//         if (pkt_type != 0x04) {
//             if ((seq + seq_comp) != 0xFF) {
//                 YMODEM_LOG("Invalid sequence: %d + %d != 255", seq, seq_comp);
//                 ymodem.uart_tx(0x15); // NAK
//                 timeout_count++;
//                 if (timeout_count >= 10) {
//                     YMODEM_LOG("Too many sequence errors, aborting");
//                     if (fd) ymodem.file_fclose(fd);
//                     ymodem.callback(YMODEM_PACKET_ERROR);
//                     return;
//                 }
//                 goto receive_next_packet;
//             }
//         }

//         // Header packet (only valid when expecting_header==1)
//         if (seq == 0 && expecting_header) {
//             char* name = (char*)&buffer[3];
//             char* size_str = name;
//             int name_len = 0;
//             while (*size_str != '\0' && (size_str - (char*)buffer) < 128) {
//                 size_str++;
//                 name_len++;
//             }
//             if ((size_str - (char*)buffer) >= 128) {
//                 YMODEM_LOG("Header parse error: no size string");
//                 ymodem.uart_tx(0x15);
//                 goto receive_next_packet;
//             }
//             size_str++; // Skip '\0'

//             // Check boundary
//             if (size_str >= (char*)buffer + sizeof(buffer)) {
//                 YMODEM_LOG("Invalid header format");
//                 ymodem.uart_tx(0x15); // NAK
//                 goto receive_next_packet;
//             }

//             // Parse file size
//             unsigned long size = 0;
//             if (sscanf(size_str, "%lu", &size) != 1) {
//                 YMODEM_LOG("Failed to parse file size: %s", size_str);
//                 size = 0; // Set to 0, indicating size unknown
//             }

//             // End packet: header packet (seq==0) and file name length is 0
//             if (buffer[3] == 0) {
//                 YMODEM_LOG("Received end packet");
//                 ymodem.uart_tx(0x06); // ACK
//                 if (fd) {
//                     ymodem.file_fclose(fd);
//                     fd = NULL;
//                 }
//                 transfer_active = 0; // Terminate transfer loop
//                 ymodem.callback(YMODEM_COMPLETE);
//                 continue; // Exit loop
//             }

//             expected_seq = 1;
//             timeout_count = 0;
//             packet_errors = 0;

//             YMODEM_LOG("Receiving file: %s, size=%lu", name, size);

//             // Close previous file (if any)
//             if (fd) {
//                 ymodem.file_fclose(fd);
//                 fd = NULL;
//             }

//             // Open new file
//             fd = ymodem.file_fopen(name, "wb");
//             if (!fd) {
//                 YMODEM_LOG("File create failed: %s", name);
//                 ymodem.uart_tx(0x15); // NAK
//                 ymodem.callback(YMODEM_FILE_ERROR);
//                 return;
//             }

//             ymodem.file_size = size;
//             strncpy(ymodem.file_name, name, sizeof(ymodem.file_name) - 1); // Save current file name
//             ymodem.file_name[sizeof(ymodem.file_name) - 1] = '\0';
//             received_bytes = 0;
//             expecting_header = 0;
//             waiting_end_packet = 0;
//             ymodem.uart_tx(0x06); // ACK
//             goto receive_next_packet;
//         }

//         // Data packet (as long as not in header state, treat as data packet)
//         if (!expecting_header && !waiting_end_packet) {
//             // Check packet sequence: allow duplicate packets (retransmission) or next packet
//             if (seq == expected_seq || seq == (uint8_t)(expected_seq - 1)) {
//                 int data_size = (pkt_type == 0x01) ? 128 : 1024;
//                 int data_offset = 3; // Data part starts at index 3
//                 int write_size = data_size;

//                 // If it's a new packet (not retransmission) and hasn't exceeded file size
//                 if (seq == expected_seq) {
//                     // File end handling
//                     if (ymodem.file_size > 0 && (received_bytes + data_size) > ymodem.file_size) {
//                         write_size = ymodem.file_size - received_bytes;
//                     }

//                     // Write to file
//                     if (write_size > 0) {
//                         int written = ymodem.file_fwrite(fd, &buffer[data_offset], write_size);
//                         if (written != write_size) {
//                             YMODEM_LOG("Write error: %d/%d bytes written", written, write_size);
//                             ymodem.uart_tx(0x15); // NAK
//                             goto receive_next_packet;
//                         }
//                         received_bytes += write_size;
//                     }

//                     YMODEM_LOG("Received %d/%d bytes (total %lu/%lu)", 
//                             write_size, data_size, received_bytes, ymodem.file_size);
                    
//                     // Update expected sequence
//                     expected_seq++;
//                 }

//                 // Send ACK (whether new packet or retransmitted packet)
//                 ymodem.uart_tx(0x06); // ACK
//                 packet_errors = 0; // Reset error counter

//                 // Check if file reception is complete
//                 if (ymodem.file_size > 0 && received_bytes >= ymodem.file_size) {
//                     expecting_header = 1; // Wait for next header packet (may be new file or end packet)
//                     YMODEM_LOG("File transfer complete, waiting for EOT");
//                 }
//             } else {
//                 // Packet sequence mismatch
//                 YMODEM_LOG("Sequence error: expected %d or %d, got %d", 
//                         expected_seq-1, expected_seq, seq);
//                 ymodem.uart_tx(0x15); // NAK
//                 packet_errors++;
//             }
//         } else {
//             // Unexpected data packet (e.g., received data packet while waiting for header)
//             YMODEM_LOG("Unexpected packet type: state=%s", 
//                     waiting_end_packet ? "waiting_end" : (expecting_header ? "expecting_header" : "data"));
//             ymodem.uart_tx(0x15); // NAK
//             packet_errors++;
//         }

//         // Check consecutive errors
//         if (packet_errors >= 10) {
//             YMODEM_LOG("Too many consecutive errors (%d), aborting", packet_errors);
//             if (fd) ymodem.file_fclose(fd);
//             ymodem.callback(YMODEM_PACKET_ERROR);
//             return;
//         }

//     receive_next_packet:
//         // Receive next packet
//         int result = Receive_Packet(buffer, sizeof(buffer), 3000);

//         if (result == 0) {
//             timeout_count = 0; // Reset timeout counter (reset on each successful reception)
//             continue; // Successfully received, continue processing
//         } else if (result == -2) { // User cancellation
//             YMODEM_LOG("User canceled transfer");
//             if (fd) ymodem.file_fclose(fd);
//             ymodem.callback(YMODEM_CANCEL_RECEIVED);
//             return;
//         } else if (result == -3) { // Timeout
//             timeout_count++;
//             YMODEM_LOG("Receive timeout, count=%d", timeout_count);

//             if (timeout_count >= 5) {
//                 YMODEM_LOG("Receive timeout limit reached");
//                 if (fd) ymodem.file_fclose(fd);
//                 ymodem.callback(YMODEM_RECEIVE_TIMEOUT);
//                 return;
//             }

//             // Smart retransmission request
//             if (expecting_header || waiting_end_packet) {
//                 ymodem.uart_tx('C'); // Request header packet or end packet
//                 YMODEM_LOG("Resent 'C' due to timeout");
//             } else {
//                 ymodem.uart_tx(0x15); // NAK, request retransmission of current packet
//                 YMODEM_LOG("Resent NAK due to timeout");
//             }
//         } else {
//             // Packet error (e.g., CRC error)
//             YMODEM_LOG("Packet error: %d", result);
//             ymodem.uart_tx(0x15); // NAK
//             packet_errors++;
//         }
//     }
// }

void Ymodem_Receive_File(void)
{
    YMODEM_LOG("Start receiving file...");

    uint8_t buffer[1029];  // Maximum packet size + header
    ymodem.cancel_count = 0;
    void *fd = NULL;
    int timeout_count = 0;
    uint32_t received_bytes = 0;
    int expecting_header = 1; // Initially expect file header
    int transfer_active = 1;  // Transfer active flag
    uint8_t expected_seq = 1; // Expected packet sequence (1 after header packet)
    int waiting_end_packet = 0; // Flag: waiting for end packet (empty header packet)
    int packet_errors = 0;      // Consecutive packet error counter

    #define YMODEM_FILE_REOPEN_INTERVAL 32
    int write_counter = 0; // Write counter

    // 1. Send 'C' to start transfer (with retry)
    int c_retry = 0;
    while (c_retry < 10) {
        ymodem.uart_tx('C');
        YMODEM_LOG("Sent 'C' to start transfer (attempt %d)", c_retry + 1);

        int result = Receive_Packet(buffer, sizeof(buffer), 1500);
        if (result == 0) {
            YMODEM_LOG("Received first packet");
            break;
        } else if (result == -3) {
            c_retry++;
        } else {
            YMODEM_LOG("Error receiving first packet: %d", result);
            c_retry++;
        }
        if (c_retry % 2 == 0) {
            osDelay(100);
        }
    }

    if (c_retry >= 10) {
        YMODEM_LOG("No response after 10 'C' attempts");
        ymodem.callback(YMODEM_RECEIVE_TIMEOUT);
        return;
    }

    // Main receive loop
    while (transfer_active) {
        uint8_t pkt_type = buffer[0];
        uint8_t seq = buffer[1];
        uint8_t seq_comp = buffer[2];

        // Check for user cancel
        if (ymodem.cancel_count >= 5) {
            YMODEM_LOG("Transfer canceled by user");
            if (fd) ymodem.file_fclose(fd);
            ymodem.callback(YMODEM_CANCEL_RECEIVED);
            return;
        }

        // Handle EOT (End Of Transfer)
        if (pkt_type == 0x04) {
            YMODEM_LOG("Received EOT");
            ymodem.uart_tx(0x06); // ACK
            waiting_end_packet = 1; // Next packet should be end packet (empty header packet)
            expecting_header = 1;
            goto receive_next_packet;
        }

        // Verify sequence number (non-EOT packet)
        if (pkt_type != 0x04) {
            if ((seq + seq_comp) != 0xFF) {
                YMODEM_LOG("Invalid sequence: %d + %d != 255", seq, seq_comp);
                ymodem.uart_tx(0x15); // NAK
                timeout_count++;
                if (timeout_count >= 10) {
                    YMODEM_LOG("Too many sequence errors, aborting");
                    if (fd) ymodem.file_fclose(fd);
                    ymodem.callback(YMODEM_PACKET_ERROR);
                    return;
                }
                goto receive_next_packet;
            }
        }

        // Header packet (only valid when expecting_header==1)
        if (seq == 0 && expecting_header) {
            char* name = (char*)&buffer[3];
            char* size_str = name;
            int name_len = 0;
            while (*size_str != '\0' && (size_str - (char*)buffer) < 128) {
                size_str++;
                name_len++;
            }
            if ((size_str - (char*)buffer) >= 128) {
                YMODEM_LOG("Header parse error: no size string");
                ymodem.uart_tx(0x15);
                goto receive_next_packet;
            }
            size_str++; // Skip '\0'

            // Check boundary
            if (size_str >= (char*)buffer + sizeof(buffer)) {
                YMODEM_LOG("Invalid header format");
                ymodem.uart_tx(0x15); // NAK
                goto receive_next_packet;
            }

            // Parse file size
            unsigned long size = 0;
            if (sscanf(size_str, "%lu", &size) != 1) {
                YMODEM_LOG("Failed to parse file size: %s", size_str);
                size = 0; // Set to 0 to indicate unknown size
            }

            // End packet: header packet (seq==0) and filename length is 0
            if (buffer[3] == 0) {
                YMODEM_LOG("Received end packet");
                ymodem.uart_tx(0x06); // ACK
                if (fd) {
                    ymodem.file_fclose(fd);
                    fd = NULL;
                }
                transfer_active = 0; // Terminate transfer loop
                ymodem.callback(YMODEM_COMPLETE);
                continue; // Exit loop
            }

            expected_seq = 1;
            timeout_count = 0;
            packet_errors = 0;

            YMODEM_LOG("Receiving file: %s, size=%lu", name, size);

            // Close previous file (if any)
            if (fd) {
                ymodem.file_fclose(fd);
                fd = NULL;
            }

            // Open new file
            fd = ymodem.file_fopen(name, "wb");
            if (!fd) {
                YMODEM_LOG("File create failed: %s", name);
                ymodem.uart_tx(0x15); // NAK
                ymodem.callback(YMODEM_FILE_ERROR);
                return;
            }

            ymodem.file_size = size;
            strncpy(ymodem.file_name, name, sizeof(ymodem.file_name) - 1); // Save current filename
            ymodem.file_name[sizeof(ymodem.file_name) - 1] = '\0';
            received_bytes = 0;
            write_counter = 0; // Reset counter for new file
            expecting_header = 0;
            waiting_end_packet = 0;
            ymodem.uart_tx(0x06); // ACK
            goto receive_next_packet;
        }

        // Data packet (treat as data packet as long as not in header state)
        if (!expecting_header && !waiting_end_packet) {
            // Check packet sequence: allow duplicate packet (retransmission) or next packet
            if (seq == expected_seq || seq == (uint8_t)(expected_seq - 1)) {
                int data_size = (pkt_type == 0x01) ? 128 : 1024;
                int data_offset = 3; // Data portion starts from index 3
                int write_size = data_size;

                // If new packet (not retransmission) and not exceeding file size
                if (seq == expected_seq) {
                    // Handle end of file
                    if (ymodem.file_size > 0 && (received_bytes + data_size) > ymodem.file_size) {
                        write_size = ymodem.file_size - received_bytes;
                    }

                    // Write to file
                    if (write_size > 0) {
                        int written = ymodem.file_fwrite(fd, &buffer[data_offset], write_size);
                        if (written != write_size) {
                            YMODEM_LOG("Write error: %d/%d bytes written", written, write_size);
                            ymodem.uart_tx(0x15); // NAK
                            goto receive_next_packet;
                        }
                        received_bytes += write_size;
                        write_counter++;

                        if (write_counter >= YMODEM_FILE_REOPEN_INTERVAL) {
                            write_counter = 0;
                            ymodem.file_fclose(fd);
                            osDelay(100);
                            fd = ymodem.file_fopen(ymodem.file_name, "r+"); // Open in read-write mode
                            if (!fd) {
                                YMODEM_LOG("File reopen failed: %s", ymodem.file_name);
                                ymodem.uart_tx(0x15); // NAK
                                ymodem.callback(YMODEM_FILE_ERROR);
                                return;
                            }
                            // Seek to current write position
                            if (ymodem.file_fseek) {
                                if (ymodem.file_fseek(fd, received_bytes, SEEK_SET) < 0) {
                                    YMODEM_LOG("File seek failed after reopen");
                                    ymodem.uart_tx(0x15); // NAK
                                    ymodem.callback(YMODEM_FILE_ERROR);
                                    return;
                                }
                            }
                        }
                    }

                    YMODEM_LOG("Received %d/%d bytes (total %lu/%lu)", 
                            write_size, data_size, received_bytes, ymodem.file_size);

                    // Update expected sequence number
                    expected_seq++;
                }

                // Send ACK (for both new packet and retransmitted packet)
                ymodem.uart_tx(0x06); // ACK
                packet_errors = 0; // Reset error counter

                // Check if file reception is complete
                if (ymodem.file_size > 0 && received_bytes >= ymodem.file_size) {
                    expecting_header = 1; // Wait for next header packet (may be new file or end packet)
                    YMODEM_LOG("File transfer complete, waiting for EOT");
                }
            } else {
                // Packet sequence number mismatch
                YMODEM_LOG("Sequence error: expected %d or %d, got %d", 
                        expected_seq-1, expected_seq, seq);
                ymodem.uart_tx(0x15); // NAK
                packet_errors++;
            }
        } else {
            // Unexpected data packet (e.g., received data packet while waiting for header packet)
            YMODEM_LOG("Unexpected packet type: state=%s", 
                    waiting_end_packet ? "waiting_end" : (expecting_header ? "expecting_header" : "data"));
            ymodem.uart_tx(0x15); // NAK
            packet_errors++;
        }

        // Check for consecutive errors
        if (packet_errors >= 10) {
            YMODEM_LOG("Too many consecutive errors (%d), aborting", packet_errors);
            if (fd) ymodem.file_fclose(fd);
            ymodem.callback(YMODEM_PACKET_ERROR);
            return;
        }

    receive_next_packet:
        // Receive next packet
        int result = Receive_Packet(buffer, sizeof(buffer), 3000);

        if (result == 0) {
            timeout_count = 0; // Reset timeout counter (reset on each successful reception)
            continue; // Successfully received, continue processing
        } else if (result == -2) { // User cancel
            YMODEM_LOG("User canceled transfer");
            if (fd) ymodem.file_fclose(fd);
            ymodem.callback(YMODEM_CANCEL_RECEIVED);
            return;
        } else if (result == -3) { // Timeout
            timeout_count++;
            YMODEM_LOG("Receive timeout, count=%d", timeout_count);

            if (timeout_count >= 5) {
                YMODEM_LOG("Receive timeout limit reached");
                if (fd) ymodem.file_fclose(fd);
                ymodem.callback(YMODEM_RECEIVE_TIMEOUT);
                return;
            }

            // Smart retransmission request
            if (expecting_header || waiting_end_packet) {
                ymodem.uart_tx('C'); // Request header packet or end packet
                YMODEM_LOG("Resent 'C' due to timeout");
            } else {
                ymodem.uart_tx(0x15); // NAK, request retransmission of current packet
                YMODEM_LOG("Resent NAK due to timeout");
            }
        } else {
            // Packet error (e.g., CRC error)
            YMODEM_LOG("Packet error: %d", result);
            ymodem.uart_tx(0x15); // NAK
            packet_errors++;
        }
    }
}
