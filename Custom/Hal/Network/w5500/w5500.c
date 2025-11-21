#include <string.h>
#include "exti.h"
#include "w5500.h"

/***********Interface to be implemented**********/
#if W5500_IS_USE_RTOS == 1
#include "FreeRTOS.h"
#include "semphr.h"
static SemaphoreHandle_t w5500_lock_semper = NULL;
#else
static uint8_t w5500_lock_semper = 0;
#endif
/// @brief Lock mutex
/// @param None
/// @return Error code
static int w5500_lock_mutex(void)
{
#if W5500_IS_USE_RTOS == 1
    if (w5500_lock_semper == NULL) w5500_lock_semper = xSemaphoreCreateMutex();
    if (w5500_lock_semper == NULL) return W5500_ERR_MEM;

    if (xSemaphoreTake(w5500_lock_semper, pdMS_TO_TICKS(W5500_LOCK_TIMEOUT)) == pdPASS) return W5500_OK;
#else
    uint32_t timeout_ms = 0;

    while (timeout_ms++ < DNS_LOCK_TIMEOUT) {
        if (w5500_lock_semper == 0) {
            w5500_lock_semper = 1;
            return W5500_OK;
        }
        if (timeout_ms < DNS_LOCK_TIMEOUT) W5500_DELAY_MS(1);
    }
#endif

    return W5500_ERR_TIMEOUT;
}
/// @brief Unlock mutex
/// @param None
/// @return Error code
static int w5500_unlock_mutex(void)
{
#if W5500_IS_USE_RTOS == 1
    if (w5500_lock_semper == NULL) w5500_lock_semper = xSemaphoreCreateMutex();
    if (w5500_lock_semper == NULL) return W5500_ERR_MEM;

    if (xSemaphoreGive(w5500_lock_semper) == pdPASS) return W5500_OK;
#else
    w5500_lock_semper = 1;
    return W5500_OK;
#endif

    return W5500_ERR_FAILED;
}
/// @brief SPI send and read one byte
/// @param byte Byte to send
/// @return Byte read
uint8_t W5500_Spi_ReadWrite(uint8_t byte)
{
    return SPI2_ReadWriteByte(byte);
}
/// @brief SPI send data function
/// @param sbuf Data to send
/// @param slen Send length
/// @param timeout Timeout (unit: ms)
/// @return Less than 0: error code, otherwise: actual sent data length
int W5500_Spi_Send(uint8_t *sbuf, uint16_t slen, uint32_t timeout)
{
    int status = HAL_OK;

    status = SPI2_WriteBytes(sbuf, slen, timeout);
    if (status == HAL_OK) return slen;
    else if (status == HAL_TIMEOUT) return 0;
    else return W5500_ERR_SPI_FAILED;
}
/// @brief SPI receive data function
/// @param rbuf Receive buffer
/// @param rlen Receive length
/// @param timeout Timeout (unit: ms)
/// @return Less than 0: error code, otherwise: actual received data length
int W5500_Spi_Recv(uint8_t *rbuf, uint16_t rlen, uint32_t timeout)
{
    int status = HAL_OK;

    status = SPI2_ReadBytes(rbuf, rlen, timeout);
    if (status == HAL_OK) return rlen;
    else if (status == HAL_TIMEOUT) return 0;
    else return W5500_ERR_SPI_FAILED;
}
/// @brief Initialize low-level interface and related pins
/// @param None
void W5500_Bsp_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    W5500_RSTn_CLK_ENABLE();
    GPIO_InitStruct.Pin = W5500_GPIO_RSTn_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(W5500_GPIO_RSTn_PORT, &GPIO_InitStruct);
    W5500_GPIO_RST_HIGH();

    W5500_CSn_CLK_ENABLE();
    GPIO_InitStruct.Pin = W5500_GPIO_CSn_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(W5500_GPIO_CSn_PORT, &GPIO_InitStruct);
    W5500_GPIO_CSn_HIGH();

    W5500_INTn_CLK_ENABLE();
    GPIO_InitStruct.Pin = W5500_GPIO_INTn_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(W5500_GPIO_INTn_PORT, &GPIO_InitStruct);
    
    MX_SPI2_Init();
}
/// @brief Deinitialize low-level interface and related pins
/// @param None
void W5500_Bsp_DeInit(void)
{
    HAL_GPIO_DeInit(W5500_GPIO_RSTn_PORT, W5500_GPIO_RSTn_PIN);
    HAL_GPIO_DeInit(W5500_GPIO_INTn_PORT, W5500_GPIO_INTn_PIN);
    HAL_GPIO_DeInit(W5500_GPIO_CSn_PORT, W5500_GPIO_CSn_PIN);
    HAL_SPI_DeInit(&hspi2);
}
/*************************************************/
/// @brief W5500 default configuration
const w5500_config_t w5500_default_config = {
    {0x00, 0x24, 0x03, 0x14, 0x22, 0x03},
    {255, 255, 255, 0},
    {192, 168, 1, 1},
    {192, 168, 1, 100},
    2000,
    3,
    {2, 2, 2, 2, 2, 2, 2, 2},
    {2, 2, 2, 2, 2, 2, 2, 2},
};
/// @brief W5500 configure network information
/// @param ip IP address
/// @param gw Gateway address
/// @param sub Subnet mask
/// @return Error code
int W5500_Cfg_Net(uint8_t *ip, uint8_t *gw, uint8_t *sub)
{
    int recode = W5500_OK;
    uint8_t tmp_buf[4] = {0};
    if (ip == NULL || gw == NULL || sub == NULL) return W5500_ERR_INVALID_ARG; 

    recode = W5500_Set_IP(ip);
    if (recode < 0) return recode;
    recode = W5500_Set_GW(gw);
    if (recode < 0) return recode;
    recode = W5500_Set_SUB(sub);
    if (recode < 0) return recode;

    recode = W5500_Get_IP(tmp_buf);
    if (recode < 0) return recode;
    if (memcmp(ip, tmp_buf, 4) != 0) return W5500_ERR_CHECK;
    recode = W5500_Get_GW(tmp_buf);
    if (recode < 0) return recode;
    if (memcmp(gw, tmp_buf, 4) != 0) return W5500_ERR_CHECK;
    recode = W5500_Get_SUB(tmp_buf);
    if (recode < 0) return recode;
    if (memcmp(sub, tmp_buf, 4) != 0) return W5500_ERR_CHECK;

    return recode;
}
/// @brief Initialize W5500
/// @param cfg Configuration
/// @return Error code
int W5500_Init(w5500_config_t *cfg)
{
    int recode = W5500_OK, i = 0;
    uint8_t tmp_buf[6] = {0}, phy_cfg = 0;
    if (cfg == NULL) cfg = (w5500_config_t *)&w5500_default_config;

    // Initialize hardware peripherals
    W5500_Bsp_Init();

    // Hardware reset
    W5500_GPIO_RST_LOW();
    W5500_DELAY_MS(1);
    W5500_GPIO_RST_HIGH();
    W5500_DELAY_MS(1);

    // Software reset
    recode = W5500_Set_MR(MR_RST);
    if (recode < 0) return recode;
    W5500_DELAY_MS(1);
    // Configure mode
    phy_cfg = 0xf8;
    recode = W5500_Write_Datas(PHYCFGR, &phy_cfg, 1, W5500_SPI_LESS_10B_TIMEOUT);
    if (recode < 0) return recode;
    // Set timeout
    recode = W5500_Set_RTR(cfg->rtr);
    if (recode < 0) return recode;
    recode = W5500_Set_RCR(cfg->rcr);
    if (recode < 0) return recode;
    // Set MAC address
    recode = W5500_Set_MAC(cfg->mac);
    if (recode < 0) return recode;
    recode = W5500_Get_MAC(tmp_buf);
    if (recode < 0) return recode;
    if (memcmp(cfg->mac, tmp_buf, 6) != 0) return W5500_ERR_CHECK;
    // Set network information
    recode = W5500_Cfg_Net(cfg->ip, cfg->gw, cfg->sub);
    if (recode < 0) return recode;
    // Set memory size
    for (; i < W5500_SOCK_MAX_NUM; i++) {
        recode = W5500_Write_Datas(Sn_TXMEM_SIZE(i), &(cfg->tx_size[i]), 1, W5500_SPI_LESS_10B_TIMEOUT);
        if (recode < 0) return recode;
        recode = W5500_Read_Datas(Sn_TXMEM_SIZE(i), tmp_buf, 1, W5500_SPI_LESS_10B_TIMEOUT);
        if (recode < 0) return recode;
        if (cfg->tx_size[i] != tmp_buf[0]) return W5500_ERR_CHECK;
        recode = W5500_Write_Datas(Sn_RXMEM_SIZE(i), &(cfg->rx_size[i]), 1, W5500_SPI_LESS_10B_TIMEOUT);
        if (recode < 0) return recode;
        recode = W5500_Read_Datas(Sn_RXMEM_SIZE(i), tmp_buf, 1, W5500_SPI_LESS_10B_TIMEOUT);
        if (recode < 0) return recode;
        if (cfg->rx_size[i] != tmp_buf[0]) return W5500_ERR_CHECK;
    }

    return W5500_OK;
}

