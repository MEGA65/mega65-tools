/**
 * @file task.c
 * @brief Task scheduler and dispatcher.
 * @compiler CC65
 * @author Paul Gardner-Stephen (paul@m-e-g-a.org)
 * based on:
 * @author Bruno Basseto (bruno@wise-ware.org)
 * @version 0.
 */

#include <stdio.h>
#include "debug.h"

// Periodically show the scheduled tasks 
//#define DEBUG_TASKS
// Show each task as it is called
//#define DEBUG_TASK_CALLS
//#define DEBUG_NAMED_TASK "dhcpretry"

/********************************************************************************
 ********************************************************************************
 * The MIT License (MIT)
 *
 * Copyright (c) 1995-2013 Bruno Basseto (bruno@wise-ware.org).
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 ********************************************************************************
 ********************************************************************************/

#include <string.h>
#include "task.h"

/**
 * Go into sleep mode when all tasks were executed.
 */
//#define _SLEEP

/**
 * Time counter.
 */
volatile _uint32_t ticks;

/*
 *  Exponential byte timing for a 10ms timebase (in seconds).
 * .===========================================================================.
 * |     |   0  |   1  |   2  |   3  |   4  |   5  |   6  |   7  |   8  |   9  |
 * |===========================================================================|
 * | 000 |    0 |  .01 |  .02 |  .03 |  .04 |  .05 |  .06 |  .07 |  .08 |  .09 |
 * | 010 |   .1 |  .11 |  .12 |  .13 |  .14 |  .15 |  .17 |  .19 |  .21 |  .23 |
 * | 020 |  .25 |  .27 |  .29 |  .31 |  .33 |  .35 |  .37 |  .39 |  .41 |  .43 |
 * | 030 |  .45 |  .47 |  .51 |  .55 |  .59 |  .63 |  .67 |  .71 |  .75 |  .79 |
 * | 040 |  .83 |  .87 |  .91 |  .95 |  .99 | 1.03 | 1.07 | 1.11 | 1.15 | 1.19 |
 * | 050 | 1.23 | 1.27 | 1.31 | 1.35 | 1.39 | 1.43 | 1.47 | 1.51 | 1.55 | 1.59 |
 * | 060 | 1.63 | 1.67 | 1.71 | 1.75 | 1.83 | 1.91 | 1.99 | 2.07 | 2.15 | 2.23 |
 * | 070 | 2.31 | 2.39 | 2.47 | 2.55 | 2.63 | 2.71 | 2.79 | 2.87 | 2.95 | 3.03 |
 * | 080 | 3.11 | 3.19 | 3.27 | 3.35 | 3.43 | 3.51 | 3.59 | 3.67 | 3.75 | 3.83 |
 * | 090 | 3.91 | 3.99 | 4.07 | 4.15 | 4.23 | 4.31 | 4.39 | 4.47 | 4.55 | 4.63 |
 * | 100 | 4.71 | 4.79 | 4.87 | 4.95 | 5.03 | 5.11 | 5.19 | 5.27 | 5.35 | 5.43 |
 * | 110 | 5.51 | 5.59 | 5.67 | 5.75 | 5.83 | 5.91 | 5.99 | 6.07 | 6.15 | 6.23 |
 * | 120 | 6.31 | 6.39 | 6.47 | 6.55 | 6.63 | 6.71 | 6.79 | 6.87 | 7.03 | 7.19 |
 * | 130 | 7.35 | 7.51 | 7.67 | 7.83 | 7.99 | 8.15 | 8.31 | 8.47 | 8.63 | 8.79 |
 * | 140 | 8.95 | 9.11 | 9.27 | 9.43 | 9.59 | 9.75 | 9.91 | 10.1 | 10.2 | 10.4 |
 * | 150 | 10.5 | 10.7 | 10.9 | 11.0 | 11.2 | 11.3 | 11.5 | 11.7 | 11.8 | 12.0 |
 * | 160 | 12.1 | 12.3 | 12.5 | 12.6 | 12.8 | 12.9 | 13.1 | 13.3 | 13.4 | 13.6 |
 * | 170 | 13.7 | 13.9 | 14.1 | 14.2 | 14.4 | 14.5 | 14.7 | 14.9 | 15.0 | 15.2 |
 * | 180 | 15.3 | 15.5 | 15.7 | 15.8 | 16.0 | 16.1 | 16.3 | 16.5 | 16.6 | 16.8 |
 * | 190 | 16.9 | 17.1 | 17.3 | 17.4 | 17.6 | 17.7 | 17.9 | 18.1 | 18.2 | 18.4 |
 * | 200 | 18.5 | 18.7 | 18.9 | 19.0 | 19.2 | 19.3 | 19.5 | 19.7 | 19.8 | 20.0 |
 * | 210 | 20.1 | 20.3 | 20.5 | 20.6 | 20.8 | 20.9 | 21.1 | 21.3 | 21.4 | 21.6 |
 * | 220 | 21.7 | 21.9 | 22.1 | 22.2 | 22.4 | 22.5 | 22.7 | 22.9 | 23.0 | 23.2 |
 * | 230 | 23.3 | 23.5 | 23.7 | 23.8 | 24.0 | 24.1 | 24.3 | 24.5 | 24.6 | 24.8 |
 * | 240 | 24.9 | 25.1 | 25.3 | 25.4 | 25.6 | 25.7 | 25.9 | 26.1 | 26.2 | 26.4 |
 * | 250 | 26.5 | 26.7 | 26.9 | 27.0 | 27.2 | 27.3 |      |      |      |      |
 * '==========================================================================='
 */

