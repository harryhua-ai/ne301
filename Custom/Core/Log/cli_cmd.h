/**
 * @file cli_cmd.h
 * @brief CLI Command Header
 * @details Header file for command line interface commands
 */

#ifndef CLI_CMD_H
#define CLI_CMD_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Register all CLI commands
 * @details Register custom CLI commands with the debug system
 */
void register_cmds(void);

#ifdef __cplusplus
}
#endif

#endif // CLI_CMD_H