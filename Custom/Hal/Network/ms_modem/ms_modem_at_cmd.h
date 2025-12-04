#ifndef __MS_MODEM_AT_CMD_H__
#define __MS_MODEM_AT_CMD_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "ms_modem_at.h"

/// @brief Disable echo command
const at_cmd_item_t at_cmd_ate0 = {
    .cmd = "ATE0\r\n",
    .cmd_len = 0,
    .timeout_ms = 500,
    .expect_rsp_line = 2,
    .expect_rsp = {"ATE0", "OK"},
    .handler = NULL,
};

#ifdef __cplusplus
}
#endif

#endif /* __MS_MODEM_AT_CMD_H__ */
