#ifndef _KERNEL_H_
#define _KERNEL_H_

/*
 *  Mini Kernel
 *  Lightweight cooperative real-time operating system kernel
 *  Martin Kopka 2023
*/ 

#include "stdint.h"
#include "stdbool.h"
#include "common_defs.h"    // this header must include the hardware platform's headers as well as the "CMSIS/core_m.." header file of the used core

//---------------------------------------------------------------------------------------------------------------------------------------------------------------

typedef uint32_t kernel_time_t;     // data type used for storing kernel time

//---- FUNCTIONS -------------------------------------------------------------------------------------------------------------------------------------------------

// initializes the OS kernel. Kernel needs to know the core frequency to properly setup the SysTick timer
void kernel_init(uint32_t core_clock_frequency_hz);

// creates new kernel task and initializes its stack
// CAUTION: this function can only be called before the kernel starts (before calling kernel_start())
void kernel_create_task(void (*task_handler)(void), uint32_t *stack, uint32_t stack_size, kernel_time_t execution_period);

// starts the OS kernel
void kernel_start(void);

// suspends current task for the specified time
void kernel_sleep_ms(kernel_time_t duration_ms);

// returns time since start [ms]
static inline kernel_time_t kernel_get_time_ms(void)  {
    
    extern volatile kernel_time_t kernel_time_ms;
    return kernel_time_ms;
}

// returns time since start [us] (slow)
uint64_t kernel_get_time_us(void);

// returns system time passed since previous time [ms]
static inline kernel_time_t kernel_get_time_since(kernel_time_t since)  {return (kernel_get_time_ms() - since);}

// yield execution to the kernel
static inline void kernel_yield(void) {asm("svc 0");}

//----------------------------------------------------------------------------------------------------------------------------------------------------------------

#endif /* _KERNEL_H_ */