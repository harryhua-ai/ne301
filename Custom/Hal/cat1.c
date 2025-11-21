#include "cat1.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "usart.h"
#include "common_utils.h"
#include "debug.h"

static cat1_t g_cat1 = {0};
extern UART_HandleTypeDef huart7;

static uint8_t cat1_tread_stack[1024 * 4] ALIGN_32 IN_PSRAM;
const osThreadAttr_t cat1Task_attributes = {
    .name = "cat1Task",
    .priority = (osPriority_t) osPriorityNormal,
    .stack_mem = cat1_tread_stack,
    .stack_size = sizeof(cat1_tread_stack),
    .cb_mem     = 0,
    .cb_size    = 0,
    .attr_bits  = 0u,
    .tz_module  = 0u,
};

int cat1_ppp_enable_recv_isr(uint8_t *buf, uint16_t len)
{
    int ret = 0;

    if (!g_cat1.is_init) return AICAM_ERROR_NOT_INITIALIZED;
    if (HAL_UARTEx_ReceiveToIdle_DMA(g_cat1.huart, buf, len) == HAL_OK) {
        SCB_InvalidateDCache_by_Addr((uint32_t *)buf, len);
        __HAL_DMA_DISABLE_IT(((UART_HandleTypeDef *)g_cat1.huart)->hdmarx, DMA_IT_HT);
    } else {
        __HAL_UART_CLEAR_FLAG(((UART_HandleTypeDef *)g_cat1.huart), 0xFFFFFFFF);
        HAL_UART_AbortReceive(g_cat1.huart);
        ret = HAL_UARTEx_ReceiveToIdle_DMA(g_cat1.huart, buf, len);
        SCB_InvalidateDCache_by_Addr((uint32_t *)buf, len);
        __HAL_DMA_DISABLE_IT(((UART_HandleTypeDef *)g_cat1.huart)->hdmarx, DMA_IT_HT);
    }
    return ret;
}

void HAL_UART7_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size) {
//     if (huart->Instance == UART7)
//     { // Adjust for your UART instance
        if (g_cat1.is_ppp_mode && g_cat1.recv_callback) g_cat1.recv_callback(huart, Size);
        else ATC_IdleLineCallback(&g_cat1.hAtc, Size);
    // }
}

static cat1_err_t cat1_write_at(const char *atCmd, char *atResp, int atRespLen,
                                int timeout, const char *pass_phrase, const char *fail_phrase)
{
    char *response = NULL;
    int ret = ATC_SendReceive(&g_cat1.hAtc, atCmd, timeout, &response, timeout, 2, pass_phrase, fail_phrase);

    // if (atCmd) LOG_DRV_INFO("Send AT: %s \r\n", atCmd);
    // if (response) LOG_DRV_INFO("Recv AT: %s\r\n", response);
    
    if (ret == 1 || ret == 2) // Match pass_phrase or fail_phrase
    {
        char *p = response;

        if (atCmd != NULL) {
            // Skip echo command (case-insensitive comparison)
            size_t cmd_len = strlen(atCmd);
            if (strncasecmp(p, atCmd, cmd_len) == 0) {
                p += cmd_len;
            }
        }

        // Skip possible \r\n after echo command
        while (*p == '\r' || *p == '\n') {
            p++;
        }

        // Copy remaining content
        strncpy(atResp, p, atRespLen - 1);
        atResp[atRespLen - 1] = '\0';

        return (ret == 1) ? CAT1_OK : CAT1_FAIL;
    }

    snprintf(atResp, atRespLen, "ERROR: %d", ret);
    return CAT1_ERR_TIMEOUT;
}

static int32_t cat1_get_baud_rate(void)
{
    char atResp[256];
    int retry = 0;
    int baud = -1;

    while (retry < CAT1_GET_BAUD_RETRY_MAX) {
        cat1_err_t err = cat1_write_at("AT+IPR?\r", atResp, sizeof(atResp),
                                       200, "+IPR:", "ERROR");
        if (err == CAT1_OK) {
            char *p = strstr(atResp, "+IPR:");
            if (p) {
                baud = atoi(p + 5);
                LOG_DRV_INFO("Current CAT1 baud: %d \r\n", baud);
                break;
            }
        }
        retry++;
        LOG_DRV_INFO("Get baud rate fail, retry %d/%d\r\n", retry, CAT1_GET_BAUD_RETRY_MAX);
    }

    return baud;
}

