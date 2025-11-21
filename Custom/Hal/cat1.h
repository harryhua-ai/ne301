#ifndef CAT1_H
#define CAT1_H

#include <stddef.h>
#include <stdint.h>
#include "cmsis_os2.h"
#include "dev_manager.h"
#include "pwr.h"
#include "atc.h"
#include "aicam_error.h"

typedef int cat1_err_t;
#define MAX_LEN_32  32
#define MAX_LEN_64  64

#define CAT1_OK                 0
#define CAT1_FAIL               -1
#define CAT1_ERR_TIMEOUT        -2

#define CAT1_BAUD_RATE (921600)  // Adjust baud rate according to module
#define CAT1_POWER_ON_TIMEOUT_MS (30000)
#define CAT1_PPP_CONNECT_TIMEOUT_MS (60000)
#define CAT1_GET_BAUD_RETRY_MAX  2

// Event flags
#define CAT1_POWER_ON_BIT (1 << 0)
#define CAT1_STA_CONNECT_BIT (1 << 1)
#define CAT1_STA_DISCONNECT_BIT (1 << 2)

typedef enum {
    CAT1_CMD_SET_PARAM      = CAT1_CMD_BASE,
    CAT1_CMD_GET_PARAM,
    CAT1_CMD_GET_STATUS,
    CAT1_CMD_GET_CSQ,
    CAT1_CMD_INTO_PPP,
    CAT1_CMD_EXIT_PPP,
    CAT1_CMD_PPP_SEND,
    CAT1_CMD_PPP_RECV,
    CAT1_CMD_USB_ECM_ENABLE,
} CAT1_CMD_E;

typedef struct {
    int conn_id;        // Connection ID
    int is_connected;   // Whether connected
    char remote_host[64];
    int remote_port;
} cat1_data_mode_t;

typedef enum cat1Status {
    CAT1_STATUS_STOPED = 0,    ///< Module stopped
    CAT1_STATUS_STARTING,      ///< UART communication established
    CAT1_STATUS_STARTED,       ///< Dial-up completed (network connection unknown)
} cat1Status_e;

typedef struct cellularParamAttr {
    // Non-configurable parameters
    char imei[MAX_LEN_32];
    // Configurable parameters
    char apn[MAX_LEN_32];
    char user[MAX_LEN_64];
    char password[MAX_LEN_64];
    char pin[MAX_LEN_32];
    uint8_t authentication;
} cellularParamAttr_t;

typedef struct cellularSignalQuality {
    int rssi;                ///< Received Signal Strength Indicator
    int ber;                 ///< Bit Error Rate
    int dbm;                 ///< Signal power in dBm
    int asu;                 ///< Arbitrary Strength Unit
    int level;               ///< Signal level (0-5)
    char quality[MAX_LEN_64]; ///< Signal quality description
} cellularSignalQuality_t;

typedef struct cellularStatusAttr {
    char networkStatus[MAX_LEN_64];   ///< Current network status
    char modemStatus[MAX_LEN_64];     ///< Modem operational status
    char model[MAX_LEN_64];           ///< Modem model
    char version[MAX_LEN_64];         ///< Firmware version
    char signalLevel[MAX_LEN_64];     ///< Signal strength level
    char registerStatus[MAX_LEN_64];  ///< Network registration status
    char imei[MAX_LEN_64];            ///< IMEI number
    char imsi[MAX_LEN_64];            ///< IMSI number
    char iccid[MAX_LEN_64];           ///< SIM ICCID
    char isp[MAX_LEN_64];             ///< Network provider
    char networkType[MAX_LEN_64];     ///< Connected network type
    char plmnId[MAX_LEN_64];          ///< PLMN ID
    char lac[MAX_LEN_64];             ///< Location Area Code
    char cellId[MAX_LEN_64];          ///< Cell ID
    char ipv4Address[MAX_LEN_64];     ///< IPv4 address
    char ipv4Gateway[MAX_LEN_64];     ///< IPv4 gateway
    char ipv4Dns[MAX_LEN_64];         ///< IPv4 DNS
    char ipv6Address[MAX_LEN_64];     ///< IPv6 address
    char ipv6Gateway[MAX_LEN_64];     ///< IPv6 gateway
    char ipv6Dns[MAX_LEN_64];         ///< IPv6 DNS
} cellularStatusAttr_t;

typedef void (*cat1_recv_callback_t)(void *handle, uint16_t len);

typedef struct {
    bool is_init;
    device_t *dev;
    osMutexId_t mtx_id;
    osSemaphoreId_t sem_id;
    osThreadId_t cat1_processId;
    ATC_HandleTypeDef hAtc;
    PowerHandle     pwr_handle;

    int mode;
    bool is_ppp_mode;
    bool is_opened;
    bool is_restarting;
    cat1Status_e cat1_status;
    osEventFlagsId_t event_group;
    cellularParamAttr_t param;
    cellularStatusAttr_t status;
    cat1_data_mode_t data_mode;
    void *huart;
    cat1_recv_callback_t recv_callback;
} cat1_t;

int cat1_ppp_enable_recv_isr(uint8_t *buf, uint16_t len);
void cat1_cmd_register(void);
void cat1_register(void);
void cat1_unregister(void);

#endif