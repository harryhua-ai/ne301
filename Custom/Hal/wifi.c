/***************************************************************************/ /**
    * @file
    * @brief WLAN Throughput Example Application
    *******************************************************************************
    * # License
    * <b>Copyright 2022 Silicon Laboratories Inc. www.silabs.com</b>
    *******************************************************************************
    *
    * SPDX-License-Identifier: Zlib
    *
    * The licensor of this software is Silicon Laboratories Inc.
    *
    * This software is provided 'as-is', without any express or implied
    * warranty. In no event will the authors be held liable for any damages
    * arising from the use of this software.
    *
    * Permission is granted to anyone to use this software for any purpose,
    * including commercial applications, and to alter it and redistribute it
    * freely, subject to the following restrictions:
    *
    * 1. The origin of this software must not be misrepresented; you must not
    *    claim that you wrote the original software. If you use this software
    *    in a product, an acknowledgment in the product documentation would be
    *    appreciated but is not required.
    * 2. Altered source versions must be plainly marked as such, and must not be
    *    misrepresented as being the original software.
    * 3. This notice may not be removed or altered from any source distribution.
    *
  ******************************************************************************/
#include <string.h>
#include "main.h"
#include "sl_wifi_callback_framework.h"
#include "sl_net_wifi_types.h"
#include "sl_wifi.h"
#include "sl_net.h"
#include "sl_rsi_utility.h"
#include "wifi.h"
#include "debug.h"
#include "generic_utils.h"
#include "generic_file.h"
#include "common_utils.h"
#include "lwip/sockets.h"
#include <errno.h>  // For ENOBUFS
#include "mem.h"
#include "storage.h"


/******************************************************
 *                      Macros
 ******************************************************/

// Memory length for send buffer
#define MAX_TCP_SIZE 1024
#define MAX_SEND_SIZE 1024

#define SERVER_IP "192.168.10.10"

#define LISTENING_PORT 5005
#define BACK_LOG       1


/******************************************************
*                    Constants
******************************************************/
/******************************************************

*               Variable Definitions
******************************************************/
static uint8_t wifi_tread_stack[1024 * 6] ALIGN_32 IN_PSRAM;
const osThreadAttr_t wifiTask_attributes = {
    .name = "uvcTask",
    .priority = (osPriority_t) osPriorityNormal,
    .stack_mem = wifi_tread_stack,
    .stack_size = sizeof(wifi_tread_stack),
};

/************************** ncp Transmit Test Configuration ****************************/
static const sl_wifi_device_configuration_t transmit_test_configuration = {
  .boot_option = LOAD_NWP_FW,
  .mac_address = NULL,
  .band        = SL_SI91X_WIFI_BAND_2_4GHZ,
  .region_code = WORLD_DOMAIN,
  .boot_config = { .oper_mode = SL_SI91X_TRANSMIT_TEST_MODE,
                   .coex_mode = SL_SI91X_WLAN_ONLY_MODE,
                   .feature_bit_map =
#ifdef SLI_SI91X_MCU_INTERFACE
                     (SL_SI91X_FEAT_SECURITY_OPEN | SL_SI91X_FEAT_WPS_DISABLE),
#else
                     (SL_SI91X_FEAT_SECURITY_OPEN),
#endif
                   .tcp_ip_feature_bit_map =
                     (SL_SI91X_TCP_IP_FEAT_DHCPV4_CLIENT | SL_SI91X_TCP_IP_FEAT_EXTENSION_VALID),
                   .custom_feature_bit_map     = SL_SI91X_CUSTOM_FEAT_EXTENTION_VALID,
                   .ext_custom_feature_bit_map = (MEMORY_CONFIG
#if defined(SLI_SI917) || defined(SLI_SI915)
                                                  | SL_SI91X_EXT_FEAT_FRONT_END_SWITCH_PINS_ULP_GPIO_4_5_0
#endif
                                                  ),
                   .bt_feature_bit_map         = SL_SI91X_BT_RF_TYPE,
                   .ext_tcp_ip_feature_bit_map = SL_SI91X_CONFIG_FEAT_EXTENTION_VALID,
                   .ble_feature_bit_map        = 0,
                   .ble_ext_feature_bit_map    = 0,
                   .config_feature_bit_map     = SL_SI91X_FEAT_SLEEP_GPIO_SEL_BITMAP }
};

