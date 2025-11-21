#include <stdint.h>
#include <stdio.h>
#include "pir.h"
#include "main.h"
#include "debug.h"
#include "exti.h"
#include "tx_user.h"
#include "aicam_error.h"

// Read mode sensor configuration values
uint8_t SENS_C = 0x0f; //[7:0] Sensitivity setting, recommended to set greater than 20. If the environment has no interferpire, it can be set to a minimum value of 10. The smaller the value, the more sensitive, but the easier it is to trigger false alarms. (Effective in interrupt mode, ineffective in read mode)
uint8_t BLIND_C= 0x03; //[3:0] The time to ignore motion detection after the interrupt output switches back to 0, range: 0.5s ~ 8s, interrupt time = register value * 0.5s + 0.5s. (Effective in interrupt mode, ineffective in read mode)
uint8_t PULSE_C= 0x01; //[1:0] Pulse counter, the number of pulses required to be reached within the window time. Range: 1 ~ 4 signed pulses, pulse count = register value + 1. The larger the value, the stronger the anti-interferpire ability, but the sensitivity is slightly reduced. (Effective in interrupt mode, ineffective in read mode)
uint8_t WINDOW_C= 0x00; //[1:0] Window time, range: 2s~8s, window time = register value * 2s + 2s. (Effective in interrupt mode, ineffective in read mode)
uint8_t MOTION_C =0x01; //[0] Must be 1
uint8_t INT_C= 0x00; // Interrupt source. 0 = motion detection, 1 = raw data from the filter. Read mode must be set to 1.
uint8_t VOLT_C =0x00; //[1:0] Multiplex ADC resources. The input sources selectable for the ADC are as follows: PIR signal BFP output = 0; PIR signal LPF output = 1; power supply voltage = 2; temperature sensor = 3; choose as needed
uint8_t SUPP_C = 0x00; // Set to 0
uint8_t RSV_C = 0x00; // Set to 0

static uint8_t SENS_W, BLIND_W, PULSE_W, WINDOW_W, MOTION_W, INT_W, VOLT_W, SUPP_W, RSV_W;
static uint8_t PIR_OUT, DATA_H, DATA_L, SENS_R, BLIND_R, PULSE_R, WINDOW_R, MOTION_R, INT_R;
static uint8_t VOLT_R, SUPP_R, RSV_R, BUF1;

static pir_t g_pir;

void Delay_us(uint32_t us)
{
    volatile uint32_t n = (SYSTEM_CLOCK / 1000000 / 4) * us; // 4 is an empirical value
    while(n--) { __NOP(); }
}

static void pir_serialIn_init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = PIR_Serial_IN_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(PIR_Serial_IN_GPIO_Port, &GPIO_InitStruct);
}

static void pir_serialIn_set(uint8_t value)
{
    if(value == 0){
        HAL_GPIO_WritePin(PIR_Serial_IN_GPIO_Port, PIR_Serial_IN_Pin, GPIO_PIN_RESET);
    }else{
        HAL_GPIO_WritePin(PIR_Serial_IN_GPIO_Port, PIR_Serial_IN_Pin, GPIO_PIN_SET);
    }
}

static void pir_do_in()
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = PIR_INT_OUT_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(PIR_INT_OUT_GPIO_Port, &GPIO_InitStruct);
}

static void pir_do_out()
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = PIR_INT_OUT_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(PIR_INT_OUT_GPIO_Port, &GPIO_InitStruct);
}

/* Need to disable info log, printing log will consume time and cause read timing errors */
static void pir_do_set(uint8_t value)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    if(value == 0){
        HAL_GPIO_WritePin(PIR_Serial_IN_GPIO_Port, PIR_Serial_IN_Pin, GPIO_PIN_RESET);
    }else{
        HAL_GPIO_WritePin(PIR_Serial_IN_GPIO_Port, PIR_Serial_IN_Pin, GPIO_PIN_SET);
    }

    GPIO_InitStruct.Pin = PIR_INT_OUT_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    if(value == 1){
        GPIO_InitStruct.Pull = GPIO_PULLUP;
    }else{
        GPIO_InitStruct.Pull = GPIO_PULLDOWN;
    }
    HAL_GPIO_Init(PIR_INT_OUT_GPIO_Port, &GPIO_InitStruct);
}

static void pir_int_set(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = PIR_INT_OUT_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(PIR_INT_OUT_GPIO_Port, &GPIO_InitStruct);
}

static int gpio_direct_read() 
{
    return (int)HAL_GPIO_ReadPin(PIR_INT_OUT_GPIO_Port, PIR_INT_OUT_Pin);
}

//======== Write NBIT subroutine ====================================
static void W_DATA(uint8_t num)
{
    char i;
    for(i=num ;i>0;i--)
    {   
        pir_serialIn_set(0);
        Delay_us(2); // Delay must be accurate, total 2us
        pir_serialIn_set(1);
        Delay_us(2); // Delay must be accurate, total 2us

        if(BUF1 & 0x80){
            pir_serialIn_set(1);
        }else{
            pir_serialIn_set(0);
        }
        Delay_us(100); // Delay must be accurate, total 100us
        BUF1 = BUF1 << 1;
    }
}

