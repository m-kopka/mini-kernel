#include "kernel.h"

/*
 *  Mini Kernel
 *  Lightweight cooperative real-time operating system kernel
 *  Martin Kopka 2023
*/ 

//---- CONSTANTS -------------------------------------------------------------------------------------------------------------------------------------------------

#define KERNEL_TASK_STACK_SIZE  256     // size of each task's stack in words
#define KERNEL_MAX_TASKS        16      // max task count

// return to Thread mode and use the process stack for return
#define KERNEL_EXCEPTION_RETURN_VALUE 0xFFFFFFFD

//---- INTERNAL FUNCTION PROTOTYPES ------------------------------------------------------------------------------------------------------------------------------

void      __kernel_init_stack(uint32_t *stack);
uint32_t* __kernel_switch_to_task(uint32_t *task_stack);
void      __kernel_init_deadlines(void);
void      __kernel_terminate_current_task(void);

//---- STRUCTS ---------------------------------------------------------------------------------------------------------------------------------------------------

typedef struct {

    uint32_t *psp;                 // task's process stack pointer (caution: the stack must stay allocated)
    kernel_time_t period;          // execution period, kernel tries to resume task's execution within this time window [ms]
    kernel_time_t deadline;        // absolute time value of the nearest deadline [ms]

} kernel_task_t;

//---- INTERNAL DATA ---------------------------------------------------------------------------------------------------------------------------------------------

struct kernel_internals_t {

    kernel_task_t task[KERNEL_MAX_TASKS];
    uint32_t      task_count;
    uint32_t      current_task;                 // index of currently executing task

} kernel;

volatile kernel_time_t kernel_time_ms = 0;      // milliseconds since kernel startup. Updated by the SysTick interrupt

//---- FUNCTIONS -------------------------------------------------------------------------------------------------------------------------------------------------

// initializes the OS kernel. Kernel needs to know the core frequency to properly setup the SysTick timer
void kernel_init(uint32_t core_clock_frequency_hz) {

    kernel.task_count = 0;

    // __kernel_init_stack() sets the current stack to the Process Stack, then triggers the Supervisor Call interrupt which pushes the process state 
    // onto the Process Stack. After returning, the kernel now runs in the Main Stack.
    // We need to pass a dummy task stack to the function to simulate returning from a task before switching to a Main Stack
    uint32_t dummy_task_stack[17];
    __kernel_init_stack(dummy_task_stack + 17);

    // init the SysTick timer to generate an interrupt every millisecond
    SysTick_Config(core_clock_frequency_hz / 1000);
    NVIC_SetPriority(SysTick_IRQn, 3);
}

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - 

// starts the OS kernel
void kernel_start(void) {

    if (kernel.task_count == 0) while (1);      // no tasks to run
    kernel.current_task = 0;

    while (1) {

        // resume execution of the selected task if the task is not sleeping
        kernel.task[kernel.current_task].psp = __kernel_switch_to_task(kernel.task[kernel.current_task].psp);

        // update deadline. If the new deadline overflowed, re-initialize all deadlines
        if ((uint64_t)kernel.task[kernel.current_task].deadline + (uint64_t)kernel.task[kernel.current_task].period > 0xffffffff) __kernel_init_deadlines();
        else kernel.task[kernel.current_task].deadline += kernel.task[kernel.current_task].period;

        // find the next task with the earliest deadtime to switch to
        kernel_time_t time = kernel_get_time_ms();
        int32_t lowest_val = 0x7fffffff;

        for (int i = 0; i < kernel.task_count; i++) {

            if (i == kernel.current_task) continue;     // do not allow any task to execute twice in a row unless no other tasks are active

            int32_t remaining_time = kernel.task[i].deadline - time;

            if (remaining_time < lowest_val) {

                kernel.current_task = i;
                lowest_val = remaining_time;
            }
        }
    }
}

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - 

