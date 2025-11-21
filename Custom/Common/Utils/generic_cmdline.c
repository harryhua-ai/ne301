#include "generic_cmdline.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "mem.h"

/* Initialize queue */
void queue_init(cmd_queue_t* q, char* buf, uint16_t size, void (*lock)(void), void (*unlock)(void)) 
{
    q->buffer = buf;
    q->size = size;
    q->wr = q->rd = 0;
    q->lock = lock;
    q->unlock = unlock;
}

// Check if queue is empty
bool queue_empty(cmd_queue_t* q) 
{
    return q->wr == q->rd;
}

// Check if queue is full
bool queue_full(cmd_queue_t* q) 
{
    return ((q->wr + 1) % q->size) == q->rd;
}

/* Thread-safe enqueue */
bool queue_put(cmd_queue_t* q, char c) 
{
    if(q->lock) q->lock();
    bool ret = false;
    if(((q->wr + 1) % q->size) != q->rd) {
        q->buffer[q->wr] = c;
        q->wr = (q->wr + 1) % q->size;
        ret = true;
    }
    if(q->unlock) q->unlock();
    return ret;
}

/* Thread-safe dequeue */
char queue_get(cmd_queue_t* q) 
{
    if(q->lock) q->lock();
    char c = '\0';
    if(q->wr != q->rd) {
        c = q->buffer[q->rd];
        q->rd = (q->rd + 1) % q->size;
    }
    if(q->unlock) q->unlock();
    return c;
}

static void clear_line(cmdline_t* cli) 
{
    uint8_t total = strlen(cli->prompt) + cli->cursor_pos;
    for(uint8_t i=0; i<total; i++) cli->output('\b');
    for(uint8_t i=0; i<total; i++) cli->output(' ');
    for(uint8_t i=0; i<total; i++) cli->output('\b');
}

static void show_prompt(cmdline_t* cli) 
{
    if (cli->output_str) {
        cli->output_str(cli->prompt);
    } else {
        for (char* p = cli->prompt; *p; p++) {
            cli->output(*p);
        }
    }
}

static void refresh_display(cmdline_t* cli)
{
    cli->output('\033'); 
    cli->output('['); 
    cli->output('2'); 
    cli->output('K'); 
    cli->output('\r');

    show_prompt(cli);

    for(uint8_t i=0; i<cli->cursor_pos; i++)
        cli->output(cli->buffer[i]);
}

static void add_to_history(cmdline_t* cli, const char* cmd) 
{
    if(cmd == NULL || cmd[0] == '\0') return;

    if(cli->history_count > 0) {
        const char* last_cmd = cli->history[cli->history_count - 1];
        if(last_cmd && strcmp(last_cmd, cmd) == 0) {
            return;
        }
    }

    if(cli->history_count == HISTORY_DEPTH) {
        hal_mem_free(cli->history[0]);
        memmove(&cli->history[0], &cli->history[1], (HISTORY_DEPTH-1)*sizeof(char*));
        cli->history_count--;
    }

    cli->history[cli->history_count] = strdup(cmd);
    cli->history_count++;
}

static void parse_args(char* line, int* argc, char** argv) 
{
    *argc = 0;
    char* p = strtok(line, " ");
    while(p && *argc < MAX_ARGC) {
        argv[(*argc)++] = p;
        p = strtok(NULL, " ");
    }
}

static void cmdline_print_help(cmdline_t* cli) 
{
    if (cli->output_str) {
        cli->output_str("\r\nAvailable commands:\r\n");
    } else {
        cli->output('\r'); cli->output('\n');
        const char* msg = "Available commands:\r\n";
        while (*msg) cli->output(*msg++);
    }

    for (int i = 0; i < cli->cmd_count; i++) {
        char line[CMD_MAX_LEN + 32];
        snprintf(line, sizeof(line), "  %-10s %s\r\n", 
                cli->commands[i].name, 
                cli->commands[i].help ? cli->commands[i].help : "");
        
        if (cli->output_str) {
            cli->output_str(line);
        } else {
            for (char* p = line; *p; p++) {
                cli->output(*p);
            }
        }
    }
}

