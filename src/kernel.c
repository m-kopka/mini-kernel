#include "kernel.h"

/*
 *  Mini Kernel
 *  Lightweight cooperative real-time operating system kernel
 *  Martin Kopka 2023
*/ 

//---- CONSTANTS -------------------------------------------------------------------------------------------------------------------------------------------------

#define KERNEL_MAX_TASKS    32  // max task count

// return to Thread mode and use the process stack for return
#define KERNEL_EXCEPTION_RETURN_VALUE 0xFFFFFFFD

// on task creation, the kernel stores this magic value at the bottom of its stack
// if the task overflows its stack, this value gets overvritten and the kernel detects it
#define KERNEL_STACK_CANARY_VALUE 0x4BC8AE00

//---- INTERNAL FUNCTION PROTOTYPES ------------------------------------------------------------------------------------------------------------------------------

void          __kernel_init_stack(uint32_t *stack);
uint32_t*     __kernel_switch_to_task(uint32_t *task_stack);
uint32_t      __compute_queue_size(void);
kernel_time_t __compute_lcm(kernel_time_t a, kernel_time_t b);
kernel_time_t __compute_gcd(kernel_time_t a, kernel_time_t b);
void          __kernel_terminate_current_task(void);

//---- STRUCTS ---------------------------------------------------------------------------------------------------------------------------------------------------

typedef struct {

    uint32_t *psp;          // task's process stack pointer (caution: the stack must stay allocated)
    uint32_t *stack_end;    // end of task's stack, used in stack overflow detection
    kernel_time_t period;   // execution period, kernel tries to resume task's execution within this time window [ms]

} kernel_task_t;

//---- INTERNAL DATA ---------------------------------------------------------------------------------------------------------------------------------------------

struct kernel_internals_t {

    kernel_task_t task[KERNEL_MAX_TASKS];
    uint32_t      task_count;

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
    NVIC_SetPriority(SysTick_IRQn, 15);

    // set SVC priority to low
    NVIC_SetPriority(SVCall_IRQn, 15);
}

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - 

// creates new kernel task and initializes its stack
// CAUTION: this function can only be called before the kernel starts (before calling kernel_start())
void kernel_create_task(void (*task_handler)(void), uint32_t *stack, uint32_t stack_size, kernel_time_t execution_period) {

    if (stack == 0) return;
    if (stack_size < 64) return;

    if (kernel.task_count >= KERNEL_MAX_TASKS) return;
    if (execution_period == 0) execution_period = 1;

    // set the task Process Stack Pointer to the end of the task's stack and allocate 17 words for preloading the exception frame
    uint32_t* task_psp = stack + (stack_size / sizeof(uint32_t)) - 17;

    // we need to preload the exception frame with data to be retreived by the __kernel_switch_to_task() function to properly enter into the task entry point
    *(task_psp + 16) = (uint32_t) 0x01000000;                       // this becomes the PSR (Program Status Register) upon first entry to the task (only Thumb bit set)
    *(task_psp + 15) = (uint32_t) task_handler;                     // this becomes the Program Counter upon first entry to the task (task entry point)
    *(task_psp + 14) = (uint32_t) __kernel_terminate_current_task;  // this becomes the LR upon first entry to the task. Returning from the task will automatically terminate it.
    *(task_psp +  8) = (uint32_t) KERNEL_EXCEPTION_RETURN_VALUE;    // this becomes the LR before entry to the task. Branching to KERNEL_EXCEPTION_RETURN_VALUE will cause the processor to pop the exception frame and restore the process state
    // the rest of the exception frame is general purpose registers and they remain unitialized until being saved after switching from the task
    
    // store the stack canary at the end of the task's stack
    *(stack + 0) = KERNEL_STACK_CANARY_VALUE;
    
    kernel.task[kernel.task_count].psp = task_psp;
    kernel.task[kernel.task_count].stack_end = stack;
    kernel.task[kernel.task_count].period = execution_period;
    kernel.task_count++;
}

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - 