//====== Write config to IC ==========================
static void CONFIG_W()
{
    BUF1 = SENS_W;
    W_DATA(8);
    BUF1 = BLIND_W;
    BUF1 = BUF1 << 0x04;
    W_DATA(4);
    BUF1 = PULSE_W;
    BUF1 = BUF1 << 0x06;
    W_DATA(2);
    BUF1 = WINDOW_W;
    BUF1 = BUF1 << 0x06;
    W_DATA(2);
    BUF1 = MOTION_W;
    BUF1 = BUF1 << 0x07;
    W_DATA(1);
    BUF1 = INT_W;
    BUF1 = BUF1 << 0x07;
    W_DATA(1);
    BUF1 = VOLT_W;
    BUF1 = BUF1 << 0x06;
    W_DATA(2);
    BUF1 = SUPP_W;
    BUF1 = BUF1 << 0x07;
    W_DATA(1);

    BUF1 = 0;
    BUF1 = BUF1 << 0x07;
    W_DATA(1);

    BUF1 = 1;
    BUF1 = BUF1 << 0x07;
    W_DATA(1);

    BUF1 = 0;
    BUF1 = BUF1 << 0x07;
    W_DATA(1);

    BUF1 = 0;
    BUF1 = BUF1 << 0x07;
    W_DATA(1);

    pir_serialIn_set(0);
    Delay_us(1000);
}

//======= Initialize sensor configuration parameters ==============================
static void CONFIG_INI()
{
    SENS_W = SENS_C;  
    BLIND_W = BLIND_C;
    PULSE_W = PULSE_C;
    WINDOW_W = WINDOW_C;
    MOTION_W = MOTION_C;
    INT_W = INT_C;
    VOLT_W = VOLT_C;
    SUPP_W = SUPP_C;
    RSV_W = RSV_C;
}

//====== Read Nbit ====================
static void RD_NBIT(uint8_t num)
{
    uint8_t i;
    BUF1 = 0x00;
    
    for(i=0;i<num;i++){
        pir_do_set(0);
        Delay_us(2);

        pir_do_set(1);
        Delay_us(2);
        pir_do_in();
        BUF1 = BUF1 << 1;
        if(gpio_direct_read() != 0x00u){
            BUF1 = BUF1 + 1;
        }else{
            BUF1 = BUF1 + 0;
        }
    }
    return;
}

//======= Read end clear subroutine ==================
static void RD_END()
{
    pir_do_out();
    pir_do_set(0);
    Delay_us(200); // Delay must be accurate, total 200us
    pir_do_in();
}

//===== Force DOCI interrupt subroutine ===============
static void F_INT()
{
    pir_do_out();
    pir_do_set(1);
    Delay_us(200); // Delay must be accurate, total 200us
}

//===== DOCI read out =======================
static void RD_DOCI()
{
    F_INT();

    PIR_OUT = 0;
    RD_NBIT(1);
    PIR_OUT = BUF1;

    DATA_H = 0x00;
    RD_NBIT(6);
    DATA_H = BUF1;

    DATA_L = 0x00;
    RD_NBIT(8);
    DATA_L = BUF1;

    SENS_R = 0x00;
    RD_NBIT(8);
    SENS_R = BUF1;

    BLIND_R = 0x00;
    RD_NBIT(4);
    BLIND_R = BUF1;

    PULSE_R = 0x00;
    RD_NBIT(2);
    PULSE_R = BUF1;

    WINDOW_R = 0x00;
    RD_NBIT(2);
    WINDOW_R = BUF1;

    MOTION_R = 0x00;
    RD_NBIT(1);
    MOTION_R = BUF1;

    INT_R = 0x00;
    RD_NBIT(1);
    INT_R = BUF1;

    VOLT_R = 0x00;
    RD_NBIT(2);
    VOLT_R = BUF1;

    SUPP_R = 0x00;
    RD_NBIT(1);
    SUPP_R = BUF1;

    RSV_R = 0x00;
    RD_NBIT(4);
    RSV_R = BUF1;

    RD_END(); // Read end clear subroutine
    // printf("PIR_OUT:%x\r\n", PIR_OUT); printf("DATA_H:%x\r\n", DATA_H); printf("DATA_L:%x\r\n", DATA_L); printf("SENS_R:%x\r\n", SENS_R);
    // printf("BLIND_R:%x\r\n", BLIND_R); printf("PULSE_R:%x\r\n", PULSE_R); printf("WINDOW_R:%x\r\n", WINDOW_R); printf("MOTION_R:%x\r\n", MOTION_R);
    // printf("INT_R:%x\r\n", INT_R); printf("VOLT_R:%x\r\n", VOLT_R); printf("SUPP_R:%x\r\n", SUPP_R); printf("RSV_R:%x\r\n", RSV_R);
}