static void on_command_executed(cmdline_t* cli) 
{
    memset(cli->buffer, 0, sizeof(cli->buffer));
    cli->cursor_pos = 0;
    cli->history_idx = -1;
    show_prompt(cli);
}

static void execute_command(cmdline_t* cli, char* cmd) 
{
    int argc = 0;
    char* argv[MAX_ARGC];
    char cmd_buf[CMD_MAX_LEN];
    
    strncpy(cmd_buf, cmd, CMD_MAX_LEN);
    parse_args(cmd_buf, &argc, argv);
    
    if(argc == 0) return;

    if (strcmp(argv[0], "help") == 0 || strcmp(argv[0], "?") == 0) {
        cmdline_print_help(cli);
        return;
    }
    
    for(int i=0; i<cli->cmd_count; i++) {
        if(strcmp(argv[0], cli->commands[i].name) == 0) {
            cli->commands[i].handler(argc, argv);
            return;
        }
    }
    
    if (cli->unknown_cmd) {
        cli->unknown_cmd(cmd);
    }
    cmdline_print_help(cli);
}

static void default_completer(void* cli, const char* prefix, char** matches, int* count) 
{
    *count = 0;
    cmdline_t* _cli = (cmdline_t *)cli;
    for(int i = 0; i < _cli->cmd_count; i++) {
        if(strncmp(prefix, _cli->commands[i].name, strlen(prefix)) == 0) {
            matches[*count] = (char*)_cli->commands[i].name;
            if(++(*count) >= 8) break;
        }
    }
}

static void handle_completion(cmdline_t* cli) 
{
    if(!cli->completer) return;

    char prefix[CMD_MAX_LEN];
    strncpy(prefix, cli->buffer, cli->cursor_pos);
    prefix[cli->cursor_pos] = '\0';

    int match_count = 0;
    memset(cli->completion_matches, 0, sizeof(cli->completion_matches));
    cli->completer(cli, prefix, cli->completion_matches, &match_count);

    if(match_count == 0) return;

    if(match_count == 1) {
        strncpy(cli->buffer, cli->completion_matches[0], CMD_MAX_LEN);
        cli->cursor_pos = strlen(cli->buffer);
        refresh_display(cli);
    } else {
        clear_line(cli);
        cli->output('\r');
        show_prompt(cli);
        for(uint8_t i=0; i<cli->cursor_pos; i++)
            cli->output(cli->buffer[i]);
        
        cli->output('\r');
        cli->output('\n');
        
        for(int i=0; i<match_count && i<8; i++) {
            if(cli->output_str) 
                cli->output_str(cli->completion_matches[i]);
            else 
                for(const char* p = cli->completion_matches[i]; *p; p++) 
                    cli->output(*p);
            cli->output(' ');
        }
        cli->output('\r');
        cli->output('\n');
        refresh_display(cli);
    }
}

void cmdline_init(cmdline_t* cli, cmd_queue_t* q, void (*output)(char), void (*unknown_cmd)(char*), const char* prompt) 
{
    memset(cli, 0, sizeof(cmdline_t));
    cli->input_queue = q;
    cli->output = output;
    cli->unknown_cmd = unknown_cmd;
    cli->completer = default_completer;
    cli->history_idx = -1; // Fix: initialize to -1
    if(prompt != NULL){
        strncpy(cli->prompt, prompt, PROMPT_MAX_LEN);
        cli->prompt[PROMPT_MAX_LEN-1] = '\0';
    }else{
        memset(cli, 0, sizeof(cmdline_t));
        strncpy(cli->prompt, "> ", PROMPT_MAX_LEN);
    }
}

void cmdline_register_output_str(cmdline_t* cli, void (*output_str)(const char*)) 
{
    cli->output_str = output_str;
}