// creates new kernel task and initializes its stack
void kernel_create_task(void (*task_handler)(void), uint32_t *stack, uint32_t stack_size, kernel_time_t execution_period) {

    if (stack == 0) return;
    if (stack_size < 128) return;

    if (kernel.task_count >= KERNEL_MAX_TASKS) return;
    if (execution_period == 0) execution_period = 1;

    // set the task Process Stack Pointer to the end of the task's stack and allocate 17 words for preloading the exception frame
    uint32_t* task_psp = stack + (stack_size / sizeof(uint32_t)) - 17;//kernel.tasks[kernel.task_count].stack + KERNEL_TASK_STACK_SIZE - 17;

    // we need to preload the exception frame with data to be retreived by the __kernel_switch_to_task() function to properly enter into the task entry point
    *(task_psp + 16) = (uint32_t) 0x01000000;                       // this becomes the PSR (Program Status Register) upon first entry to the task (only Thumb bit set)
    *(task_psp + 15) = (uint32_t) task_handler;                     // this becomes the Program Counter upon first entry to the task (task entry point)
    *(task_psp + 14) = (uint32_t) __kernel_terminate_current_task;  // this becomes the LR upon first entry to the task. Returning from the task will automatically terminate it.
    *(task_psp +  8) = (uint32_t) KERNEL_EXCEPTION_RETURN_VALUE;    // this becomes the LR before entry to the task. Branching to KERNEL_EXCEPTION_RETURN_VALUE will cause the processor to pop the exception frame and restore the process state
    // the rest of the exception frame is general purpose registers and they remain unitialized until being saved after switching from the task
    
    kernel.task[kernel.task_count].psp = task_psp;
    kernel.task[kernel.task_count].period = execution_period;
    kernel.task[kernel.task_count].deadline = kernel_get_time_ms() + execution_period;
    kernel.task_count++;
}

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - 

// sets the current task's maximum execution period [ms]. Kernel makes sure the task's execution is resumed within this window to avoid deadline misses
void kernel_set_execution_period(uint32_t period_ms) {

    if (period_ms == 0) period_ms = 1;
    kernel.task[kernel.current_task].period = period_ms;
}

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - 

// suspends current task for the specified time
void kernel_sleep_ms(kernel_time_t duration_ms) {

    kernel_time_t start = kernel_get_time_ms();
    uint32_t prev_period = kernel.task[kernel.current_task].period;
    kernel.task[kernel.current_task].period = duration_ms;     // temporarily change execution period to make sure we don't miss the sleep duration end

    do {

        kernel_yield();

    } while (kernel_get_time_since(start) < duration_ms);

    kernel.task[kernel.current_task].period = prev_period;
}

//---- INTERNAL FUNCTIONS ----------------------------------------------------------------------------------------------------------------------------------------

// updates deadlines of all task based on their execution period
// when the CPU utilization is bellow 100%, the tasks get resumed well before deadline. This causes the deadline variable to increase in value faster than the system time counter
// the deadline variable eventually overflows and due to the properties of subtraction it is not a problem. Howewer, the deadline variable overflowing twice before the system timer overflows is a problem
// this function is called after detecting the first overflow of any task's deadline variable. It prevents double-overflow by putting the variables back to lower values close to the system timer.
void __kernel_init_deadlines(void) {

    kernel_time_t time = kernel_get_time_ms();
    for (int i = 0; i < kernel.task_count; i++) kernel.task[i].deadline = time + kernel.task[i].period;
}

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - 

// exit vector for a task
void __kernel_terminate_current_task(void) {

    kernel.task[kernel.current_task].period = 10000;

    while (1) {

        kernel_yield();
    }
}

//---- INTERRUPT HANDLERS ----------------------------------------------------------------------------------------------------------------------------------------

// triggered every millisecond to increment the system time counter
void SysTick_Handler() {

    kernel_time_ms++;
}

//----------------------------------------------------------------------------------------------------------------------------------------------------------------
