#ifndef __ETH_TOOL_H__
#define __ETH_TOOL_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define ETH_TOOL_OK         (0)
#define ETH_TOOL_ERR_ARG    (-1)

int ETH_TOOL_MAC_TO_STR(const uint8_t *mac, char *str);
int ETH_TOOL_STR_TO_MAC(const char *str, uint8_t *mac);
int ETH_TOOL_IP_TO_STR(const uint8_t *addr, char *str);
int ETH_TOOL_STR_TO_IP(const char *str, uint8_t *addr);

char *ETH_TOOL_GET_MAC_STR(const uint8_t *mac);
char *ETH_TOOL_GET_IP_STR(const uint8_t *addr);

unsigned short htons(unsigned short hostshort);	    /* htons function converts a unsigned short from host to TCP/IP network byte order (which is big-endian).*/
unsigned long htonl(unsigned long hostlong);		/* htonl function converts a unsigned long from host to TCP/IP network byte order (which is big-endian). */
unsigned short ntohs(unsigned short netshort);		/* ntohs function converts a unsigned short from TCP/IP network byte order to host byte order (which is little-endian on Intel processors). */
unsigned long ntohl(unsigned long netlong);		    /* ntohl function converts a u_long from TCP/IP network order to host byte order (which is little-endian on Intel processors). */


#ifdef __cplusplus
}
#endif

#endif
