
#ifndef __TASKH__
#define __TASKH__

#include "defs.h"

#define C_CLOCK         32


typedef byte_t (*task_t)(byte_t);

/**
 * Task structure.
 */
typedef struct {
   task_t fun;                ///< Task address.
   byte_t par;                ///< Parameter value.
   byte_t tmr;                ///< Time to wait before calling.
  char name[8+1];             ///< Name of task
} tid_t;

/**
 * Maximum pending tasks.
 */
#define NTASKS             8

/**
 * Task list.
 */
extern volatile tid_t _tasks[NTASKS];

extern volatile _uint32_t ticks;

/*
 * Timing helper macros.
 */
#define timer_t unsigned long int
#define start_timer(X) (X+ticks.d)
#define timeout(X) (ticks.d>X?1:0)

extern bool_t wdt_tick;
extern void tick();
extern void task_init();
extern void task_periodic(void);
extern bool_t task_add(task_t f, byte_t tempo, byte_t par,char *name);
extern bool_t task_cancel(task_t f);
extern void task_cancel_all();

#endif