// Set baud rate
static cat1_err_t cat1_set_baud_rate(void)
{
    char atCmd[64];
    char atResp[256];
    cat1_err_t err = CAT1_FAIL;
    const int baud_rates[] = {115200, 230400, 460800, 921600};
    const int num_rates = sizeof(baud_rates) / sizeof(baud_rates[0]);

    for (int i = 0; i < num_rates; i++) {
        HAL_UART_DeInit((UART_HandleTypeDef *)g_cat1.huart);
        ((UART_HandleTypeDef *)g_cat1.huart)->Init.BaudRate = baud_rates[i];
        HAL_UART_Init((UART_HandleTypeDef *)g_cat1.huart);

        osDelay(100);

        int32_t current_baud = cat1_get_baud_rate();
        if (current_baud < 0) continue;

        if (current_baud != CAT1_BAUD_RATE) {
            snprintf(atCmd, sizeof(atCmd), "AT+IPR=%d;&W\r", CAT1_BAUD_RATE);
            err = cat1_write_at(atCmd, atResp, sizeof(atResp),
                                200, "OK", "ERROR");
            LOG_DRV_INFO("Set CAT1 baud to %d, ret=%d \r\n", CAT1_BAUD_RATE, err);
            if (err == CAT1_OK) {
                HAL_UART_DeInit(g_cat1.huart);
                ((UART_HandleTypeDef *)g_cat1.huart)->Init.BaudRate = CAT1_BAUD_RATE;
                HAL_UART_Init(g_cat1.huart);
                break;
            }
        } else {
            err = CAT1_OK;
            break;
        }
    }
    return err;
}

// Get signal quality
static cat1_err_t get_signal_quality(cellularSignalQuality_t *sq)
{
    char atResp[256];
    memset(sq, 0, sizeof(cellularSignalQuality_t));

    cat1_err_t err = cat1_write_at("AT+CSQ\r", atResp, sizeof(atResp),
                                  500, "+CSQ:", "ERROR");
    if (err != CAT1_OK) return err;

    int rssi, ber;
    if (sscanf(atResp, "+CSQ:%d,%d", &rssi, &ber) == 2) {
        if (rssi >= 0 && rssi <= 31) {
            int dBm = -113 + 2 * rssi;
            int asu = dBm + 140;
            int dBmLevel = 0;

            if (dBm >= -53) dBmLevel = 5;
            else if (dBm >= -63) dBmLevel = 4;
            else if (dBm >= -73) dBmLevel = 3;
            else if (dBm >= -83) dBmLevel = 2;
            else if (dBm >= -93) dBmLevel = 1;

            sq->rssi = rssi;
            sq->ber = ber;
            sq->dbm = dBm;
            sq->asu = asu;
            sq->level = dBmLevel;
            snprintf(sq->quality, sizeof(sq->quality),
                    "%dasu(%ddBm)", asu, dBm);
        } else {
            snprintf(sq->quality, sizeof(sq->quality), "-");
        }
        LOG_DRV_INFO("Signal: %s\r\n", sq->quality);
        return CAT1_OK;
    }
    LOG_DRV_ERROR("Parse +CSQ failed: %s\r\n", atResp);
    return CAT1_FAIL;
}

// Get module model
static cat1_err_t cat1_get_model(char *model, int len)
{
    char atResp[128];
    cat1_err_t err = cat1_write_at("AT+CGMM\r", atResp, sizeof(atResp), 500, "OK", "ERROR");
    if (err == CAT1_OK) {
        char *p = strtok(atResp, "\r\n");
        while (p) {
            if (strlen(p) > 2 && strstr(p, "OK") == NULL && strstr(p, "ERROR") == NULL) {
                strncpy(model, p, len-1); model[len-1]=0;
                break;
            }
            p = strtok(NULL, "\r\n");
        }
    }
    LOG_DRV_INFO("Module Model: %s\r\n", model);
    return err;
}

// Get version number
static cat1_err_t cat1_get_version(char *version, int len)
{
    char atResp[128];
    cat1_err_t err = cat1_write_at("AT+CGMR\r", atResp, sizeof(atResp), 500, "OK", "ERROR");
    if (err == CAT1_OK) {
        char *p = strtok(atResp, "\r\n");
        while (p) {
            if (strlen(p) > 2 && strstr(p, "OK") == NULL && strstr(p, "ERROR") == NULL) {
                strncpy(version, p, len-1); version[len-1]=0;
                break;
            }
            p = strtok(NULL, "\r\n");
        }
    }
    LOG_DRV_INFO("Module Version: %s\r\n", version);
    return err;
}

// Get ICCID
static cat1_err_t cat1_get_iccid(char *iccid, int len)
{
    char atResp[128];
    cat1_err_t err = cat1_write_at("AT+QCCID\r", atResp, sizeof(atResp), 500, "OK", "ERROR");
    if (err == CAT1_OK) {
        char *p = strstr(atResp, "+QCCID:");
        if (p) {
            p += strlen("+QCCID:");
            while (*p == ' ') p++;
            strncpy(iccid, p, len-1);
            iccid[len-1] = 0;
            char *end = strchr(iccid, '\r');
            if (end) *end = 0;
        }
    }
    LOG_DRV_INFO("ICCID: %s\r\n", iccid);
    return err;
}

// Get IMSI
static cat1_err_t cat1_get_imsi(char *imsi, int len)
{
    char atResp[128];
    cat1_err_t err = cat1_write_at("AT+CIMI\r", atResp, sizeof(atResp), 500, "OK", "ERROR");
    if (err == CAT1_OK) {
        char *p = atResp;
        while (*p && (*p < '0' || *p > '9')) p++;
        strncpy(imsi, p, len-1);
        imsi[len-1]=0;
        char *end = strchr(imsi, '\r');
        if (end) *end = 0;
    }
    LOG_DRV_INFO("IMSI: %s\r\n", imsi);
    return err;
}