//==== Configuration IC write and read check ==============
static uint8_t CFG_CHK()
{
    pir_serialIn_init();
    pir_serialIn_set(0);
    pir_do_out();
    pir_do_set(0);
    osDelay(1);
    CONFIG_INI(); // Initialize sensor configuration parameters
    CONFIG_W(); // Write config to IC
    osDelay(25); // Delay
    RD_DOCI(); // Read data
    // Check if the write is correct
    if(SENS_W != SENS_R)
    { return 1; }
    else if(BLIND_W != BLIND_R)
    { return 2; }
    else if(PULSE_W != PULSE_R)
    { return 3; }
    else if(WINDOW_W != WINDOW_R)
    { return 4; }
    else if(MOTION_W != MOTION_R)
    { return 5; }
    else if(INT_W != INT_R)
    { return 6; }
    else if(VOLT_W != VOLT_R)
    { return 7; }
    else if(SUPP_W != SUPP_R)
    { return 8; }

    return 0;
}

void pir_int_trigger(void)
{
    if(!g_pir.is_init)
        return;

    pir_do_out();
    pir_do_set(0);
    LOG_DRV_DEBUG("------pir int trigger--- \r\n");
    if(g_pir.cb != NULL){
        g_pir.cb();
    }
    pir_int_set();
}

static int pir_ioctl(void *priv, unsigned int cmd, unsigned char* ubuf, unsigned long arg)
{
    pir_t *pir = (pir_t *)priv;
    int ret = AICAM_OK;
    if(!pir->is_init)
        return AICAM_ERROR_NOT_FOUND;
    osMutexAcquire(pir->mtx_id, osWaitForever);
    switch(cmd){
        case PIR_CMD_SET_CB:
            if(ubuf == NULL){
                ret = AICAM_ERROR_INVALID_PARAM;
                break;
            }
            pir->cb = (void (*)(void))ubuf;
            break;
        default:
            ret = AICAM_ERROR_NOT_SUPPORTED;
            break;
    }
    osMutexRelease(pir->mtx_id);
    return ret;
}

static int pir_start(void *priv)
{
    int err, i;
    pir_t *pir = (pir_t *)priv;
    int ret = AICAM_OK;
    pwr_manager_acquire(pir->pwr_handle);
    for(i = 0; i < PIR_INIT_RETRY; i++){
        err = CFG_CHK();
        if(err == 0){
            /* EXTI interrupt init*/
            pir_int_set();
            HAL_NVIC_SetPriority(EXTI8_IRQn, 5, 0);
            HAL_NVIC_EnableIRQ(EXTI8_IRQn);
            ret = AICAM_OK;
            break;
        }else{
            LOG_DRV_DEBUG("pir_start err:%d retry:%d\r\n", err, i);
            ret = AICAM_ERROR;
        }
    }
    return ret;
}

static int pir_stop(void *priv)
{
    pir_t *pir = (pir_t *)priv;
    HAL_NVIC_DisableIRQ(EXTI8_IRQn);
    pwr_manager_release(pir->pwr_handle);
    return AICAM_OK;
}   

static int pir_init(void *priv)
{
    LOG_DRV_DEBUG("pir_init \r\n");
    pir_t *pir = (pir_t *)priv;
    pir->mtx_id = osMutexNew(NULL);
    pir->pwr_handle = pwr_manager_get_handle(PWR_PIR_NAME);

    /*Configure the EXTI line attribute */
    HAL_EXTI_ConfigLineAttributes(EXTI_LINE_8, EXTI_LINE_SEC);
    exti8_irq_register(pir_int_trigger);
    pir->is_init = true;
    return AICAM_OK;
}

static int pir_deinit(void *priv)
{
    pir_t *pir = (pir_t *)priv;

    pir->is_init = false;

    if (pir->mtx_id != NULL) {
        osMutexDelete(pir->mtx_id);
        pir->mtx_id = NULL;
    }

    if (pir->pwr_handle != 0) {
        pwr_manager_release(pir->pwr_handle);
        pir->pwr_handle = 0;
    }

    return AICAM_OK;
}

int pir_register(void)
{
    static dev_ops_t pir_ops = {
    .init = pir_init, 
    .deinit = pir_deinit,
    .start = pir_start,
    .stop = pir_stop,
    .ioctl = pir_ioctl
    };
    if(g_pir.is_init == true){
        return AICAM_ERROR_BUSY;
    }
    device_t *dev = hal_mem_alloc_fast(sizeof(device_t));
    g_pir.dev = dev;
    strcpy(dev->name, PIR_DEVICE_NAME);
    dev->type = DEV_TYPE_MISC;
    dev->ops = &pir_ops;
    dev->priv_data = &g_pir;

    device_register(g_pir.dev);
    return AICAM_OK;
}

int pir_unregister(void)
{
    if (g_pir.dev) {
        device_unregister(g_pir.dev);
        hal_mem_free(g_pir.dev);
        g_pir.dev = NULL;
    }
    return AICAM_OK;
}