const sl_wifi_data_rate_t rate    = SL_WIFI_DATA_RATE_6;
const sl_wifi_tx_test_mode_t mode = SL_WIFI_TEST_CONTINOUS_WAVE_MODE_OFF_CENTER_HIGH;
int wifi_ant_flag = 0;

sl_si91x_request_tx_test_info_t tx_test_info = {
  .enable      = 1,
  .power       = 127,
  .rate        = rate,
  .length      = 100,
  .mode        = mode,
  .channel     = 1,
  .aggr_enable = 0,
  .no_of_pkts  = 0,
#if defined(SLI_SI917) || defined(SLI_SI915)
  .enable_11ax            = 0,
  .coding_type            = 0,
  .nominal_pe             = 0,
  .ul_dl                  = 0,
  .he_ppdu_type           = 0,
  .beam_change            = 0,
  .bw                     = 0,
  .stbc                   = 0,
  .tx_bf                  = 0,
  .gi_ltf                 = 0,
  .dcm                    = 0,
  .nsts_midamble          = 0,
  .spatial_reuse          = 0,
  .bss_color              = 0,
  .he_siga2_reserved      = 0,
  .ru_allocation          = 0,
  .n_heltf_tot            = 0,
  .sigb_dcm               = 0,
  .sigb_mcs               = 0,
  .user_sta_id            = 0,
  .user_idx               = 0,
  .sigb_compression_field = 0,
#endif
};
/************************** ncp Transmit Test Configuration end****************************/

static const sl_wifi_device_configuration_t firmware_update_configuration = {
#if (FW_UPDATE_TYPE == NWP_FW_UPDATE)
  .boot_option = BURN_NWP_FW,
#else
  .boot_option = BURN_M4_FW,
#endif
  .mac_address = NULL,
  .band        = SL_SI91X_WIFI_BAND_2_4GHZ,
  .region_code = US,
  .boot_config = { .oper_mode              = SL_SI91X_CLIENT_MODE,
                   .coex_mode              = SL_SI91X_WLAN_ONLY_MODE,
                   .feature_bit_map        = (SL_SI91X_FEAT_SECURITY_PSK | SL_SI91X_FEAT_AGGREGATION),
                   .tcp_ip_feature_bit_map = (SL_SI91X_TCP_IP_FEAT_DHCPV4_CLIENT),
                   .custom_feature_bit_map = (SL_SI91X_CUSTOM_FEAT_EXTENTION_VALID),
                   .ext_custom_feature_bit_map =
                     (SL_SI91X_EXT_FEAT_XTAL_CLK | SL_SI91X_EXT_FEAT_UART_SEL_FOR_DEBUG_PRINTS | MEMORY_CONFIG
                      | SL_SI91X_EXT_FEAT_FRONT_END_SWITCH_PINS_ULP_GPIO_4_5_0
                      ),
                   .bt_feature_bit_map         = 0,
                   .ext_tcp_ip_feature_bit_map = SL_SI91X_CONFIG_FEAT_EXTENTION_VALID,
                   .ble_feature_bit_map        = 0,
                   .ble_ext_feature_bit_map    = 0,
                   .config_feature_bit_map     = 0 }
};

si91x_wlan_app_cb_t si91x_wlan_app_cb;
uint32_t chunk_cnt = 0u, chunk_check = 0u, offset = 0u, fw_image_size = 0u;
uint8_t recv_buffer[SI91X_CHUNK_SIZE] ALIGN_32 UNCACHED;
sl_wifi_firmware_version_t fw_version = {0};
uint8_t one_time = 1;
volatile uint32_t remaining_bytes = 0u;
uint32_t t_start = 0;
uint32_t t_end = 0;
uint32_t xfer_time = 0;
int tcp_socket = -1;