// Get carrier/ISP
static cat1_err_t cat1_get_isp(char *isp, int len)
{
    char atResp[128];
    cat1_err_t err = cat1_write_at("AT+COPS?\r", atResp, sizeof(atResp), 500, "OK", "ERROR");
    if (err == CAT1_OK) {
        char *p = strstr(atResp, "+COPS:");
        if (p) {
            // Format: +COPS: 0,0,"CMCC",7
            p = strchr(p, '"');
            if (p) {
                char *end = strchr(p+1, '"');
                if (end) {
                    int copylen = (end-p-1) < (len-1) ? (end-p-1) : (len-1);
                    strncpy(isp, p+1, copylen);
                    isp[copylen] = 0;
                }
            }
        }
    }
    LOG_DRV_INFO("ISP: %s\r\n", isp);
    return err;
}

static cat1_err_t cat1_usb_ecm_enable(void)
{
    char atResp[128];
    cat1_err_t err = CAT1_FAIL;
    
    err = cat1_write_at("AT+QCFG=\"usbnet\"\r", atResp, sizeof(atResp), 500, "OK", "ERROR");
    if (err == CAT1_OK) {
        if (strstr(atResp, "+QCFG: \"usbnet\",1")) return CAT1_OK;
        err = cat1_write_at("AT+QCFG=\"usbnet\",1\r", atResp, sizeof(atResp), 500, "OK", "ERROR");
        if (err == CAT1_OK) {
            // Restart module
            osDelay(200);
            pwr_manager_release(g_cat1.pwr_handle);
            osDelay(500);
            pwr_manager_acquire(g_cat1.pwr_handle);
            osDelay(300);
            if (cat1_get_baud_rate() != CAT1_BAUD_RATE) {
                err = cat1_set_baud_rate();
            }
        }
    }
    return err;
}

static cat1_err_t get_status(cellularStatusAttr_t *status)
{
    LOG_DRV_INFO("Get CAT1 status...\r\n");
    char atResp[64];
    cat1_err_t err;

    // SIM card status
    err = cat1_write_at("AT+CPIN?\r", atResp, sizeof(atResp), 500, "+CPIN:", "ERROR");
    LOG_DRV_INFO("AT+CPIN? => %s \r\n", atResp);
    if (strstr(atResp, "+CPIN")) {
        if (strstr(atResp, "READY")) {
            snprintf(status->modemStatus, sizeof(status->modemStatus), "%s", "Ready");
        } else if (strstr(atResp, "SIM PIN")) {
            snprintf(status->modemStatus, sizeof(status->modemStatus), "%s", "PIN Required");
        } else if (strstr(atResp, "SIM PUK")) {
            snprintf(status->modemStatus, sizeof(status->modemStatus), "%s", "PUK Required");
        } else {
            snprintf(status->modemStatus, sizeof(status->modemStatus), "%s", atResp);
        }
    } else if (strstr(atResp, "+CME ERROR")) {
        int errCode = -1;
        sscanf(atResp, "+CME ERROR: %d", &errCode);
        if (errCode == 10)
            snprintf(status->modemStatus, sizeof(status->modemStatus), "%s", "No SIM Card");
        else
            snprintf(status->modemStatus, sizeof(status->modemStatus), "%s", atResp);
    } else {
        if (atResp[0] != '\0')
            snprintf(status->modemStatus, sizeof(status->modemStatus), "%s", atResp);
        else
            snprintf(status->modemStatus, sizeof(status->modemStatus), "%s", "Unknown");
    }

    // IMEI
    err = cat1_write_at("AT+GSN\r", atResp, sizeof(atResp), 500, "OK", "ERROR");
    LOG_DRV_INFO("AT+GSN => %s \r\n", atResp);
    if (err == CAT1_OK) {
        char *p = atResp;
        while (*p && (*p < '0' || *p > '9')) p++;
        strncpy(status->imei, p, sizeof(status->imei)-1);
        status->imei[sizeof(status->imei)-1]=0;
        char *end = strchr(status->imei, '\r');
        if (end) *end = 0;
    }

    // IMSI
    cat1_get_imsi(status->imsi, sizeof(status->imsi));

    // ICCID
    cat1_get_iccid(status->iccid, sizeof(status->iccid));

    // Model
    cat1_get_model(status->model, sizeof(status->model));

    // Version
    cat1_get_version(status->version, sizeof(status->version));

    // Carrier/ISP
    cat1_get_isp(status->isp, sizeof(status->isp));

    // Signal quality
    cellularSignalQuality_t signal;
    get_signal_quality(&signal);
    strncpy(status->signalLevel, signal.quality, sizeof(status->signalLevel)-1);

    return CAT1_OK;
}