// starts the OS kernel
void kernel_start(void) {

    if (kernel.task_count == 0) while (1);      // no tasks to run

    // initialize deadlines
    kernel_time_t task_deadline[kernel.task_count];
    for (int i = 0; i < kernel.task_count; i++) task_deadline[i] = kernel.task[i].period;

    // calculate the hyper-period (number of cycles after which the execution pattern repeats) and allocate a queue of the required size
    uint32_t queue_size = __compute_queue_size();
    uint8_t task_queue[queue_size];
    
    // loop through the queue and fill it
    for (int queue_pos = 0; queue_pos < queue_size; queue_pos++) {

        // find the task with the earliest deadline
        uint32_t earliest_deadline = 0xffffffff;
        uint8_t next_task_index = 0;

        for (int task_index = 0; task_index < kernel.task_count; task_index++) {

            if (task_deadline[task_index] < earliest_deadline) {

                next_task_index = task_index;
                earliest_deadline = task_deadline[task_index];
            }
        }

        task_queue[queue_pos] = next_task_index;                                    // add the selected task to the queue
        task_deadline[next_task_index] += kernel.task[next_task_index].period;      // simulate execution of selected task to select a next one in the next iteration
    }

    // kernel main loop
    // task_queue is periodic and as long as the execution periods don't change and no new tasks are added, it remains constant
    while (1) {

        for (int queue_pos = 0; queue_pos < queue_size; queue_pos++) {

            // resume execution of the next task in the queue
            kernel.task[task_queue[queue_pos]].psp = __kernel_switch_to_task(kernel.task[task_queue[queue_pos]].psp);

            // check if the stack canary was overwritten
            if (*kernel.task[task_queue[queue_pos]].stack_end != KERNEL_STACK_CANARY_VALUE) return;
        }
    }
}

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - 

// suspends current task for the specified time
void kernel_sleep_ms(kernel_time_t duration_ms) {

    kernel_time_t start = kernel_get_time_ms();

    do {

        kernel_yield();

    } while (kernel_get_time_since_ms(start) < duration_ms);
}

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - 

// returns time since start [us] (slow)
uint64_t kernel_get_time_us(void) {

    uint32_t irq_status = __get_PRIMASK();
    __disable_irq();    // disable interrupts to avoid corrupted values

    // read the completed milliseconds + 1 and subtract the remaining time callculated from the progress of the SysTick timer
    uint64_t time_us = (((uint64_t)kernel_get_time_ms() + 1) * 1000) - (SysTick->VAL * 1000 / SysTick->LOAD);

    __set_PRIMASK(irq_status);   // return the interrupt enable to the previous value 

    return (time_us);
}

//---- INTERNAL FUNCTIONS ----------------------------------------------------------------------------------------------------------------------------------------

// returns the hyper-period (number of context switches after which the scheduling pattern repeats)
// this is used to calculate the necessary task queue size
uint32_t __compute_queue_size(void) {

    // compute the least common multiplier of all task periods
    kernel_time_t lcm = kernel.task[0].period;
    for (int i = 1; i < kernel.task_count; i++) lcm = __compute_lcm(lcm, kernel.task[i].period);

    // after a hyper-period, the deadline of each task is period + lcm (deadline grows by lcm)
    // thus, by dividing the final value (period + lcm) by period and subtracting one, we get a number of executions in a hyper-period (per each task)
    // by summing up all the numbers of task executions, we get the total number of kernel executions in a hyper-period
    uint32_t queue_size = 0;
    for (int i = 0; i < kernel.task_count; i++) queue_size += (kernel.task[i].period + lcm) / kernel.task[i].period - 1;

    return (queue_size);
}

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - 

// returns the least common multiplier of two numbers, used in the queue size calculation
kernel_time_t __compute_lcm(kernel_time_t a, kernel_time_t b) {

    return ((a * b) / __compute_gcd(a, b));
}

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - 

// returns the greatest common denominator of two numbers using the Euclidian algorithm, used in the lcm calculation
kernel_time_t __compute_gcd(kernel_time_t a, kernel_time_t b) {

    while (b != 0) {

        kernel_time_t temp = b;
        b = a % b;
        a = temp;
    }

    return (a);
}

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - 

// exit vector for a task
void __kernel_terminate_current_task(void) {

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
