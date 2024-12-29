#ifndef _KERNEL_TIMER_H_
#define _KERNEL_TIMER_H_

/*
 *  Mini Kernel
 *  Timer kernel service
 *  Martin Kopka 2024
*/ 

#include "kernel.h"

//---- STRUCTS --------------------------------------------------------------------------------------------------------------------------------------------------

// kernel timer struct
typedef struct {

    kernel_time_t start_time;   // absolute time of timer start
    kernel_time_t duration_ms;  // timer duration [ms], value of 0 means the timer is stopped

} kernel_timer_t;

//---- FUNCTIONS -------------------------------------------------------------------------------------------------------------------------------------------------

// starts a timer
static inline void kernel_timer_start(kernel_timer_t *timer, kernel_time_t duration_ms) {

    timer->duration_ms = duration_ms;
    timer->start_time = kernel_get_time_ms();
}

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - 

// stops a timer
static inline void kernel_timer_stop(kernel_timer_t *timer) {

    timer->duration_ms = 0;
}

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - 

// returns time since the timer start [ms], returns 0 if the timer is stopped
static inline kernel_time_t kernel_timer_get_time_ms(kernel_timer_t *timer) {

    if (timer->duration_ms == 0) return 0;
    return (kernel_get_time_since_ms(timer->start_time));
}

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - 

// returns true if the timer is expired, a stopped timer is always considered not expired
static inline bool kernel_timer_is_expired(kernel_timer_t *timer) {

    if (timer->duration_ms == 0) return false;
    return (kernel_get_time_since_ms(timer->start_time) >= timer->duration_ms);
}

//----------------------------------------------------------------------------------------------------------------------------------------------------------------

#endif /* _KERNEL_TIMER_H_ */