// Check PIN
static cat1_err_t check_pin_status(void)
{
    char atResp[256];
    cat1_err_t err;
    int retry = 0;

    while (retry++ < 5) {
        err = cat1_write_at("AT+CPIN?\r", atResp, sizeof(atResp),
                            500, "+CPIN:", "ERROR");
        LOG_DRV_INFO("AT+CPIN? => %s \r\n", atResp);
        if (err == CAT1_OK) {
            if (strstr(atResp, "READY")) {
                strcpy(g_cat1.status.modemStatus, "Ready");
                return CAT1_OK;
            }
            else if (strstr(atResp, "SIM PIN")) {
                // strcpy(g_cat1.param.pin, "1234");
                if (g_cat1.param.pin[0]) {
                    char cmd[64];
                    snprintf(cmd, sizeof(cmd), "AT+CPIN=%s\r", g_cat1.param.pin);
                    err = cat1_write_at(cmd, atResp, sizeof(atResp),
                                        3000, "OK", "ERROR");
                    LOG_DRV_INFO("Enter PIN: %s => %s \r\n", g_cat1.param.pin, atResp);
                    if (err == CAT1_OK) {
                        strcpy(g_cat1.status.modemStatus, "Ready");
                        return CAT1_OK;
                    }
                }
                return CAT1_FAIL;
            }
        }
        osDelay(1000);
    }
    LOG_DRV_ERROR("SIM PIN check failed!\r\n");
    return CAT1_FAIL;
}

cat1_err_t cat1_set_ip_apn(void)
{
    char now_apn[MAX_LEN_32] = {0};
    char *tmp_ptr1 = NULL;
    char *tmp_ptr2 = NULL;
    char atCmd[128];
    char atResp[256];
    cat1_err_t err;
    
    // Get current APN
    err = cat1_write_at("AT+CGDCONT?\r", atResp, sizeof(atResp), 500, "OK", "ERROR");
    if (err != CAT1_OK) {
        LOG_DRV_ERROR("Get current APN failed: %s \r\n", atResp);
        return err;
    }
    tmp_ptr1 = strstr(atResp, "+CGDCONT: 1,\"IP\",\"");
    if (tmp_ptr1) {
        tmp_ptr1 += strlen("+CGDCONT: 1,\"IP\",\"");
        tmp_ptr2 = strchr(tmp_ptr1, '"');
        if (tmp_ptr2) {
            strncpy(now_apn, tmp_ptr1, tmp_ptr2 - tmp_ptr1);
            LOG_DRV_INFO("Current APN: %s \r\n", now_apn);
        }
    }

    if (g_cat1.param.apn[0] != 0) {
        if (strcmp(now_apn, g_cat1.param.apn) != 0) {
            snprintf(atCmd, sizeof(atCmd),
                    "AT+CGDCONT=1,\"IP\",\"%s\"\r", g_cat1.param.apn);
            err = cat1_write_at(atCmd, atResp, sizeof(atResp), 500, "OK", "ERROR");
            if (err != CAT1_OK) {
                LOG_DRV_ERROR("Set IP APN failed: %s \r\n", atResp);
                return err;
            }
            // Restart network
            cat1_write_at("AT+CFUN=0\r", atResp, sizeof(atResp), 3000, "OK", "ERROR");
            osDelay(1000);
            cat1_write_at("AT+CFUN=1\r", atResp, sizeof(atResp), 3000, "OK", "ERROR");
            osDelay(1000);
            LOG_DRV_INFO("Set IP APN: %s => %s \r\n", now_apn, g_cat1.param.apn);
        }
    } else if (now_apn[0] != 0) strcpy(g_cat1.param.apn, now_apn);
    
    return CAT1_OK;
}

cat1_err_t connect_to_network(void)
{
    char atCmd[128];
    char atResp[256];
    cat1_err_t err;

    // Set APN
    if (g_cat1.param.apn[0]) {
        snprintf(atCmd, sizeof(atCmd),
                "AT+CGDCONT=1,\"IP\",\"%s\"\r", g_cat1.param.apn);
        err = cat1_write_at(atCmd, atResp, sizeof(atResp), 500, "OK", "ERROR");
        if (err != CAT1_OK) {
            LOG_DRV_ERROR("Set IP APN failed: %s \r\n", atResp);
            return err;
        }
        LOG_DRV_INFO("Set IP APN: %s => %s \r\n", g_cat1.param.apn, atResp);
    }

    // Activate PDP context
    err = cat1_write_at("AT+CGACT=1,1\r", atResp, sizeof(atResp), 5000, "OK", "ERROR");
    if (err != CAT1_OK) {
        LOG_DRV_ERROR("Activate IP PDP failed: %s \r\n", atResp);
        return err;
    }
    LOG_DRV_INFO("PDP IP Activate: %s \r\n", atResp);

    // Other necessary configurations...

    return CAT1_OK;
}

cat1_err_t ppp_connect_to_network(void)
{
    char atCmd[128];
    char atResp[256];
    cat1_err_t err;

    // Set APN
    if (g_cat1.param.apn[0]) {
        snprintf(atCmd, sizeof(atCmd),
                "AT+CGDCONT=2,\"PPP\",\"%s\"\r", g_cat1.param.apn);
        err = cat1_write_at(atCmd, atResp, sizeof(atResp), 500, "OK", "ERROR");
        if (err != CAT1_OK) {
            LOG_DRV_ERROR("Set PPP APN failed: %s \r\n", atResp);
            return err;
        }
        LOG_DRV_INFO("Set PPP APN: %s => %s \r\n", g_cat1.param.apn, atResp);
    }

    // Activate PDP context
    err = cat1_write_at("AT+CGACT=1,2\r", atResp, sizeof(atResp), 5000, "OK", "ERROR");
    if (err != CAT1_OK) {
        LOG_DRV_ERROR("Activate PPP PDP failed: %s \r\n", atResp);
        return err;
    }
    LOG_DRV_INFO("PPP PDP Activate: %s \r\n", atResp);

    // Other necessary configurations...

    return CAT1_OK;
}