/******************************************************
*               Function Declarations
******************************************************/

static void data_send(uint8_t * buffer, uint32_t len)
{
    ssize_t sent_bytes;
    uint32_t total_sent = 0;

    if (tcp_socket != -1) {
        while (total_sent < len) {
            uint32_t send_len = (len - total_sent) > MAX_SEND_SIZE ? MAX_SEND_SIZE : (len - total_sent);
            sent_bytes = send(tcp_socket, buffer + total_sent, send_len, 0);

            if (sent_bytes < 0) {
                if (errno == ENOBUFS) {
                    osDelay(1); // 1ms
                    continue;
                } else {
                    printf("\r\nSocket send failed with bsd error: %d\r\n", errno);
                    sent_bytes = -1;
                    close(tcp_socket);
                    break;
                }
            } else if (sent_bytes == 0) {
                printf("\r\nSocket closed by peer\r\n");
                tcp_socket = -1;
                close(tcp_socket);
                break;
            } else {
                total_sent += sent_bytes;
            }
        }
    }
}

__attribute__((unused)) static void tcpConsleProcess(void *argument)
{
    int server_socket = -1, client_socket = -1;
    int socket_return_value           = 0;
    struct sockaddr_in server_address = { 0 };
    socklen_t socket_length           = sizeof(struct sockaddr_in);
    uint8_t *data_buffer;

    server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_socket < 0) {
        printf("\r\nSocket creation failed with bsd error: %d\r\n", errno);
        return;
    }
    printf("\r\nServer Socket ID : %d\r\n", server_socket);

    server_address.sin_family = AF_INET;
    server_address.sin_port   = LISTENING_PORT;
    sl_net_inet_addr(SERVER_IP, &server_address.sin_addr.s_addr);

    socket_return_value = bind(server_socket, (struct sockaddr *)&server_address, socket_length);
    if (socket_return_value < 0) {
        printf("\r\nSocket bind failed with bsd error: %d\r\n", errno);
        close(server_socket);
        return;
    }

    socket_return_value = listen(server_socket, BACK_LOG);
    if (socket_return_value < 0) {
        printf("\r\nSocket listen failed with bsd error: %d\r\n", errno);
        close(server_socket);
        return;
    }
    printf("\r\nListening on Local Port : %d\r\n", LISTENING_PORT);
    debug_output_register((log_custom_output_func_t)data_send);
    while (1) {
        client_socket = accept(server_socket, NULL, NULL);
        if (client_socket < 0) {
            printf("\r\nSocket accept failed with bsd error: %d\r\n", errno);
            osDelay(1);
            continue;
        }
        data_buffer = hal_mem_alloc_fast(MAX_TCP_SIZE);
        tcp_socket = client_socket;
        
        printf("\r\n[Echo] Client connected. Socket: %d\r\n", client_socket);

        int n = 1;
        while (n  > 0) {
            n = recv(client_socket, data_buffer, MAX_TCP_SIZE, 0);
            int sent = 0, total = 0;
            if(n > 0){
                for (uint16_t i = 0; i < n; i++) {
                    printf("%c", ((uint8_t *)data_buffer)[i]);
                    debug_cmdline_input(((uint8_t *)data_buffer)[i]);
                }
            }
            while (total < n) {
                sent = send(client_socket, data_buffer + total, n - total, 0);
                if (sent <= 0) break;
                total += sent;
            }
            osDelay(1);
        }
        printf("[Echo] Client disconnected.\r\n");
        if(data_buffer != NULL){
            hal_mem_free(data_buffer);
        }
        tcp_socket = -1;
        close(client_socket);

    }
    close(server_socket);
    osThreadExit();
}


static uint32_t get_fw_size(char *buffer)
{
  fwreq_t *fw = (fwreq_t *)buffer;
  return fw->image_size;
}

