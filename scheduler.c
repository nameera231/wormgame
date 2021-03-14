#define _XOPEN_SOURCE
#define _XOPEN_SOURCE_EXTENDED

#include "scheduler.h"

#include <assert.h>
#include <curses.h>
#include <ucontext.h>

#include "util.h"

// This is an upper limit on the number of tasks we can create.
#define MAX_TASKS 128

// This is the size of each task's stack memory
#define STACK_SIZE 65536

// This struct will hold the all the necessary information for each task
typedef struct task_info {

  // This field stores all the state required to switch back to this task
  ucontext_t context;

  // This field stores another context. This one is only used when the task
  // is exiting.
  ucontext_t exit_context;
  
  // TODO: Add fields here so you can:
  //   a. Keep track of this task's state.
  //   b. If the task is sleeping, when should it wake up?
  //   c. If the task is waiting for another task, which task is it waiting for?
  //   d. Was the task blocked waiting for user input? Once you successfully
  //      read input, you will need to save it here so it can be returned.
  //block = 0 -- unblocked
  //block = 1 -- blocked for sleep
  //block = 2 -- blocked for waiting 
  //block = 3 -- blocked for input
  int blocked;
  //for exit status use bool
  bool exit;
  size_t wakeup_time;
  int input;
  int index;
  task_t wait_index;
} task_info_t;

#define handle_error(msg) \
do { perror(msg); exit(EXIT_FAILURE); } while (0)

int current_task = 0; //< The handle of the currently-executing task
int num_tasks = 1;    //< The number of tasks created so far
task_info_t tasks[MAX_TASKS]; // Information for every task

void switcher(ucontext_t* current) {
  int i;
  if(current_task + 1 < num_tasks) {
    i = current_task + 1;
  } else {
      i =0;
    }

  int status = tasks[i].blocked;

  while(i < num_tasks) {

    status = tasks[i].blocked;   
    // check if the current task is unblocked
    // if it is unblocked, run the current task
    if (status == 0 && tasks[i].exit == false) {
      current_task = tasks[i].index;
      status = 0;
      swapcontext(current, &tasks[i].context);
      return;
          
      // check if the current task is blocked for sleep 
    } else if (status == 1 && tasks[i].exit == false) {
        // if the current task is no longer sleeping, run the current task
        // unblock the current task
        if (time_ms() >= tasks[i].wakeup_time) {
          tasks[i].wakeup_time = 0;
          tasks[i].blocked = 0;
          status = 0;
          current_task = tasks[i].index;
          swapcontext(current, &tasks[i].context);
          return;
        }
          
      // check if the current task is blocked for waiting
    } else if (status == 2 && tasks[i].exit == false) {
        // if the task the current task is waiting for has exited, run the current task
        // unblock the current task
          if (tasks[tasks[i].wait_index].exit) {
            tasks[i].blocked = 0;
            status = 0;
            current_task = tasks[i].index;
            swapcontext(current, &tasks[i].context);
            return;
          }

          // check if the current task is blocked for input   
    } else if (status == 3 && tasks[i].exit == false) {
           
        // if no input is found, attempt to read a new input
        int next_input = getch();
        // if an input is found, run the current task
        // unblock the current task
        if(next_input != ERR) {
          tasks[i].blocked = 0;
          status = 0;
          tasks[i].input = next_input;
          current_task = tasks[i].index;
          swapcontext(current, &tasks[i].context);
          return;
        }
      }
      
        i++;
        //keep looping till we find an unblocked tasks
        if (i >= num_tasks) {
          i = 0;
        }
  } 
}

/**
 * Initialize the scheduler. Programs should call this before calling any other
 * functions in this file.
 */
void scheduler_init() {
   
}

/**
 * This function will execute when a task's function returns. This allows you
 * to update scheduler states and start another task. This function is run
 * because of how the contexts are set up in the task_create function.
 */
void task_exit() {
  // TODO: Handle the end of a task's execution here
  //exit the task
  tasks[current_task].exit = true;
  //call switcher to find next task
  switcher(&tasks[current_task].context);
}

/**
 * Create a new task and add it to the scheduler.
 *
 * \param handle  The handle for this task will be written to this location.
 * \param fn      The new task will run this function.
 */
void task_create(task_t* handle, task_fn_t fn) {
  // Claim an index for the new task
  int index = num_tasks;
  num_tasks++;
  
  // Set the task handle to this index, since task_t is just an int
  *handle = index;
 
  // We're going to make two contexts: one to run the task, and one that runs at the end of the task so we can clean up. Start with the second
  
  // First, duplicate the current context as a starting point
  getcontext(&tasks[index].exit_context);
  
  // Set up a stack for the exit context
  tasks[index].exit_context.uc_stack.ss_sp = malloc(STACK_SIZE);
  tasks[index].exit_context.uc_stack.ss_size = STACK_SIZE;
  
  // Set up a context to run when the task function returns. This should call task_exit.
  makecontext(&tasks[index].exit_context, task_exit, 0);
  
  // Now we start with the task's actual running context
  getcontext(&tasks[index].context);
  
  // Allocate a stack for the new task and add it to the context
  tasks[index].context.uc_stack.ss_sp = malloc(STACK_SIZE);
  tasks[index].context.uc_stack.ss_size = STACK_SIZE;
  
  // Now set the uc_link field, which sets things up so our task will go to the exit context when the task function finishes
  tasks[index].context.uc_link = &tasks[index].exit_context;
  
  
  // And finally, set up the context to execute the task function
  makecontext(&tasks[index].context, fn, 0);
  
  //initialize other fields
  tasks[index].blocked = 0;
  tasks[index].wakeup_time = 0;
  tasks[index].input = -1;
  tasks[index].index = index;
  tasks[index].exit = false;
}

/**
 * Wait for a task to finish. If the task has not yet finished, the scheduler should
 * suspend this task and wake it up later when the task specified by handle has exited.
 *
 * \param handle  This is the handle produced by task_create
 */
void task_wait(task_t handle) {
  // TODO: Block this task until the specified task has exited.
  //puts("task wait\n");
  if(tasks[handle].exit == true) {
    tasks[current_task].blocked = 0;
    //puts("wait exit");
    return;
  } else {
      tasks[current_task].blocked = 2;
      tasks[current_task].wait_index = handle;
      switcher(&tasks[current_task].context); 
      return;
    }
}
  
/**
 * The currently-executing task should sleep for a specified time. If that time is larger
 * than zero, the scheduler should suspend this task and run a different task until at least
 * ms milliseconds have elapsed.
 * 
 * \param ms  The number of milliseconds the task should sleep.
 */
void task_sleep(size_t ms) {
  // TODO: Block this task until the requested time has elapsed.
  // Hint: Record the time the task should wake up instead of the time left for it to sleep. The bookkeeping is easier this way.
  //wakeup time = ms; --add current to wait time and just wakeup when it is equal
  tasks[current_task].wakeup_time = time_ms() + ms;
  tasks[current_task].blocked = 1;
  switcher(&tasks[current_task].context);
  return;
}

/**
 * Read a character from user input. If no input is available, the task should
 * block until input becomes available. The scheduler should run a different
 * task while this task is blocked.
 *
 * \returns The read character code
 */
int task_readchar() {
  // TODO: Block this task until there is input available.
  // To check for input, call getch(). If it returns ERR, no input was available.
  int i = getch();
  if(i == ERR) {
    tasks[current_task].blocked = 3;
    switcher(&tasks[current_task].context);
    return tasks[current_task].input;  
  } else {
      // Otherwise return the character code that was read.
    return i;
    }
}