static cat1_err_t cat1_tcp_connect(const char *host, int port)
{
    char atCmd[128], atResp[256];
    snprintf(atCmd, sizeof(atCmd),
             "AT+QIOPEN=1,0,\"TCP\",\"%s\",%d,0,1\r", host, port);
    cat1_err_t err = cat1_write_at(atCmd, atResp, sizeof(atResp), 5000, "OK", "ERROR");
    if (err != CAT1_OK) return err;

    // Wait for connection result
    err = cat1_write_at(NULL, atResp, sizeof(atResp), 5000, "+QIOPEN:", "ERROR"); // Wait for URC
    if (strstr(atResp, "+QIOPEN: 0,0")) {
        g_cat1.data_mode.conn_id = 0;
        g_cat1.data_mode.is_connected = 1;
        LOG_DRV_INFO("TCP connected: %s:%d\r\n", host, port);
        return CAT1_OK;
    }
    LOG_DRV_ERROR("TCP connect failed: %s\r\n", atResp);
    return CAT1_FAIL;
}

static cat1_err_t cat1_tcp_close(void)
{
    char atResp[128];
    cat1_err_t err = cat1_write_at("AT+QICLOSE=0\r", atResp, sizeof(atResp), 3000, "OK", "ERROR");
    g_cat1.data_mode.is_connected = 0;
    LOG_DRV_INFO("TCP closed\r\n");
    return err;
}

static cat1_err_t cat1_data_send(const uint8_t *data, int len)
{
    char atCmd[32], atResp[256];
    snprintf(atCmd, sizeof(atCmd), "AT+QISEND=0,%d\r", len);
    cat1_err_t err = cat1_write_at(atCmd, atResp, sizeof(atResp), 2000, ">", "ERROR");
    if (err != CAT1_OK) {
        LOG_DRV_ERROR("Enter data send mode failed: %s\r\n", atResp);
        return err;
    }

    SCB_CleanDCache_by_Addr((uint32_t*)data, len);
    // Send data (DMA)
    if (HAL_UART_Transmit_DMA(g_cat1.huart, (uint8_t*)data, len) != HAL_OK) {
        LOG_DRV_ERROR("DMA transmit start failed\r\n");
        return CAT1_FAIL;
    }

    // Wait for DMA completion
    while (HAL_UART_GetState(g_cat1.huart) != HAL_UART_STATE_READY) {
        osDelay(1);
    }

    // Wait for SEND OK
    err = cat1_write_at(NULL, atResp, sizeof(atResp), 3000, "SEND OK", "SEND FAIL");
    if (err == CAT1_OK) {
        LOG_DRV_INFO("Send data OK\r\n");
        return CAT1_OK;
    }
    LOG_DRV_ERROR("Send data failed: %s\r\n", atResp);
    return CAT1_FAIL;
}

__attribute__((unused)) static void cat1_exit_data_mode(void)
{
    // Delay 1s first to ensure safe exit
    osDelay(1000);
    const char *plus = "+++";
    HAL_UART_Transmit(g_cat1.huart, (uint8_t*)plus, 3, 1000);
    osDelay(1000);
    LOG_DRV_INFO("Exit data mode\r\n");
}

static int cat1_at_cmd(int argc, char* argv[])
{
    if (!g_cat1.is_init) return AICAM_ERROR_NOT_INITIALIZED;
    if (argc < 2) {
        LOG_SIMPLE("Usage: cat1at \"AT+CMD\"\r\n");
        return -1;
    }
    char atResp[256] = {0};
    char atCmd[128] = {0};

    // Concatenate all parameters into complete AT command
    int offset = 0;
    for(int i = 1; i < argc; ++i) {
        int n = snprintf(atCmd + offset, sizeof(atCmd) - offset, "%s%s", argv[i], (i < argc - 1) ? " " : "");
        if(n < 0 || n >= (int)(sizeof(atCmd) - offset)) {
            LOG_SIMPLE("AT command too long!\r\n");
            return -1;
        }
        offset += n;
    }

    // Add carriage return at the end
    if(offset < (int)(sizeof(atCmd) - 1)) {
        atCmd[offset++] = '\r';
        atCmd[offset] = '\0';
    } else {
        LOG_SIMPLE("AT command too long!\r\n");
        return -1;
    }

    cat1_err_t err = cat1_write_at(atCmd, atResp, sizeof(atResp), 5000, "OK", "ERROR");
    LOG_SIMPLE("AT Resp: %s\r\n", atResp);

    // if (strstr(atCmd, "QIDNSGIP")) {
    //     err = cat1_write_at(NULL, atResp, sizeof(atResp), 10000, "+QIDNSGIP:", "ERROR");
    //     LOG_SIMPLE("AT Resp: %s\r\n", atResp);
    // }

    return (err == CAT1_OK) ? 0 : -1;
}