static int32_t sl_si91x_app_task_fw_update_via_xmodem(uint8_t *rx_data, uint32_t size)
{
    UNUSED_PARAMETER(size);
    int32_t status = SL_STATUS_OK;
    switch (si91x_wlan_app_cb.state) {
        case SI91X_WLAN_INITIAL_STATE:
        case SI91X_WLAN_FW_UPGRADE: {
            if (one_time == 1) {
                // Initial setup (only executed once)
                fw_image_size = get_fw_size((char *)rx_data);
                remaining_bytes = fw_image_size;
                
                chunk_check = (fw_image_size + FW_HEADER_SIZE + SI91X_CHUNK_SIZE - 1) / SI91X_CHUNK_SIZE;
                one_time = 0;
                LOG_SIMPLE("Firmware upgrade started. Total chunks: %lu\r\n", chunk_check);
            }

            if (chunk_cnt >= chunk_check) {
                break;
            }

            // Set transfer mode based on data block position
            uint32_t transfer_mode;
            if (chunk_cnt == 0) {
                transfer_mode = SI91X_START_OF_FILE;
            } else if (chunk_cnt == (chunk_check - 1)) {
                transfer_mode = SI91X_END_OF_FILE;
            } else {
                transfer_mode = SI91X_IN_BETWEEN_FILE;
            }

            // Execute firmware upgrade transfer
            status = sl_si91x_bl_upgrade_firmware(rx_data, SI91X_CHUNK_SIZE, transfer_mode);
            if (status != SL_STATUS_OK) {
                LOG_SIMPLE("ERROR at chunk %lu: 0x%lx\r\n", chunk_cnt, status);
                return status;
            }

            // Update status counter
            offset += SI91X_CHUNK_SIZE;
            chunk_cnt++;
            
            // Transfer completion handling
            if (chunk_cnt == chunk_check) {
                LOG_SIMPLE("\r\nFirmware upgrade completed\r\n");
                si91x_wlan_app_cb.state = SI91X_WLAN_FW_UPGRADE_DONE;
            }
            break;
        }
        case SI91X_WLAN_FW_UPGRADE_DONE: {
            status = sl_net_init(SL_NET_WIFI_CLIENT_INTERFACE, NULL, NULL, NULL);
            if (status != SL_STATUS_OK) {
                return status;
            }

            status = sl_wifi_get_firmware_version(&fw_version);
            if (status == SL_STATUS_OK) {
                LOG_SIMPLE("New firmware version: ");
                print_firmware_version(&fw_version);
            }

            t_end = osKernelGetTickCount();
            xfer_time = t_end - t_start;
            uint32_t secs = xfer_time / 1000;
            LOG_SIMPLE("\r\nFirmware upgrade time: %d seconds\r\n", (int)secs);
            LOG_SIMPLE("\r\nDEMO COMPLETED\r\n");
            
            break;
        }
        default:
            break;
    }
    return status;
}