/// @brief Enable interrupt
/// @param callback Interrupt callback 
void W5500_Enable_Interrupt(void (*callback)(void))
{
    HAL_EXTI_ConfigLineAttributes(EXTI_LINE_15, EXTI_LINE_SEC);

    HAL_NVIC_SetPriority(EXTI15_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(EXTI15_IRQn);

    exti15_irq_register(callback);
}

/// @brief Disable interrupt
/// @param None
void W5500_Disable_Interrupt(void)
{
    HAL_NVIC_DisableIRQ(EXTI15_IRQn);
}

/// @brief Deinitialize W5500
/// @param None
void W5500_DeInit(void)
{
    W5500_Bsp_DeInit();
}
/// @brief Write data to specified register address
/// @param addr_bsb Register address and BSB configuration
/// @param wbuf Data to write
/// @param wlen Write length
/// @param timeout Timeout (unit: ms)
/// @return Less than 0: error code, otherwise: actual sent data length
int W5500_Write_Datas(uint32_t addr_bsb, uint8_t *wbuf, uint16_t wlen, uint32_t timeout)
{
    int recode = 0;
    uint8_t tmp_buf[3] = {0};
    uint16_t i = 0;
    if (wbuf == NULL || wlen == 0) return W5500_ERR_INVALID_ARG;
    // TODO: Disable interrupt and acquire mutex
    if (w5500_lock_mutex() != W5500_OK) {
        W5500_LOGE("W5500_Write_Datas Lock take failed!");
        return W5500_ERR_INVALID_STATE;
    }

    W5500_GPIO_CSn_LOW();

    tmp_buf[0] = (addr_bsb >> 16) & 0xff;
    tmp_buf[1] = (addr_bsb >> 8) & 0xff;
    tmp_buf[2] = (addr_bsb & 0xf8) | 0x04;

    if (wlen < 16) {
        W5500_Spi_ReadWrite(tmp_buf[0]);
        W5500_Spi_ReadWrite(tmp_buf[1]);
        W5500_Spi_ReadWrite(tmp_buf[2]);
        for (; i < wlen; i++) W5500_Spi_ReadWrite(wbuf[i]);
        recode = wlen;
    } else {
        recode = W5500_Spi_Send(tmp_buf, 3, W5500_SPI_LESS_10B_TIMEOUT);
        if (recode != 3) {
            W5500_LOGE("W5500_Spi_Send Failed(recode = %d)!", recode);
            if (recode == 0) recode = W5500_ERR_TIMEOUT;
            else if (recode < 0) recode = W5500_ERR_SPI_FAILED;
            else recode = W5500_ERR_INVALID_SIZE;
        } else {
            recode = W5500_Spi_Send(wbuf, wlen, timeout);
            if (recode != wlen) {
                W5500_LOGE("W5500_Spi_Send Failed(recode = %d)!", recode);
                if (recode == 0) recode = W5500_ERR_TIMEOUT;
                else if (recode < 0) recode = W5500_ERR_SPI_FAILED;
                else recode = W5500_ERR_INVALID_SIZE;
            }
        }
    }

    W5500_GPIO_CSn_HIGH();
    // TODO: Enable interrupt and release mutex
    w5500_unlock_mutex();
    return recode;
}
/// @brief Read data from specified register address
/// @param addr_bsb Register address and BSB configuration
/// @param rbuf Read buffer
/// @param rlen Read length
/// @param timeout Timeout (unit: ms)
/// @return Less than 0: error code, otherwise: actual received data length
int W5500_Read_Datas(uint32_t addr_bsb, uint8_t *rbuf, uint16_t rlen, uint32_t timeout)
{
    int recode = 0;
    uint8_t tmp_buf[3] = {0};
    uint16_t i = 0;
    if (rbuf == NULL || rlen == 0) return W5500_ERR_INVALID_ARG;
    // TODO: Disable interrupt and acquire mutex
    if (w5500_lock_mutex() != W5500_OK) {
        W5500_LOGE("W5500_Read_Datas Lock take failed!");
        return W5500_ERR_INVALID_STATE;
    }

    W5500_GPIO_CSn_LOW();
    
    tmp_buf[0] = (addr_bsb >> 16) & 0xff;
    tmp_buf[1] = (addr_bsb >> 8) & 0xff;
    tmp_buf[2] = addr_bsb & 0xf8;

    if (rlen < 16) {
        W5500_Spi_ReadWrite(tmp_buf[0]);
        W5500_Spi_ReadWrite(tmp_buf[1]);
        W5500_Spi_ReadWrite(tmp_buf[2]);
        for (; i < rlen; i++) rbuf[i] = W5500_Spi_ReadWrite(0xff);
        recode = rlen;
    } else {
        recode = W5500_Spi_Send(tmp_buf, 3, W5500_SPI_LESS_10B_TIMEOUT);
        if (recode != 3) {
            W5500_LOGE("W5500_Spi_Send Failed(recode = %d)!", recode);
            if (recode == 0) recode = W5500_ERR_TIMEOUT;
            else if (recode < 0) recode = W5500_ERR_SPI_FAILED;
            else recode = W5500_ERR_INVALID_SIZE;
        } else {
            recode = W5500_Spi_Recv(rbuf, rlen, timeout);
            if (recode != rlen) {
                W5500_LOGE("W5500_Spi_Recv Failed(recode = %d)!", recode);
                if (recode == 0) recode = W5500_ERR_TIMEOUT;
                else if (recode < 0) recode = W5500_ERR_SPI_FAILED;
                else recode = W5500_ERR_INVALID_SIZE;
            }
        }
    }

    W5500_GPIO_CSn_HIGH();
    // TODO: Enable interrupt and release mutex
    w5500_unlock_mutex();
    return recode;
}
/// @brief Set MAC address
/// @param mac MAC address
/// @return Error code
int W5500_Set_MAC(uint8_t *mac)
{
    int recode = W5500_OK;
    if (mac == NULL) return W5500_ERR_INVALID_ARG;

    W5500_LOGD("SET MAC: %s", ETH_TOOL_GET_MAC_STR(mac));
    recode = W5500_Write_Datas(SHAR0, mac, 6, W5500_SPI_LESS_10B_TIMEOUT);
    if (recode < 0) return recode;

    return W5500_OK;
}
/// @brief Set subnet mask
/// @param sub Subnet mask
/// @return Error code
int W5500_Set_SUB(uint8_t *sub)
{
    int recode = W5500_OK;
    if (sub == NULL) return W5500_ERR_INVALID_ARG;

    W5500_LOGD("SET SUB: %s", ETH_TOOL_GET_IP_STR(sub));
    recode = W5500_Write_Datas(SUBR0, sub, 4, W5500_SPI_LESS_10B_TIMEOUT);
    if (recode < 0) return recode;

    return W5500_OK;
}
/// @brief Set gateway address
/// @param gw Gateway
/// @return Error code
int W5500_Set_GW(uint8_t *gw)
{
    int recode = W5500_OK;
    if (gw == NULL) return W5500_ERR_INVALID_ARG;

    W5500_LOGD("SET GW: %s", ETH_TOOL_GET_IP_STR(gw));
    recode = W5500_Write_Datas(GAR0, gw, 4, W5500_SPI_LESS_10B_TIMEOUT);
    if (recode < 0) return recode;
    
    return W5500_OK;
}
/// @brief Set IP address
/// @param ip IP address
/// @return Error code
int W5500_Set_IP(uint8_t *ip)
{
    int recode = W5500_OK;
    if (ip == NULL) return W5500_ERR_INVALID_ARG;

    W5500_LOGD("SET IP: %s", ETH_TOOL_GET_IP_STR(ip));
    recode = W5500_Write_Datas(SIPR0, ip, 4, W5500_SPI_LESS_10B_TIMEOUT);
    if (recode < 0) return recode;
    
    return W5500_OK;
}
/// @brief Get MAC address
/// @param mac MAC address
/// @return Error code
int W5500_Get_MAC(uint8_t *mac)
{
    int recode = W5500_OK;
    if (mac == NULL) return W5500_ERR_INVALID_ARG;

    recode = W5500_Read_Datas(SHAR0, mac, 6, W5500_SPI_LESS_10B_TIMEOUT);
    if (recode < 0) return recode;

    W5500_LOGD("GET MAC: %s", ETH_TOOL_GET_MAC_STR(mac));
    return W5500_OK;
}
/// @brief Get subnet mask
/// @param sub Subnet mask
/// @return Error code
int W5500_Get_SUB(uint8_t *sub)
{
    int recode = W5500_OK;
    if (sub == NULL) return W5500_ERR_INVALID_ARG;

    recode = W5500_Read_Datas(SUBR0, sub, 4, W5500_SPI_LESS_10B_TIMEOUT);
    if (recode < 0) return recode;

    W5500_LOGD("GET SUB: %s", ETH_TOOL_GET_IP_STR(sub));
    return W5500_OK;
}
/// @brief Get gateway address
/// @param gw Gateway
/// @return Error code
int W5500_Get_GW(uint8_t *gw)
{
    int recode = W5500_OK;
    if (gw == NULL) return W5500_ERR_INVALID_ARG;
    
    recode = W5500_Read_Datas(GAR0, gw, 4, W5500_SPI_LESS_10B_TIMEOUT);
    if (recode < 0) return recode;
    
    W5500_LOGD("GET GW: %s", ETH_TOOL_GET_IP_STR(gw));
    return W5500_OK;
}
/// @brief Get host IP address
/// @param ip IP address
/// @return Error code
int W5500_Get_IP(uint8_t *ip)
{
    int recode = W5500_OK;
    if (ip == NULL) return W5500_ERR_INVALID_ARG;

    recode = W5500_Read_Datas(SIPR0, ip, 4, W5500_SPI_LESS_10B_TIMEOUT);
    if (recode < 0) return recode;
    
    W5500_LOGD("GET IP: %s", ETH_TOOL_GET_IP_STR(ip));
    return W5500_OK;
}
/// @brief Set mode register
/// @param mr Value
/// @return Error code
int W5500_Set_MR(uint8_t mr)
{
    int recode = W5500_OK;

    recode = W5500_Write_Datas(MR, &mr, 1, W5500_SPI_LESS_10B_TIMEOUT);
    if (recode < 0) return recode;

    return W5500_OK;
}
/// @brief Get mode register
/// @param mr Value save pointer
/// @return Error code
int W5500_Get_MR(uint8_t *mr)
{
    int recode = W5500_OK;
    if (mr == NULL) return W5500_ERR_INVALID_ARG;

    recode = W5500_Read_Datas(MR, mr, 1, W5500_SPI_LESS_10B_TIMEOUT);
    if (recode < 0) return recode;

    return W5500_OK;
}
/// @brief Set interrupt register
/// @param ir Value
/// @return Error code
int W5500_Set_IR(uint8_t ir)
{
    int recode = W5500_OK;

    recode = W5500_Write_Datas(IR, &ir, 1, W5500_SPI_LESS_10B_TIMEOUT);
    if (recode < 0) return recode;

    return W5500_OK;
}
/// @brief Get interrupt register
/// @param ir Value save pointer
/// @return Error code
int W5500_Get_IR(uint8_t *ir)
{
    int recode = W5500_OK;
    if (ir == NULL) return W5500_ERR_INVALID_ARG;

    recode = W5500_Read_Datas(IR, ir, 1, W5500_SPI_LESS_10B_TIMEOUT);
    if (recode < 0) return recode;

    return W5500_OK;
}
/// @brief Set Socket interrupt register
/// @param sir Value
/// @return Error code
int W5500_Set_SIR(uint8_t sir)
{
    int recode = W5500_OK;

    recode = W5500_Write_Datas(SIR, &sir, 1, W5500_SPI_LESS_10B_TIMEOUT);
    if (recode < 0) return recode;

    return W5500_OK;
}
/// @brief Get Socket interrupt register
/// @param sir Value save pointer
/// @return Error code
int W5500_Get_SIR(uint8_t *sir)
{
    int recode = W5500_OK;
    if (sir == NULL) return W5500_ERR_INVALID_ARG;

    recode = W5500_Read_Datas(SIR, sir, 1, W5500_SPI_LESS_10B_TIMEOUT);
    if (recode < 0) return recode;

    return W5500_OK;
}
/// @brief Set timeout
/// @param rtr Value
/// @return Error code
int W5500_Set_RTR(uint16_t rtr)
{
    int recode = W5500_OK;
    uint8_t wbuf[2] = {0};

    wbuf[0] = (rtr >> 8) & 0xff;
    wbuf[1] = rtr & 0xff;
    recode = W5500_Write_Datas(RTR0, wbuf, 2, W5500_SPI_LESS_10B_TIMEOUT);
    if (recode < 0) return recode;

    return W5500_OK;
}
/// @brief Get timeout
/// @param rtr Value save pointer
/// @return Error code
int W5500_Get_RTR(uint16_t *rtr)
{
    int recode = W5500_OK;
    uint8_t rbuf[2] = {0};
    if (rtr == NULL) return W5500_ERR_INVALID_ARG;

    recode = W5500_Read_Datas(RTR0, rbuf, 2, W5500_SPI_LESS_10B_TIMEOUT);
    if (recode < 0) return recode;

    *rtr = ((uint16_t)rbuf[0] << 8) | (rbuf[1]);
    return W5500_OK;
}
/// @brief Set retry count
/// @param rcr Value
/// @return Error code
int W5500_Set_RCR(uint8_t rcr)
{
    int recode = W5500_OK;

    recode = W5500_Write_Datas(WIZ_RCR, &rcr, 1, W5500_SPI_LESS_10B_TIMEOUT);
    if (recode < 0) return recode;

    return W5500_OK;
}
/// @brief Get retry count
/// @param rcr Value save pointer
/// @return Error code
int W5500_Get_RCR(uint8_t *rcr)
{
    int recode = W5500_OK;
    if (rcr == NULL) return W5500_ERR_INVALID_ARG;

    recode = W5500_Read_Datas(WIZ_RCR, rcr, 1, W5500_SPI_LESS_10B_TIMEOUT);
    if (recode < 0) return recode;

    return W5500_OK;
}
/// @brief Set SOCK MSS register
/// @param sock Socket number
/// @param mssr Value
/// @return Error code
int W5500_Sock_Set_MSSR(uint8_t sock, uint16_t mssr)
{
    int recode = W5500_OK;
    uint8_t wbuf[2] = {0};
    if (sock >= W5500_SOCK_MAX_NUM) return W5500_ERR_INVALID_ARG;

    wbuf[0] = (mssr >> 8) & 0xff;
    wbuf[1] = mssr & 0xff;

    recode = W5500_Write_Datas(Sn_MSSR0(sock), wbuf, 2, W5500_SPI_LESS_10B_TIMEOUT);
    if (recode < 0) return recode;

    return W5500_OK;
}
/// @brief Get SOCK MSS register
/// @param sock Socket number
/// @param mssr Value save pointer
/// @return Error code
int W5500_Sock_Get_MSSR(uint8_t sock, uint16_t *mssr)
{
    int recode = W5500_OK;
    uint8_t rbuf[2] = {0};
    if (sock >= W5500_SOCK_MAX_NUM || mssr == NULL) return W5500_ERR_INVALID_ARG;

    recode = W5500_Read_Datas(Sn_MSSR0(sock), rbuf, 2, W5500_SPI_LESS_10B_TIMEOUT);
    if (recode < 0) return recode;

    *mssr = ((uint16_t)rbuf[0] << 8) | (rbuf[1]);
    return W5500_OK;
}
/// @brief Set SOCK TTL
/// @param sock Socket number
/// @param ttl Value
/// @return Error code
int W5500_Sock_Set_TTL(uint8_t sock, uint8_t ttl)
{
    int recode = W5500_OK;
    if (sock >= W5500_SOCK_MAX_NUM) return W5500_ERR_INVALID_ARG;

    recode = W5500_Write_Datas(Sn_TTL(sock), &ttl, 1, W5500_SPI_LESS_10B_TIMEOUT);
    if (recode < 0) return recode;

    return W5500_OK;
}
/// @brief Get SOCK TTL
/// @param sock Socket number
/// @param ttl Value save pointer
/// @return Error code
int W5500_Sock_Get_TTL(uint8_t sock, uint8_t *ttl)
{
    int recode = W5500_OK;
    if (sock >= W5500_SOCK_MAX_NUM || ttl == NULL) return W5500_ERR_INVALID_ARG;

    recode = W5500_Read_Datas(Sn_TTL(sock), ttl, 1, W5500_SPI_LESS_10B_TIMEOUT);
    if (recode < 0) return recode;

    return W5500_OK;
}
/// @brief Set SOCK control register
/// @param sock Socket number
/// @param cr Value
/// @return Error code
int W5500_Sock_Set_CR(uint8_t sock, uint8_t cr)
{
    int recode = W5500_OK;
    if (sock >= W5500_SOCK_MAX_NUM) return W5500_ERR_INVALID_ARG;

    recode = W5500_Write_Datas(Sn_CR(sock), &cr, 1, W5500_SPI_LESS_10B_TIMEOUT);
    if (recode < 0) return recode;

    return W5500_OK;
}
/// @brief Get SOCK control register
/// @param sock Socket number
/// @param cr Value save pointer
/// @return Error code
int W5500_Sock_Get_CR(uint8_t sock, uint8_t *cr)
{
    int recode = W5500_OK;
    if (sock >= W5500_SOCK_MAX_NUM || cr == NULL) return W5500_ERR_INVALID_ARG;

    recode = W5500_Read_Datas(Sn_CR(sock), cr, 1, W5500_SPI_LESS_10B_TIMEOUT);
    if (recode < 0) return recode;
    
    return W5500_OK;
}
/// @brief Set SOCK interrupt register
/// @param sock Socket number
/// @param ir Value
/// @return Error code
int W5500_Sock_Set_IR(uint8_t sock, uint8_t ir)
{
    int recode = W5500_OK;
    if (sock >= W5500_SOCK_MAX_NUM) return W5500_ERR_INVALID_ARG;

    recode = W5500_Write_Datas(Sn_IR(sock), &ir, 1, W5500_SPI_LESS_10B_TIMEOUT);
    if (recode < 0) return recode;

    return W5500_OK;
}
/// @brief Get SOCK interrupt register
/// @param sock Socket number
/// @param ir Value save pointer
/// @return Error code
int W5500_Sock_Get_IR(uint8_t sock, uint8_t *ir)
{
    int recode = W5500_OK;
    if (sock >= W5500_SOCK_MAX_NUM || ir == NULL) return W5500_ERR_INVALID_ARG;

    recode = W5500_Read_Datas(Sn_IR(sock), ir, 1, W5500_SPI_LESS_10B_TIMEOUT);
    if (recode < 0) return recode;
    
    return W5500_OK;
}
/// @brief Get SOCK status register
/// @param sock Socket number
/// @param sr Value save pointer
/// @return Error code
int W5500_Sock_Get_SR(uint8_t sock, uint8_t *sr)
{
    int recode = W5500_OK;
    if (sock >= W5500_SOCK_MAX_NUM || sr == NULL) return W5500_ERR_INVALID_ARG;

    recode = W5500_Read_Datas(Sn_SR(sock), sr, 1, W5500_SPI_LESS_10B_TIMEOUT);
    if (recode < 0) return recode;
    
    return W5500_OK;
}
/// @brief Get SOCK free TX buffer register
/// @param sock Socket number
/// @param tx_fsr Value save pointer
/// @return Error code
int W5500_Sock_Get_TX_FSR(uint8_t sock, uint16_t *tx_fsr)
{
    int recode = W5500_OK;
    uint8_t rbuf[2] = {0};
    if (sock >= W5500_SOCK_MAX_NUM || tx_fsr == NULL) return W5500_ERR_INVALID_ARG;

    recode = W5500_Read_Datas(Sn_TX_FSR0(sock), rbuf, 2, W5500_SPI_LESS_10B_TIMEOUT);
    if (recode < 0) return recode;

    *tx_fsr = ((uint16_t)rbuf[0] << 8) | (rbuf[1]);
    return W5500_OK;
}
/// @brief Get SOCK received data size register
/// @param sock Socket number
/// @param rx_rsr Value save pointer
/// @return Error code
int W5500_Sock_Get_RX_RSR(uint8_t sock, uint16_t *rx_rsr)
{
    int recode = W5500_OK;
    uint8_t rbuf[2] = {0};
    if (sock >= W5500_SOCK_MAX_NUM || rx_rsr == NULL) return W5500_ERR_INVALID_ARG;

    recode = W5500_Read_Datas(Sn_RX_RSR0(sock), rbuf, 2, W5500_SPI_LESS_10B_TIMEOUT);
    if (recode < 0) return recode;

    *rx_rsr = ((uint16_t)rbuf[0] << 8) | (rbuf[1]);
    return W5500_OK;
}
/// @brief Set SOCK TX write pointer register
/// @param sock Socket number
/// @param tx_wr Value
/// @return Error code
int W5500_Sock_Set_TX_WR(uint8_t sock, uint16_t tx_wr)
{
    int recode = W5500_OK;
    uint8_t wbuf[2] = {0};
    if (sock >= W5500_SOCK_MAX_NUM) return W5500_ERR_INVALID_ARG;

    wbuf[0] = (tx_wr >> 8) & 0xff;
    wbuf[1] = tx_wr & 0xff;

    recode = W5500_Write_Datas(Sn_TX_WR0(sock), wbuf, 2, W5500_SPI_LESS_10B_TIMEOUT);
    if (recode < 0) return recode;

    return W5500_OK;
}
/// @brief Get SOCK TX write pointer register
/// @param sock Socket number
/// @param tx_wr Value save pointer
/// @return Error code
int W5500_Sock_Get_TX_WR(uint8_t sock, uint16_t *tx_wr)
{
    int recode = W5500_OK;
    uint8_t rbuf[2] = {0};
    if (sock >= W5500_SOCK_MAX_NUM || tx_wr == NULL) return W5500_ERR_INVALID_ARG;

    recode = W5500_Read_Datas(Sn_TX_WR0(sock), rbuf, 2, W5500_SPI_LESS_10B_TIMEOUT);
    if (recode < 0) return recode;

    *tx_wr = ((uint16_t)rbuf[0] << 8) | (rbuf[1]);
    return W5500_OK;
}
/// @brief Set SOCK RX read pointer register
/// @param sock Socket number
/// @param rx_rd Value
/// @return Error code
int W5500_Sock_Set_RX_RD(uint8_t sock, uint16_t rx_rd)
{
    int recode = W5500_OK;
    uint8_t wbuf[2] = {0};
    if (sock >= W5500_SOCK_MAX_NUM) return W5500_ERR_INVALID_ARG;

    wbuf[0] = (rx_rd >> 8) & 0xff;
    wbuf[1] = rx_rd & 0xff;

    recode = W5500_Write_Datas(Sn_RX_RD0(sock), wbuf, 2, W5500_SPI_LESS_10B_TIMEOUT);
    if (recode < 0) return recode;

    return W5500_OK;
}
/// @brief Get SOCK RX read pointer register
/// @param sock Socket number
/// @param rx_rd Value save pointer
/// @return Error code
int W5500_Sock_Get_RX_RD(uint8_t sock, uint16_t *rx_rd)
{
    int recode = W5500_OK;
    uint8_t rbuf[2] = {0};
    if (sock >= W5500_SOCK_MAX_NUM || rx_rd == NULL) return W5500_ERR_INVALID_ARG;

    recode = W5500_Read_Datas(Sn_RX_RD0(sock), rbuf, 2, W5500_SPI_LESS_10B_TIMEOUT);
    if (recode < 0) return recode;

    *rx_rd = ((uint16_t)rbuf[0] << 8) | (rbuf[1]);
    return W5500_OK;
}
/// @brief SOCK send data
/// @param sock Socket number
/// @param sbuf Data buffer
/// @param slen Send length
/// @return Error code
int W5500_Sock_Send(uint8_t sock, uint8_t *sbuf, uint16_t slen)
{
    int recode = W5500_OK;
    uint16_t ptr = 0;
    uint32_t addr_bsb = 0;
    if (sock >= W5500_SOCK_MAX_NUM || sbuf == NULL || slen == 0) return W5500_ERR_INVALID_ARG;

    recode = W5500_Sock_Get_TX_WR(sock, &ptr);
    if (recode < 0) return recode;

    addr_bsb = ((uint32_t)ptr << 8) | (sock << 5) | 0x10;
    recode = W5500_Write_Datas(addr_bsb, sbuf, slen, W5500_SPI_MAX_TIMEOUT);
    if (recode < 0) return recode;
    
    ptr += slen;
    recode = W5500_Sock_Set_TX_WR(sock, ptr);
    if (recode < 0) return recode;
    return W5500_OK;
}
/// @brief SOCK receive data
/// @param sock Socket number
/// @param rbuf Data buffer
/// @param rlen Receive length
/// @return Error code
int W5500_Sock_Recv(uint8_t sock, uint8_t *rbuf, uint16_t rlen)
{
    int recode = W5500_OK;
    uint16_t ptr = 0;
    uint32_t addr_bsb = 0;
    if (sock >= W5500_SOCK_MAX_NUM || rbuf == NULL || rlen == 0) return W5500_ERR_INVALID_ARG;

    recode = W5500_Sock_Get_RX_RD(sock, &ptr);
    if (recode < 0) return recode;

    addr_bsb = ((uint32_t)ptr << 8) | (sock << 5) | 0x18;
    recode = W5500_Read_Datas(addr_bsb, rbuf, rlen, W5500_SPI_MAX_TIMEOUT);
    if (recode < 0) return recode;

    ptr += rlen;
    recode = W5500_Sock_Set_RX_RD(sock, ptr);
    if (recode < 0) return recode;
    return W5500_OK;
}
/// @brief SOCK send data (with timeout version)
/// @param sock Socket number
/// @param sbuf Data buffer
/// @param slen Send length
/// @return Error code
int W5500_Sock_Send_With_Timeout(uint8_t sock, uint8_t *sbuf, uint16_t slen, uint32_t timeout)
{
    int recode = W5500_OK;
    uint16_t ptr = 0;
    uint32_t addr_bsb = 0;
    if (sock >= W5500_SOCK_MAX_NUM || sbuf == NULL || slen == 0) return W5500_ERR_INVALID_ARG;

    recode = W5500_Sock_Get_TX_WR(sock, &ptr);
    if (recode < 0) return recode;

    addr_bsb = ((uint32_t)ptr << 8) | (sock << 5) | 0x10;
    recode = W5500_Write_Datas(addr_bsb, sbuf, slen, timeout);
    if (recode < 0) return recode;
    
    ptr += slen;
    recode = W5500_Sock_Set_TX_WR(sock, ptr);
    if (recode < 0) return recode;
    return W5500_OK;
}
/// @brief SOCK receive data (with timeout version)
/// @param sock Socket number
/// @param rbuf Data buffer
/// @param rlen Receive length
/// @return Error code
int W5500_Sock_Recv_With_Timeout(uint8_t sock, uint8_t *rbuf, uint16_t rlen, uint32_t timeout)
{
    int recode = W5500_OK;
    uint16_t ptr = 0;
    uint32_t addr_bsb = 0;
    if (sock >= W5500_SOCK_MAX_NUM || rbuf == NULL || rlen == 0) return W5500_ERR_INVALID_ARG;

    recode = W5500_Sock_Get_RX_RD(sock, &ptr);
    if (recode < 0) return recode;

    addr_bsb = ((uint32_t)ptr << 8) | (sock << 5) | 0x18;
    recode = W5500_Read_Datas(addr_bsb, rbuf, rlen, timeout);
    if (recode < 0) return recode;
    
    ptr += rlen;
    recode = W5500_Sock_Set_RX_RD(sock, ptr);
    if (recode < 0) return recode;
    return W5500_OK;
}
/// @brief Set SOCK keepalive register
/// @param sock Socket number
/// @param kpalvtr Keepalive interval (unit: 5s, 0 means disabled)
/// @return Error code
int W5500_Sock_Set_KPALVTR(uint8_t sock, uint8_t kpalvtr)
{
    int recode = W5500_OK;
    if (sock >= W5500_SOCK_MAX_NUM) return W5500_ERR_INVALID_ARG;

    recode = W5500_Write_Datas(Sn_KPALVTR(sock), &kpalvtr, 1, W5500_SPI_LESS_10B_TIMEOUT);
    if (recode < 0) return recode;

    return W5500_OK;
}

/// @brief Configure MACRAW SOCK
/// @param mac_filter MAC filter
/// @param ipv6_filter IPv6 filter
/// @param bcast_block Broadcast packet blocking
/// @param mcast_block Multicast packet blocking
/// @return Error code
int W5500_Macraw_Sock_Open(uint8_t mac_filter, uint8_t ipv6_filter, uint8_t bcast_block, uint8_t mcast_block)
{
    int recode = W5500_OK;
    uint8_t sn_mr = 0x00, imr = 0x00, simr = 0x01, s0_imr = 0x1c;

    sn_mr = Sn_MR_MACRAW | (mac_filter << 7) | (ipv6_filter << 4) | (bcast_block << 6) | (mcast_block << 5);
    recode = W5500_Write_Datas(Sn_MR(0), &sn_mr, 1, W5500_SPI_LESS_10B_TIMEOUT);
    if (recode != 1) return recode;

    recode = W5500_Write_Datas(IMR, &imr, 1, W5500_SPI_LESS_10B_TIMEOUT);
    if (recode != 1) return recode;

    recode = W5500_Write_Datas(SIMR, &simr, 1, W5500_SPI_LESS_10B_TIMEOUT);
    if (recode != 1) return recode;

    recode = W5500_Write_Datas(Sn_IMR(0), &s0_imr, 1, W5500_SPI_LESS_10B_TIMEOUT);
    if (recode != 1) return recode;

    recode = W5500_Sock_Set_CR(0, Sn_CR_OPEN);
    if (recode != W5500_OK) return recode;

    return recode;
}

/// @brief MACRAW SOCK send data
/// @param sbuf Send buffer
/// @param slen Send buffer length
/// @param is_flush Whether to send immediately
/// @return Actual sent length or error code
int W5500_Macraw_Sock_Send(uint8_t *sbuf, uint16_t slen, uint8_t is_flush)
{
    int recode = W5500_OK;
    uint8_t sn_sr = 0x00, sn_cr = 0x00;
    uint16_t sn_tx_fsr = 0, cr_timeout = 0;

    recode = W5500_Sock_Get_SR(0, &sn_sr);
    if (recode != W5500_OK) return recode;
    if (sn_sr != 0x42) return W5500_ERR_INVALID_STATE;

    if (sbuf != NULL && slen > 0) {
        recode = W5500_Sock_Get_TX_FSR(0, &sn_tx_fsr);
        if (recode != W5500_OK) return recode;

        if (sn_tx_fsr > slen) sn_tx_fsr = slen;
        recode = W5500_Sock_Send(0, sbuf, sn_tx_fsr);
        if (recode != W5500_OK) return recode;
    }

    if (sn_tx_fsr <= slen || is_flush) {
        recode = W5500_Sock_Set_CR(0, Sn_CR_SEND);
        if (recode != W5500_OK) return recode;

        do {
            recode = W5500_Sock_Get_CR(0, &sn_cr);
            if (recode != W5500_OK) return recode;
        } while (((sn_cr & Sn_CR_SEND) == Sn_CR_SEND) && (++cr_timeout < 100));
    }

    return sn_tx_fsr;
}

/// @brief MACRAW SOCK receive data
/// @param rbuf Receive buffer
/// @param rlen Receive buffer length
/// @return Actual received length or error code
int W5500_Macraw_Sock_Recv(uint8_t *rbuf, uint16_t rlen, uint8_t en_recv)
{
    int recode = W5500_OK;
    uint8_t sn_sr = 0x00, sn_cr = 0x00;
    uint16_t sn_rx_rsr = 0, cr_timeout = 0;
    if (rbuf == NULL || rlen == 0) return W5500_ERR_INVALID_ARG;

    recode = W5500_Sock_Get_SR(0, &sn_sr);
    if (recode != W5500_OK) return recode;
    if (sn_sr != 0x42) return W5500_ERR_INVALID_STATE;

    recode = W5500_Sock_Get_RX_RSR(0, &sn_rx_rsr);
    if (recode != W5500_OK) return recode;

    if (sn_rx_rsr > 0) {
        if (sn_rx_rsr >= rlen) sn_rx_rsr = rlen;
        recode = W5500_Sock_Recv(0, rbuf, sn_rx_rsr);
        if (recode != W5500_OK) return recode;
    }

    if (en_recv) {
        recode = W5500_Sock_Set_CR(0, Sn_CR_RECV);
        if (recode != W5500_OK) return recode;

        do {
            recode = W5500_Sock_Get_CR(0, &sn_cr);
            if (recode != W5500_OK) return recode;
        } while (((sn_cr & Sn_CR_RECV) == Sn_CR_RECV) && (++cr_timeout < 100));
    }
    
    return sn_rx_rsr;
}

/// @brief Close MACRAW SOCK
/// @param None
void W5500_Macraw_Sock_Close(void)
{
    W5500_Sock_Set_CR(0, Sn_CR_CLOSE);
}