bool cmdline_register(cmdline_t* cli, const char* name, const char* help, cmd_handler handler)
{
    for (int i = 0; i < cli->cmd_count; ++i) {
        if (strcmp(cli->commands[i].name, name) == 0) {
            return false;
        }
    }

    if (cli->cmd_count >= MAX_CMDS) return false;

    cli->commands[cli->cmd_count].name = name;
    cli->commands[cli->cmd_count].help = help;
    cli->commands[cli->cmd_count].handler = handler;
    cli->cmd_count++;
    return true;
}

void cmdline_process(cmdline_t* cli) 
{
    static uint8_t esc_state = 0;
    char c;
    
    while(!queue_empty(cli->input_queue)) {
        c = queue_get(cli->input_queue);
        
        if(esc_state == 1) {
            if(c == '[') esc_state++;
            else esc_state = 0;
            continue;
        } else if(esc_state == 2) {
            esc_state = 0;
            if(c == 'A' || c == 'B') {  // 'A' up (previous/older), 'B' down (next/newer)
                if(cli->history_count == 0) continue;

                int8_t new_idx;
                if(c == 'A') {  // UP arrow - go to previous (older) commands
                    if(cli->history_idx == -1) {
                        // First time pressing UP - start from most recent command
                        new_idx = cli->history_count - 1;
                    } else {
                        new_idx = cli->history_idx - 1;
                        if(new_idx < 0) {
                            // Reached oldest, wrap to newest
                            new_idx = cli->history_count - 1;
                        }
                    }
                } else {  // DOWN arrow - go to next (newer) commands
                    if(cli->history_idx == -1) {
                        // At empty line, stay there
                        new_idx = -1;
                    } else {
                        new_idx = cli->history_idx + 1;
                        if(new_idx >= cli->history_count) {
                            // Reached newest, go to empty line
                            new_idx = -1;
                        }
                    }
                }

                if(new_idx != cli->history_idx) {
                    cli->history_idx = new_idx;

                    if(cli->history_idx >= 0 && cli->history_idx < cli->history_count) {
                        // Load command from history
                        strncpy(cli->buffer, cli->history[cli->history_idx], CMD_MAX_LEN);
                        cli->buffer[CMD_MAX_LEN-1] = '\0';
                        cli->cursor_pos = strlen(cli->buffer);
                    } else {
                        // Empty line
                        cli->buffer[0] = '\0';
                        cli->cursor_pos = 0;
                    }
                    refresh_display(cli);
                }
            }
            continue;
        }
        
        /* Process normal characters */
        switch(c) {
        case KEY_ESC:
            esc_state = 1;
            break;
            
        case KEY_TAB:
            handle_completion(cli);
            break;
            
        case KEY_ENTER:      // 0x0D (\r)
        case '\n':           // 0x0A (\n) - add support for Windows
            cli->output('\r');
            cli->output('\n');
            if(cli->buffer[0] != '\0') {
                add_to_history(cli, cli->buffer);
                execute_command(cli, cli->buffer);
            }
            on_command_executed(cli);
            break;
            
        case KEY_BACKSPACE:
            if(cli->cursor_pos > 0) {
                cli->cursor_pos--;
                cli->buffer[cli->cursor_pos] = '\0';
                cli->output('\b');
                cli->output(' ');
                cli->output('\b');
            }
            break;
            
        case KEY_CTRL_C:
            // Cancel current input and start fresh
            cli->output('^');
            cli->output('C');
            cli->output('\r');
            cli->output('\n');
            
            // Clear buffer and reset state
            memset(cli->buffer, 0, sizeof(cli->buffer));
            cli->cursor_pos = 0;
            cli->history_idx = -1;
            
            // Show new prompt
            show_prompt(cli);
            break;
            
        default:
            if(cli->cursor_pos < CMD_MAX_LEN && c >= 0x20 && c <= 0x7E) {
                cli->buffer[cli->cursor_pos] = c;
                cli->cursor_pos++;
                cli->output(c);
            }
            break;
        }
    }
}