static int firmware_upgrade_from_file(const char *file_path)
{
    void *fd = NULL;
    size_t read_size;
    int32_t status = SL_STATUS_OK;
    uint32_t total_chunks;
    uint32_t file_remaining;
    uint32_t progress_percent = 0;
    uint32_t last_reported_percent = 0;
    uint32_t total_size = 0;

    printf("\n[FW UPGRADE] Starting firmware upgrade from file: %s\r\n", file_path);

    fd = file_fopen(file_path, "rb");
    if (fd == NULL) {
        printf("[ERROR] Failed to open firmware file\r\n");
        return -1;
    }

    read_size = file_fread(fd, recv_buffer, SI91X_CHUNK_SIZE);
    if (read_size == 0) {
        printf("[ERROR] Failed to read firmware header\r\n");
        file_fclose(fd);
        return -1;
    }

    chunk_cnt = 0;
    offset = 0;
    one_time = 1;
    si91x_wlan_app_cb.state = SI91X_WLAN_FW_UPGRADE;

    t_start = osKernelGetTickCount();
    printf("[TIMER] Firmware upgrade started at tick: %lu\r\n", t_start);

    fw_image_size = get_fw_size((char *)recv_buffer);
    total_size = fw_image_size + FW_HEADER_SIZE;
    printf("\n[FIRMWARE] Firmware details:\r\n");
    printf("  - Header size: %ld bytes\r\n", FW_HEADER_SIZE);
    printf("  - Payload size: %lu bytes\r\n", fw_image_size);
    printf("  - Total size: %lu bytes\r\n", total_size);
    printf("  - Chunk size: %ld bytes\r\n", SI91X_CHUNK_SIZE);

    total_chunks = (total_size + SI91X_CHUNK_SIZE - 1) / SI91X_CHUNK_SIZE;
    file_remaining = total_size - read_size;
    printf("  - Total chunks: %lu\n", total_chunks);
    printf("  - Remaining bytes: %lu\n", file_remaining);
    printf("\r\n[PROGRESS] Starting firmware transmission...\r\n");

    printf("\r\n[BLOCK 0] Sending header block (START_OF_FILE)\r\n");
    status = sl_si91x_app_task_fw_update_via_xmodem(recv_buffer, SI91X_CHUNK_SIZE);
    if (status != SL_STATUS_OK) {
        printf("[ERROR] First chunk processing failed: 0x%lx\r\n", status);
        file_fclose(fd);
        return -1;
    }
    printf("[SUCCESS] Header block sent\r\n");

    for (uint32_t i = 1; i < total_chunks; i++) {
        memset(recv_buffer, 0, SI91X_CHUNK_SIZE);
        size_t bytes_to_read = (file_remaining > SI91X_CHUNK_SIZE) ? 
                              SI91X_CHUNK_SIZE : file_remaining;
        
        read_size = file_fread(fd, recv_buffer, bytes_to_read);
        if (read_size == 0) {
            printf("[ERROR] File read failed at chunk %lu\r\n", i);
            break;
        }
        
        file_remaining -= read_size;
        
        progress_percent = (i * 100) / total_chunks;
        if (progress_percent != last_reported_percent && 
            (progress_percent % 10 == 0 || i == total_chunks - 1)) {
            printf("\n[PROGRESS] %ld%% complete (%lu/%lu chunks)\r\n", 
                   progress_percent, i, total_chunks);
            last_reported_percent = progress_percent;
        }
        

        if (i == (total_chunks - 1)) {
            printf("\n[BLOCK %lu] Sending final block (END_OF_FILE, %zu bytes)\r\n", i, read_size);
        } else {
            printf("\n[BLOCK %lu] Sending data block (%zu bytes)\r\n", i, read_size);
        }

        status = sl_si91x_app_task_fw_update_via_xmodem(recv_buffer, SI91X_CHUNK_SIZE);
        if (status != SL_STATUS_OK) {
            printf("[ERROR] Chunk %lu processing failed: 0x%lx\r\n", i, status);
            break;
        }
        
        printf("[SUCCESS] Block %lu processed\r\n", i);
        osDelay(10);
    }

    // Close file
    file_fclose(fd);
    printf("[FILE] Firmware file closed\r\n");

    // Check if completion state needs to be triggered
    if (si91x_wlan_app_cb.state == SI91X_WLAN_FW_UPGRADE_DONE) {
        printf("\n[UPGRADE] Triggering final upgrade state\r\n");
        return sl_si91x_app_task_fw_update_via_xmodem(NULL, 0);
    }
    return -1;
}

/**
 * @brief Upgrade the firmware of the Wi-Fi module.
 *
 * This function performs the following steps:
 * 1. Initialize the Wi-Fi module as a client.
 * 2. Check if the firmware needs to be updated.
 * 3. If the firmware needs to be updated, read the firmware image from the file
 *    specified by the `WIFI_FIR_NAME` macro and upgrade the firmware.
 *
 * @return 0 if the firmware upgrade is successful, -1 otherwise.
 */