/**
 * Task list.
 */
volatile tid_t _tasks[NTASKS];

/**
 * Sleep task.
 */
static task_t _task_sleep = NULL;

/**
 * Define a task to be run during sleep time.
 * It will be called after the periodic action of the watchdog-timer.
 * @param f Task address.
 */
void
task_sleep
   (task_t f)
{
   _task_sleep = f;
}

/**
 * Create a task for execution.
 * @param f Task address.
 * @param time Time to call (0 = immediate).
 * @param par Task parameter.
 * @return TRUE if successful.
 */
bool_t 
task_add
   (task_t f, 
    byte_t tempo, 
    byte_t par,
    char *name)
{
   bool_t ok;
   volatile tid_t *task;
   uint8_t j;

   if(f == NULL) return FALSE;

   // Don't allow same task to be multiply scheduled
   task_cancel(f);
   
   /*
    * Search the task list for an empty slot.
    */
   ok = FALSE;
   for(j=0;j<NTASKS;j++) {
     task=&_tasks[j];
     if(task->fun == NULL) {
       /*
	* Empty slot found.
	* Save task information.
	*/
       task->fun = f;
       task->par = par;
       task->tmr = tempo;
       strncpy(task->name,name,8);
       task->name[8]=0;
       ok = TRUE;
       break;
     }
   }

   return ok;
}

/**
 * Cancel the execution of a task.
 * @param f Task address.
 * @return FALSE if the task was already called.
 */
bool_t 
task_cancel
   (task_t f)
{
  volatile tid_t *task;
   bool_t ok;
   
   if(f == NULL) return FALSE;

   /*
    * Search for the task in the list.
    */
   ok = FALSE;
   for_each(_tasks, task) {
      if(task->fun == f) {
         /*
          * Found. Remove it.
          */
         task->fun = NULL;
         ok = TRUE;
         break;
      }
   }

   return ok;
}

/**
 * Cancel all tasks.
 */
void 
task_cancel_all()
{
  volatile tid_t *task;
  //  printf("Cancel all tasks.\n");
   for_each(_tasks, task) {
      task->fun = NULL;
   }
}

/**
 * Time interrupt handler.
 * Must be called by the interrupt thread.
 */
void 
tick()
{
  volatile static tid_t *task;
   static byte_t tmp;

   /*
    * Update timing information.
    */
   ticks.d++;

   /*
    * Update task timing information.
    */
   for_each(_tasks, task) {
      if(task->fun == NULL) continue;              // unused.

      tmp = task->tmr;
      if(tmp == 0) continue;                       // no timing.

      /*
       * Exponential time algorithm.
       */
      if(tmp & 0x80) {
         if(ticks.b[0] & 0x0f) continue;           // each 16 ticks.
      } else if(tmp & 0xc0) {
         if(ticks.b[0] & 0x07) continue;           // each 8 ticks.
      } else if(tmp & 0xe0) {
         if(ticks.b[0] & 0x03) continue;           // each 4 ticks.
      } else if(tmp & 0xf0) {
         if(ticks.b[0] & 0x01) continue;           // each 2 ticks.
      }
      task->tmr = tmp-1;
   }
}

/**
 * Task management system initialization.
 */
void 
task_init()
{
   /*
    * Setup data structures.
    */
   memset((void*)_tasks, 0, sizeof(_tasks));

}

/**
 * Main loop.
 * Schedule tasks as needed.
 */
void task_periodic(void)
{
   static byte_t (*f)(byte_t);
   volatile static tid_t *task;
   static bool_t exe;
#ifdef DEBUG_TASK_CALLS
   static task_t last_task_fun = NULL;
   static unsigned char rev_toggle = 0x00;
#endif
#ifdef DEBUG_TASKS
   static char show_task_list_n=0;
   if (!show_task_list_n) printf("Tasks=");
#endif
   
   /*
    * Call pending tasks.
    */
   exe = FALSE;
   for_each(_tasks, task) {
     f = task->fun;
     if(f == NULL) continue;

#ifdef DEBUG_TASKS
     if (!show_task_list_n) printf("%s,",task->name);
#endif
     
     /*
      * Check task timing.
      */
     exe = TRUE;
     if(task->tmr) continue;

#ifdef DEBUG_TASK_CALLS
#ifdef DEBUG_NAMED_TASK
     if (strstr(DEBUG_NAMED_TASK,task->name)) {
#endif
     if (task->fun!=last_task_fun) {
       printf("[Task:%s]",task->name);
       last_task_fun=task->fun;
     } else {
       // Show blinking cursor as function is repeatedly called
       printf("%c %c%c",0x12+rev_toggle,0x9d,0x92);
       rev_toggle^=0x80;
     }
#ifdef DEBUG_NAMED_TASK
     }
#endif
#endif
     /*
      * Task is ready.
      * Remove from the list and run.
      */
     task->fun = NULL;
     f(task->par);
   }
   tick();
#ifdef DEBUG_TASKS
   if (!show_task_list_n) printf("\n");
#endif
}
