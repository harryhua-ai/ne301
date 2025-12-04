#include <string.h>
#include <stdio.h>
#include "Log/debug.h"
#include "cmsis_os2.h"
#include "common_utils.h"
#include "tx_api.h"
#include "system_top.h"

#define CPU_LOAD_HISTORY_DEPTH 8
typedef struct {
	ULONG64 current_total;
	ULONG64 current_thread_total;
	ULONG64 prev_total;
	ULONG64 prev_thread_total;
	struct {
		ULONG64 total;
		ULONG64 thread;
		uint32_t tick;
	} history[CPU_LOAD_HISTORY_DEPTH];
} cpuload_info_t;
static cpuload_info_t cpu_load = {0};
static osSemaphoreId_t printf_sem = NULL;
static uint8_t top_tread_stack[1024 * 4] ALIGN_32;
const osThreadAttr_t topTask_attributes = {
    .name = "topTask",
    .priority = (osPriority_t) osPriorityRealtime7,
    .stack_mem = top_tread_stack,
    .stack_size = sizeof(top_tread_stack),
};

static void cpuload_update(cpuload_info_t *cpu_load)
{
	EXECUTION_TIME thread_total;
	EXECUTION_TIME isr;
	EXECUTION_TIME idle;
	int i;

	cpu_load->history[1] = cpu_load->history[0];

	_tx_execution_thread_total_time_get(&thread_total);
	_tx_execution_isr_time_get(&isr);
	_tx_execution_idle_time_get(&idle);

	cpu_load->history[0].total = thread_total + isr + idle;
	cpu_load->history[0].thread = thread_total;
	cpu_load->history[0].tick = HAL_GetTick();

	if (cpu_load->history[1].tick - cpu_load->history[2].tick < 1000)
		return ;

	for (i = 0; i < CPU_LOAD_HISTORY_DEPTH - 2; i++)
		cpu_load->history[CPU_LOAD_HISTORY_DEPTH - 1 - i] = cpu_load->history[CPU_LOAD_HISTORY_DEPTH - 1 - i - 1];
}

static void cpuload_get_info(cpuload_info_t *cpu_load, float *cpu_load_last, float *cpu_load_last_second,
                             float *cpu_load_last_five_seconds)
{
	if (cpu_load_last)
		*cpu_load_last = 100.0 * (cpu_load->history[0].thread - cpu_load->history[1].thread) /
							(cpu_load->history[0].total - cpu_load->history[1].total);
	if (cpu_load_last_second)
		*cpu_load_last_second = 100.0 * (cpu_load->history[2].thread - cpu_load->history[3].thread) /
							(cpu_load->history[2].total - cpu_load->history[3].total);
	if (cpu_load_last_five_seconds)
		*cpu_load_last_five_seconds = 100.0 * (cpu_load->history[2].thread - cpu_load->history[7].thread) /
							(cpu_load->history[2].total - cpu_load->history[7].total);
}

// Note: _tx_thread_created_ptr and _tx_thread_created_count are ThreadX internal variables
extern TX_THREAD *_tx_thread_created_ptr;
extern ULONG _tx_thread_created_count;

static void system_top_printf_info(void)
{
    TX_THREAD *tptr = _tx_thread_created_ptr;
    ULONG tcount = _tx_thread_created_count, i = 0;
    ULONG64 run_milliseconds = 0, all_run_count = 0, *run_count_list = NULL;
    float cpu_load_one_second;
    TX_INTERRUPT_SAVE_AREA

    run_count_list = hal_mem_alloc(tcount * sizeof(ULONG64), MEM_FAST);
    if (run_count_list == NULL) return;
    cpuload_get_info(&cpu_load, NULL, &cpu_load_one_second, NULL);
    run_milliseconds = cpu_load.history[0].total / 800 / 1000;
    LOG_SIMPLE("\r\n==================================================================================================================");
    LOG_SIMPLE("CPU Load: %.2f%%", cpu_load_one_second);
    LOG_SIMPLE("TX Thread Count: %lu", tcount);
    LOG_SIMPLE("TX Thread Total Time: %02lu:%02lu:%02lu.%03lu", (uint32_t)(run_milliseconds / 1000 / 60 / 60), (uint32_t)((run_milliseconds / 1000 / 60) % 60), (uint32_t)((run_milliseconds / 1000) % 60), (uint32_t)(run_milliseconds % 1000));
    LOG_SIMPLE("------------------------------------------------------------------------------------------------------------------");
    LOG_SIMPLE(" %2s | %28s | %5s | %4s | %10s | %10s | %10s | %10s | %8s", "ID", "Thread Name", "State", "Prio", "StackSize" , "CurStack", "MaxStack", "RunTime", "Ratio");
    LOG_SIMPLE("------------------------------------------------------------------------------------------------------------------");
    TX_DISABLE
    for (i = 0; i < tcount; i++) {
      	run_count_list[i] = tptr->tx_thread_run_count;
        tptr->tx_thread_run_count = 0;
      	all_run_count += run_count_list[i];
		tptr = tptr->tx_thread_created_next;
		if (tptr == NULL) break;
    }
    TX_RESTORE
	tptr = _tx_thread_created_ptr;
    for (i = 0; i < tcount; i++) {
    	LOG_SIMPLE(" %2lu | %28s | %5u | %4u | %10u | %10u | %10u | %10u | %8.2f%%",
                   i,
                   tptr->tx_thread_name,
                   tptr->tx_thread_state,
                   tptr->tx_thread_priority,
                   tptr->tx_thread_stack_size,
                   tptr->tx_thread_stack_end - tptr->tx_thread_stack_ptr,
                   tptr->tx_thread_stack_end - tptr->tx_thread_stack_highest_ptr,
                   run_count_list[i], ((double)run_count_list[i]) * 100.0 / ((double)all_run_count));	
        tptr = tptr->tx_thread_created_next;
        if (tptr == NULL) break;
    }
    LOG_SIMPLE("==================================================================================================================\r\n");
}

static void system_top_process(void *argument)
{
    osStatus_t ret;

    while (1) {
        cpuload_update(&cpu_load);
        ret = osSemaphoreAcquire(printf_sem, 100);
        if (ret == osOK) system_top_printf_info();
    }
}

int system_top_cmd_deal(int argc, char* argv[])
{
    osSemaphoreRelease(printf_sem);
    return 0;
}

debug_cmd_reg_t system_top_cmd_table[] = {
    {"top",    "print system task information.",      system_top_cmd_deal},
};

static void system_top_cmd_register(void)
{
    printf_sem = osSemaphoreNew(1, 0, NULL);
    if (printf_sem == NULL) return;
    osThreadNew(system_top_process, NULL, &topTask_attributes);
    debug_cmdline_register(system_top_cmd_table, sizeof(system_top_cmd_table) / sizeof(system_top_cmd_table[0]));
}

void system_top_register(void)
{
    driver_cmd_register_callback("top", system_top_cmd_register);
}