static void wifi_update_process(void) 
{
    int32_t status = SL_STATUS_OK;
    // PowerHandle wifi_handle = pwr_manager_get_handle(PWR_WIFI);
    // pwr_manager_acquire(wifi_handle);
    // osDelay(100);
    storage_nvs_write(NVS_FACTORY, NVS_KEY_WIFI_MODE, WIFI_MODE_NORMAL, strlen(WIFI_MODE_NORMAL));
    
    status = sl_net_init(SL_NET_WIFI_CLIENT_INTERFACE, &firmware_update_configuration, NULL, NULL);
    if (status == SL_STATUS_OK) {
        printf("wifi_update sl_net_init ok \r\n");
        return;   
    }
    
    status = firmware_upgrade_from_file(WIFI_FIR_NAME);
    if(status == 0) {
        printf("wifi_update ok \r\n");
        storage_nvs_flush_all();
        osDelay(200);
#if ENABLE_U0_MODULE
        u0_module_clear_wakeup_flag();
        u0_module_reset_chip_n6();
#endif
        HAL_NVIC_SystemReset();
    }
    // pwr_manager_release(wifi_handle);
    printf("wifi_update failed \r\n");
    return;
}

static void wifi_ant_process(void)
{
    int32_t status = SL_STATUS_OK;

    storage_nvs_write(NVS_FACTORY, NVS_KEY_WIFI_MODE, WIFI_MODE_NORMAL, strlen(WIFI_MODE_NORMAL));

    status = sl_net_init(SL_NET_WIFI_CLIENT_INTERFACE, &transmit_test_configuration, NULL, NULL);
    if (status != SL_STATUS_OK) {
        printf("Failed to start Wi-Fi client interface: 0x%lx\r\n", status);
        return;
    }
    printf("\r\nWi-Fi Init Done \r\n");

    status = sl_wifi_set_antenna(SL_WIFI_CLIENT_2_4GHZ_INTERFACE, SL_WIFI_ANTENNA_INTERNAL);
    if (status != SL_STATUS_OK) {
        printf("Failed to start set Antenna: 0x%lx\r\n", status);
        return;
    }
}
static int wifi_update_cmd(int argc, char* argv[]) 
{
    storage_nvs_write(NVS_FACTORY, NVS_KEY_WIFI_MODE, WIFI_MODE_UPDATE, strlen(WIFI_MODE_UPDATE));
    LOG_SIMPLE("wifi update, System reset...\r\n");
    storage_nvs_flush_all();
    osDelay(200);
#if ENABLE_U0_MODULE
    u0_module_clear_wakeup_flag();
    u0_module_reset_chip_n6();
#endif
    HAL_NVIC_SystemReset();
    return 0;
}

static int wifi_test_cmd(int argc, char* argv[]) 
{
    storage_nvs_write(NVS_FACTORY, NVS_KEY_WIFI_MODE, WIFI_MODE_TX_TEST, strlen(WIFI_MODE_TX_TEST));
    LOG_SIMPLE("wifi test, System reset...\r\n");
    storage_nvs_flush_all();
    osDelay(200);
#if ENABLE_U0_MODULE
    u0_module_clear_wakeup_flag();
    u0_module_reset_chip_n6();
#endif
    HAL_NVIC_SystemReset();
    return 0;
}

static int wifi_ant_cmd(int argc, char* argv[]) 
{
    if (argc < 2) {
        LOG_SIMPLE("Usage: wifi_ant <start|stop>\r\n");
        return -1;
    }
    sl_status_t status;
    if(wifi_ant_flag == 0) {
        LOG_SIMPLE("WiFi antenna test not started.\r\n");
        return 0;
    }
    if (strcmp(argv[1], "start") == 0) {

        tx_test_info.mode = SL_WIFI_TEST_CONTINOUS_MODE;
        status            = sl_si91x_transmit_test_start(&tx_test_info);
        if (status != SL_STATUS_OK) {
            LOG_SIMPLE("\r\nantenna test start Failed, Error Code : 0x%lX", status);
            return 0 ;
        }
        LOG_SIMPLE("WiFi antenna test started.\r\n");
        return 0;
    } else if (strcmp(argv[1], "stop") == 0) {
        status = sl_si91x_transmit_test_stop();
        if (status != SL_STATUS_OK) {
            LOG_SIMPLE("antenna test stop Failed, Error Code : 0x%lX", status);
            return 0;
        }
        LOG_SIMPLE("WiFi antenna test stopped.\r\n");
        return 0;
    } else {
        LOG_SIMPLE("Unknown command. Usage: wifi_ant <start|stop>\r\n");
        return -1;
    }
}