static int cat1_status_cmd(int argc, char* argv[])
{
    cellularStatusAttr_t status = {0};
    if (!g_cat1.is_init) return AICAM_ERROR_NOT_INITIALIZED;
    get_status(&status);
    LOG_SIMPLE("ModemStatus: %s\r\n", status.modemStatus);
    LOG_SIMPLE("IMEI: %s\r\n", status.imei);
    LOG_SIMPLE("IMSI: %s\r\n", status.imsi);
    LOG_SIMPLE("ICCID: %s\r\n", status.iccid);
    LOG_SIMPLE("Model: %s\r\n", status.model);
    LOG_SIMPLE("Version: %s\r\n", status.version);
    LOG_SIMPLE("ISP: %s\r\n", status.isp);
    LOG_SIMPLE("Signal: %s\r\n", status.signalLevel);
    return 0;
}

static int cat1_csq_cmd(int argc, char* argv[])
{
    cellularSignalQuality_t sq = {0};
    if (!g_cat1.is_init) return AICAM_ERROR_NOT_INITIALIZED;
    get_signal_quality(&sq);
    LOG_SIMPLE("Signal Quality: %s, RSSI: %d, dBm: %d\r\n", sq.quality, sq.rssi, sq.dbm);
    return 0;
}

static int cat1_tcp_open_cmd(int argc, char* argv[])
{
    if (argc < 3) {
        LOG_SIMPLE("Usage: cat1tcpopen <host> <port>\r\n");
        return -1;
    }
    if (!g_cat1.is_init) return AICAM_ERROR_NOT_INITIALIZED;
    return (cat1_tcp_connect(argv[1], atoi(argv[2])) == CAT1_OK) ? 0 : -1;
}

static int cat1_tcp_send_cmd(int argc, char* argv[])
{
    if (argc < 2) {
        LOG_SIMPLE("Usage: cat1tcpsend <data>\r\n");
        return -1;
    }
    if (!g_cat1.is_init) return AICAM_ERROR_NOT_INITIALIZED;
    return (cat1_data_send((uint8_t*)argv[1], strlen(argv[1])) == CAT1_OK) ? 0 : -1;
}

static int cat1_tcp_close_cmd(int argc, char* argv[])
{
    if (!g_cat1.is_init) return AICAM_ERROR_NOT_INITIALIZED;
    return (cat1_tcp_close() == CAT1_OK) ? 0 : -1;
}

static int cat1_ping_cmd(int argc, char* argv[])
{
    if (argc < 2) {
        LOG_SIMPLE("Usage: cat1ping <host> [timeout] [num]\r\n");
        return -1;
    }
    if (!g_cat1.is_init) return AICAM_ERROR_NOT_INITIALIZED;
    const char *host = argv[1];
    int timeout = (argc > 2) ? atoi(argv[2]) : 4; // Default 4 seconds
    int num = (argc > 3) ? atoi(argv[3]) : 4;     // Default 4 times

    char atCmd[128], atResp[256];
    snprintf(atCmd, sizeof(atCmd), "AT+QPING=1,\"%s\",%d,%d\r", host, timeout, num);

    cat1_err_t err = cat1_write_at(atCmd, atResp, sizeof(atResp), 10000, "+QPING:", "ERROR");
    if (err == CAT1_OK) {
        LOG_SIMPLE("Ping Resp: %s\r\n", atResp);
        // Optional: further parse atResp to display more user-friendly statistics
        return 0;
    } else {
        LOG_SIMPLE("Ping Failed: %s\r\n", atResp);
        return -1;
    }
}

debug_cmd_reg_t cat1_cmd_table[] = {
    {"cat1at",   "Send AT command: cat1at \"AT+XXX\"", cat1_at_cmd},
    {"cat1stat", "Print CAT1 status",                  cat1_status_cmd},
    {"cat1csq",  "Print CAT1 signal quality",          cat1_csq_cmd},
    {"cat1tcpopen", "Open TCP: cat1tcpopen host port", cat1_tcp_open_cmd},
    {"cat1tcpsend", "Send data: cat1tcpsend data", cat1_tcp_send_cmd},
    {"cat1tcpclose", "Close TCP", cat1_tcp_close_cmd},
    {"cat1ping", "Ping host: cat1ping <host> [timeout] [num]", cat1_ping_cmd},
};


void cat1_cmd_register(void)
{
    debug_cmdline_register(cat1_cmd_table, sizeof(cat1_cmd_table) / sizeof(cat1_cmd_table[0]));
}

