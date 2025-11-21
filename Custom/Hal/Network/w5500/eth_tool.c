#include <stdio.h>
#include <string.h>
#include "eth_tool.h"

/// @brief Convert MAC address to string
/// @param mac MAC address
/// @param str String
/// @return Error code
int ETH_TOOL_MAC_TO_STR(const uint8_t *mac, char *str)
{
    if (mac == NULL || str == NULL) return ETH_TOOL_ERR_ARG;

    sprintf(str, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return ETH_TOOL_OK;
}
/// @brief Convert string to MAC address
/// @param str String
/// @param mac MAC address
/// @return Error code
int ETH_TOOL_STR_TO_MAC(const char *str, uint8_t *mac)
{
    uint8_t mun = 0;
    int i = 0, index = 0;
    if (str == NULL || mac == NULL) return ETH_TOOL_ERR_ARG;

    for (; i < strlen(str); i++) {
        if (str[i] == ':' && mun == 2) {
            index ++;
            mun = 0;
            if (index > 5) break;
        } else if (str[i] <= '9' && str[i] >= '0' && mun < 2) {
            if (mun == 0) {
                mac[index] = (str[i] - '0');
            } else {
                mac[index] *= 16;
                mac[index] += (str[i] - '0');
            }
            mun++;
            if (mac[index] > 255) return ETH_TOOL_ERR_ARG;
        } else if (str[i] <= 'f' && str[i] >= 'a' && mun < 2) {
            if (mun == 0) {
                mac[index] = (str[i] - 'a') + 10;
            } else {
                mac[index] *= 16;
                mac[index] += (str[i] - 'a') + 10;
            }
            mun++;
            if (mac[index] > 255) return ETH_TOOL_ERR_ARG;
        } else if (str[i] <= 'F' && str[i] >= 'A' && mun < 2) {
            if (mun == 0) {
                mac[index] = (str[i] - 'A') + 10;
            } else {
                mac[index] *= 16;
                mac[index] += (str[i] - 'A') + 10;
            }
            mun++;
            if (mac[index] > 255) return ETH_TOOL_ERR_ARG;
        } else return ETH_TOOL_ERR_ARG;
    }
    return ETH_TOOL_OK;
}
/// @brief Convert IP address to string
/// @param addr IP address
/// @param str String
/// @return Error code
int ETH_TOOL_IP_TO_STR(const uint8_t *addr, char *str)
{
    if (addr == NULL || str == NULL) return ETH_TOOL_ERR_ARG;

    sprintf(str, "%d.%d.%d.%d", addr[0], addr[1], addr[2], addr[3]);
    return ETH_TOOL_OK;
}
/// @brief Convert string to IP address
/// @param str String
/// @param addr IP address
/// @return Error code
int ETH_TOOL_STR_TO_IP(const char *str, uint8_t *addr)
{
    uint8_t mun = 0;
    int i = 0, index = 0;
    if (str == NULL || addr == NULL) return ETH_TOOL_ERR_ARG;
    
    for (; i < strlen(str); i++) {
        if (str[i] == '.' && mun) {
            index ++;
            mun = 0;
            if (index > 3) break;
        } else if (str[i] <= '9' && str[i] >= '0' && mun < 3) {
            if (mun == 0) {
                addr[index] = (str[i] - '0');
            } else {
                addr[index] *= 10;
                addr[index] += (str[i] - '0');
            }
            mun++;
            if (addr[index] > 255) return ETH_TOOL_ERR_ARG;
        } else return ETH_TOOL_ERR_ARG;
    }
    return ETH_TOOL_OK;
}
/// @brief Convert MAC address to string and return
/// @param mac MAC address
/// @return MAC address string
char *ETH_TOOL_GET_MAC_STR(const uint8_t *mac)
{
    static char mac_buf[32] = {0};
    if (mac == NULL) return "ERR_ARG";

    sprintf(mac_buf, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return mac_buf;
}
/// @brief Convert IP address to string and return
/// @param addr IP address
/// @return IP address string
char *ETH_TOOL_GET_IP_STR(const uint8_t *addr)
{
    static char ip_buf[32] = {0};
    if (addr == NULL) return "ERR_ARG";

    sprintf(ip_buf, "%d.%d.%d.%d", addr[0], addr[1], addr[2], addr[3]);
    return ip_buf;
}


uint16_t swaps(uint16_t i)
{
  uint16_t ret=0;
  ret = (i & 0xFF) << 8;
  ret |= ((i >> 8)& 0xFF);
  return ret;	
}

uint32_t swapl(uint32_t l)
{
  uint32_t ret=0;
  ret = (l & 0xFF) << 24;
  ret |= ((l >> 8) & 0xFF) << 16;
  ret |= ((l >> 16) & 0xFF) << 8;
  ret |= ((l >> 24) & 0xFF);
  return ret;
}

/**
@brief	htons function converts a unsigned short from host to TCP/IP network byte order (which is big-endian).
@return 	the value in TCP/IP network byte order
*/ 
unsigned short htons( 
	unsigned short hostshort	/**< A 16-bit number in host byte order.  */
	)
{
#if ( SYSTEM_ENDIAN == _ENDIAN_LITTLE_ )
	return swaps(hostshort);
#else
	return hostshort;
#endif		
}


/**
@brief	htonl function converts a unsigned long from host to TCP/IP network byte order (which is big-endian).
@return 	the value in TCP/IP network byte order
*/ 
unsigned long htonl(
	unsigned long hostlong		/**< hostshort  - A 32-bit number in host byte order.  */
	)
{
#if ( SYSTEM_ENDIAN == _ENDIAN_LITTLE_ )
	return swapl(hostlong);
#else
	return hostlong;
#endif	
}


/**
@brief	ntohs function converts a unsigned short from TCP/IP network byte order to host byte order (which is little-endian on Intel processors).
@return 	a 16-bit number in host byte order
*/ 
unsigned short ntohs(
	unsigned short netshort	/**< netshort - network odering 16bit value */
	)
{
#if ( SYSTEM_ENDIAN == _ENDIAN_LITTLE_ )	
	return htons(netshort);
#else
	return netshort;
#endif		
}


/**
@brief	converts a unsigned long from TCP/IP network byte order to host byte order (which is little-endian on Intel processors).
@return 	a 16-bit number in host byte order
*/ 
unsigned long ntohl(unsigned long netlong)
{
#if ( SYSTEM_ENDIAN == _ENDIAN_LITTLE_ )
	return htonl(netlong);
#else
	return netlong;
#endif		
}