static int wifi_cmd_spi(int argc, char *argv[])
{
    if (argc < 2) {
        LOG_SIMPLE("Usage: wifispi <hexdata> [count]\r\n");
        LOG_SIMPLE("Example: wifispi 0a0b0c0d 10\r\n");
        LOG_SIMPLE("         wifispi 0x0a 5\r\n");
        return -1;
    }
    // Parse hex string to byte stream, support 0x prefix
    const char *hexstr = argv[1];
    int hexlen = strlen(hexstr);
    uint8_t txbuf[256] = {0};
    uint8_t rxbuf[256] = {0};
    int txlen = 0;
    int i = 0;
    while (i < hexlen && txlen < 256) {
        // Skip 0x or 0X prefix
        if ((hexstr[i] == '0') && (hexstr[i+1] == 'x' || hexstr[i+1] == 'X')) {
            i += 2;
            continue;
        }
        unsigned int val = 0;
        if (sscanf(&hexstr[i], "%2x", &val) == 1) {
            txbuf[txlen++] = (uint8_t)val;
            i += 2;
        } else {
            LOG_SIMPLE("Invalid hexdata\r\n");
            return -1;
        }
    }
    if (txlen == 0) {
        LOG_SIMPLE("No valid hexdata\r\n");
        return -1;
    }
    int count = 1;
    if (argc >= 3) {
        count = atoi(argv[2]);
        if (count < 1) count = 1;
    }
    for (int c = 0; c < count; c++) {
        sl_status_t ret = sl_si91x_host_spi_transfer(txbuf, rxbuf, txlen);
        if (ret != SL_STATUS_OK) {
            LOG_SIMPLE("spi transfer failed, ret=%d\r\n", ret);
            return -1;
        }
        LOG_SIMPLE("spi tx:");
        for (int j = 0; j < txlen; j++) printf(" %02X", txbuf[j]);
        LOG_SIMPLE("\r\nspi rx:");
        for (int j = 0; j < txlen; j++) printf(" %02X", rxbuf[j]);
        LOG_SIMPLE("\r\n");
    }
    return 0;
}

debug_cmd_reg_t wifi_cmd_table[] = {
    {"wifiup",     "WiFi update.",      wifi_update_cmd},
    {"wifitest",   "WiFi test.",        wifi_test_cmd},
    {"wifi_ant",  "WiFi antenna test <start|stop>",      wifi_ant_cmd},
    {"wifispi", "wifi spi <hexdata> [count]", wifi_cmd_spi},
};


static void wifi_cmd_register(void)
{
    debug_cmdline_register(wifi_cmd_table, sizeof(wifi_cmd_table) / sizeof(wifi_cmd_table[0]));
}

void wifi_register(void)
{
    driver_cmd_register_callback("wifi_mode", wifi_cmd_register);
}

int is_wifi_ant(void)
{
    return wifi_ant_flag;
}
void wifi_mode_process(void)
{
    char wifi_mode[16] = {0};

    if(storage_nvs_read(NVS_FACTORY, NVS_KEY_WIFI_MODE, &wifi_mode, sizeof(wifi_mode)) < 0) {
        return;
    }

    printf("\r\n wifi_mode: %s \r\n", wifi_mode);

    if (strcmp(WIFI_MODE_UPDATE, wifi_mode) == 0) {
        printf("\r\n wifi_update_process \r\n");
        wifi_update_process();
        wifi_ant_flag = 1;
        return;
    }

    if (strcmp(WIFI_MODE_TX_TEST, wifi_mode) == 0) {
        printf("\r\n wifi_test_process \r\n");
        wifi_ant_process();
        wifi_ant_flag = 1;
        return;
    }
}