static void cat1Process(void *argument)
{
    cat1_t *cat1 = (cat1_t *)argument;
    int ret;
    __attribute__((unused)) uint32_t try_times = 0;
    LOG_DRV_INFO("cat1Process start \r\n");
    MX_UART7_Init();
    cat1->huart = (void*)&huart7;
    ret = ATC_Init(&cat1->hAtc, cat1->huart, 512, "CAT1");
    if(ret == false){
        LOG_DRV_ERROR("ATC_Init failed! \r\n");
        goto CAT1_EXIT;
    }

    if (cat1_get_baud_rate() != CAT1_BAUD_RATE) {
        if (cat1_set_baud_rate() != CAT1_OK)
        {
            LOG_DRV_INFO("cat1_set_baud_rate failed, exit cat1Process! \r\n");
            goto CAT1_EXIT;
        }
    }
    LOG_DRV_INFO("CAT1 module starting! \r\n");
    g_cat1.is_opened = true;
    g_cat1.cat1_status = CAT1_STATUS_STARTING;

    check_pin_status();
    cat1_set_ip_apn();

    g_cat1.cat1_status = CAT1_STATUS_STARTED;
    LOG_DRV_INFO("CAT1 module started!\r\n");

    cat1->is_init = true;
    while (cat1->is_init) {
        // ATC_Loop(&cat1->hAtc);
        osDelay(100);
    }
CAT1_EXIT:
    cat1->is_init = false;
    g_cat1.cat1_status = CAT1_STATUS_STOPED;
    osMutexAcquire(cat1->mtx_id, osWaitForever);
    HAL_UART_DeInit(cat1->huart);
    ATC_DeInit(&cat1->hAtc);
    LOG_DRV_INFO("cat1Process exit \r\n");
    // cat1->cat1_processId = NULL;  // Thread cleans up its own ID
    osSemaphoreRelease(cat1->sem_id);
    osThreadExit();
}

static int cat1_ioctl(void *priv, unsigned int cmd, unsigned char* ubuf, unsigned long arg)
{
    cat1_t *cat1 = (cat1_t *)priv;
    int ret = AICAM_OK;
    uint32_t timeout_ms = 0;
    if (!cat1->is_init) return AICAM_ERROR_NOT_INITIALIZED;
    osMutexAcquire(cat1->mtx_id, osWaitForever);

    switch (cmd) {
        case CAT1_CMD_SET_PARAM:
            if (ubuf && arg == sizeof(cellularParamAttr_t)) {
                memcpy(&cat1->param, ubuf, sizeof(cellularParamAttr_t));
            } else {
                ret = AICAM_ERROR_INVALID_PARAM;
            }
            break;
        case CAT1_CMD_GET_PARAM:
            if (ubuf && arg == sizeof(cellularParamAttr_t)) {
                memcpy(ubuf, &cat1->param, sizeof(cellularParamAttr_t));
            } else {
                ret = AICAM_ERROR_INVALID_PARAM;
            }
            break;
        case CAT1_CMD_GET_STATUS:
            if (ubuf && arg == sizeof(cellularStatusAttr_t)) {
                ret = get_status((cellularStatusAttr_t *)ubuf);
                if (ret == CAT1_OK) {
                    memcpy(&cat1->status, ubuf, sizeof(cellularStatusAttr_t));
                } else ret = AICAM_ERROR;
            } else {
                ret = AICAM_ERROR_INVALID_PARAM;
            }
            break;
        case CAT1_CMD_GET_CSQ:
            if (ubuf && arg == sizeof(cellularSignalQuality_t)) {
                ret = get_signal_quality((cellularSignalQuality_t *)ubuf);
                if (ret != CAT1_OK) ret = AICAM_ERROR;
            } else {
                ret = AICAM_ERROR_INVALID_PARAM;
            }
            break;
        case CAT1_CMD_INTO_PPP:
            if (cat1->is_ppp_mode) break;
            if (ubuf == NULL || arg != sizeof(cat1_recv_callback_t)) {
                ret = AICAM_ERROR_INVALID_PARAM;
                break;
            }
            ret = check_pin_status();
            // if (ret == CAT1_OK) ret = ppp_connect_to_network(); // Activate PPP PDP context
            if (ret == CAT1_OK) ret = cat1_set_ip_apn();
            if (ret == CAT1_OK) {
                ret = ATC_SendReceive(&cat1->hAtc, "ATD*99#\r", 200, NULL, 0, 0);
                if (ret == 0) {
                    __HAL_UART_CLEAR_FLAG(((UART_HandleTypeDef *)cat1->huart), 0xFFFFFFFF);
                    HAL_UART_AbortReceive(cat1->huart);
                    cat1->is_ppp_mode = true;
                    cat1->recv_callback = (cat1_recv_callback_t)ubuf;
                }
            }
            if (ret != CAT1_OK) ret = AICAM_ERROR;
            break;
        case CAT1_CMD_EXIT_PPP:
            if (!cat1->is_ppp_mode) break;
            __HAL_UART_CLEAR_FLAG(((UART_HandleTypeDef *)cat1->huart), 0xFFFFFFFF);
            HAL_UART_AbortReceive(cat1->huart);
            ret = HAL_UARTEx_ReceiveToIdle_DMA(cat1->hAtc.hUart, cat1->hAtc.pRxBuff, cat1->hAtc.Size);
            if (ret != HAL_OK) {
                LOG_DRV_ERROR("Cat1 DMA receive failed: %d", ret);
                ret = AICAM_ERROR;
            } else {
                SCB_InvalidateDCache_by_Addr((uint32_t *)(cat1->hAtc.pRxBuff), cat1->hAtc.Size);
                __HAL_DMA_DISABLE_IT(((UART_HandleTypeDef *)cat1->huart)->hdmarx, DMA_IT_HT);
            }
            cat1->is_ppp_mode = false;
            break;
        case CAT1_CMD_PPP_SEND:
            if (cat1->is_ppp_mode && ubuf && arg > 0) {
                SCB_CleanDCache_by_Addr((uint32_t *)ubuf, arg);
                // Send data (DMA)
                ret = HAL_UART_Transmit_DMA(cat1->huart, (uint8_t*)ubuf, arg);
                if (ret != HAL_OK) {
                    LOG_DRV_ERROR("Cat1 DMA transmit failed: %d", ret);
                    ret = AICAM_ERROR;
                } else {
                    // Wait for DMA completion
                    while (1) {
                        osDelay(1);
                        if ((HAL_UART_GetState(cat1->huart) == HAL_UART_STATE_BUSY_RX) || (HAL_UART_GetState(cat1->huart) == HAL_UART_STATE_READY)) {
                            ret = AICAM_OK;
                            break;
                        }
                        if ((HAL_UART_GetState(cat1->huart) == HAL_UART_STATE_ERROR) || (HAL_UART_GetState(cat1->huart) == HAL_UART_STATE_TIMEOUT)) {
                            ret = AICAM_ERROR;
                            break;
                        }
                        timeout_ms++;
                        if (timeout_ms > 1000) {
                            ret = AICAM_ERROR_TIMEOUT;
                            break;
                        }
                    }
                    if (ret != AICAM_OK) {
                        HAL_UART_AbortTransmit(cat1->huart);
                        LOG_DRV_ERROR("Cat1 DMA transmit failed during wait: %d", ret);
                    }
                }
            } else {
                ret = AICAM_ERROR_INVALID_PARAM;
            }
            break;
        case CAT1_CMD_PPP_RECV:
            if (cat1->is_ppp_mode && ubuf && arg > 0) {
                __HAL_UART_CLEAR_FLAG(((UART_HandleTypeDef *)cat1->huart), 0xFFFFFFFF);
                HAL_UART_AbortReceive(cat1->huart);
                ret = HAL_UARTEx_ReceiveToIdle_DMA(cat1->huart, ubuf, arg);
                if (ret == HAL_OK) {
                    SCB_InvalidateDCache_by_Addr((uint32_t*)ubuf, arg);
                    __HAL_DMA_DISABLE_IT(((UART_HandleTypeDef *)cat1->huart)->hdmarx, DMA_IT_HT);
                } else {
                    LOG_DRV_ERROR("Cat1 DMA receive failed: %d", ret);
                    ret = AICAM_ERROR;
                }
            } else {
                ret = AICAM_ERROR_INVALID_PARAM;
            }
            break;
        case CAT1_CMD_USB_ECM_ENABLE:
            ret = cat1_usb_ecm_enable();
            break;
        default:
            ret = AICAM_ERROR_NOT_SUPPORTED;
            break;
    }

    osMutexRelease(cat1->mtx_id);
    return ret;
}

static int cat1_init(void *priv)
{
    LOG_DRV_DEBUG("cat1_init \r\n");
    cat1_t *cat1 = (cat1_t *)priv;
    cat1->mtx_id = osMutexNew(NULL);
    cat1->sem_id = osSemaphoreNew(1, 0, NULL);
    cat1->pwr_handle = pwr_manager_get_handle(PWR_CAT1_NAME);
    pwr_manager_acquire(cat1->pwr_handle);
    osDelay(10);

    cat1->cat1_processId = osThreadNew(cat1Process, cat1, &cat1Task_attributes);
    return 0;
}

static int cat1_deinit(void *priv)
{
    cat1_t *cat1 = (cat1_t *)priv;

    cat1->is_init = false;
    osSemaphoreAcquire(cat1->sem_id, osWaitForever);
    if (cat1->cat1_processId != NULL && osThreadGetId() != cat1->cat1_processId) {
        osThreadTerminate(cat1->cat1_processId);
        cat1->cat1_processId = NULL;
    }

    if (cat1->sem_id != NULL) {
        osSemaphoreDelete(cat1->sem_id);
        cat1->sem_id = NULL;
    }

    if (cat1->mtx_id != NULL) {
        osMutexDelete(cat1->mtx_id);
        cat1->mtx_id = NULL;
    }

    if (cat1->pwr_handle != 0) {
        pwr_manager_release(cat1->pwr_handle);
        cat1->pwr_handle = 0;
    }

    return 0;
}

void cat1_register(void)
{
    static dev_ops_t cat1_ops = {
        .init = cat1_init, 
        .deinit = cat1_deinit, 
        .ioctl = cat1_ioctl
    };
    if (g_cat1.is_init == true){
        return;
    }
    device_t *dev = hal_mem_alloc_fast(sizeof(device_t));
    g_cat1.dev = dev;
    strcpy(dev->name, CAT1_DEVICE_NAME);
    dev->type = DEV_TYPE_NET;
    dev->ops = &cat1_ops;
    dev->priv_data = &g_cat1;

    device_register(g_cat1.dev);
}

void cat1_unregister(void)
{
    if (g_cat1.dev) {
        device_unregister(g_cat1.dev);
        hal_mem_free(g_cat1.dev);
        g_cat1.dev = NULL;
    }
}
