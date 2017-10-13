/* Tests cetegorical mutual exclusion with different numbers of threads.
 * Automatic checks only catch severe problems like crashes.
 */
#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "lib/random.h" //generate random numbers

#define BUS_CAPACITY 3
#define SENDER 0
#define RECEIVER 1
#define NORMAL 0
#define HIGH 1

/*
 *	initialize task with direction and priority
 *	call o
 * */
typedef struct {
	int direction;
	int priority;
} task_t;

void batchScheduler(unsigned int num_tasks_send, unsigned int num_task_receive,
        unsigned int num_priority_send, unsigned int num_priority_receive);

void senderTask(void *);
void receiverTask(void *);
void senderPriorityTask(void *);
void receiverPriorityTask(void *);


void oneTask(task_t task);/*Task requires to use the bus and executes methods below*/
	void getSlot(task_t task); /* task tries to use slot on the bus */
	void transferData(task_t task); /* task processes data on the bus either sending or receiving based on the direction*/
	void leaveSlot(task_t task); /* task release the slot */

struct semaphore send, receive;
struct condition wait;
struct lock condition_lock, send_lock, receive_lock;
int send_priority_num, receive_priority_num;
bool senderprio, recprio;

/* initializes semaphores */ 
void init_bus(void){ 
 
    random_init((unsigned int)123456789); 
    
    sema_init(&send, BUS_CAPACITY);
    sema_init(&receive, BUS_CAPACITY);
    cond_init(&wait);
    lock_init(&condition_lock);
    lock_init(&send_lock);
    lock_init(&receive_lock);
}

/*
 *  Creates a memory bus sub-system  with num_tasks_send + num_priority_send
 *  sending data to the accelerator and num_task_receive + num_priority_receive tasks
 *  reading data/results from the accelerator.
 *
 *  Every task is represented by its own thread. 
 *  Task requires and gets slot on bus system (1)
 *  process data and the bus (2)
 *  Leave the bus (3).
 */

void batchScheduler(unsigned int num_tasks_send, unsigned int num_task_receive,
        unsigned int num_priority_send, unsigned int num_priority_receive)
{
    unsigned int i;
    char name[16];
    
    
    send_priority_num    = 0;
    receive_priority_num = 0;   

    for(i=0; i < num_priority_send; i++)
    {
        snprintf (name, sizeof name, "PriThread %d", i);
        thread_create(name, HIGH, senderPriorityTask, NULL);
    }    
    for(i= 0; i < num_priority_receive; i++)
    {
        snprintf (name, sizeof name, "PriRecThread %d", i);
        thread_create(name,HIGH,receiverPriorityTask,NULL);
    }
    for(i= 0; i < num_tasks_send; i++)
    {
        snprintf (name, sizeof name, "normThread %d", i);
        thread_create(name,NORMAL,senderTask,NULL);
    }
    for(i= 0; i < num_task_receive; i++)
    {
        snprintf (name, sizeof name, "normRecThread %d", i);
        thread_create(name,NORMAL,receiverTask,NULL);
    }
}

/* Normal task,  sending data to the accelerator */
void senderTask(void *aux UNUSED){
        task_t task = {SENDER, NORMAL};
        oneTask(task);
}

/* High priority task, sending data to the accelerator */
void senderPriorityTask(void *aux UNUSED){
        task_t task = {SENDER, HIGH};
        oneTask(task);
}

/* Normal task, reading data from the accelerator */
void receiverTask(void *aux UNUSED){
        task_t task = {RECEIVER, NORMAL};
        oneTask(task);   
}

/* High priority task, reading data from the accelerator */
void receiverPriorityTask(void *aux UNUSED){
        task_t task = {RECEIVER, HIGH};
        oneTask(task);
}

/* abstract task execution*/
void oneTask(task_t task) {
  getSlot(task);
  //msg("Transfering DATA DIR:%d PRI:%d \n", task.direction, task.priority);
  transferData(task);
  //msg("DATA transferred\n");
  leaveSlot(task);
}


/* task tries to get slot on the bus subsystem */
void getSlot(task_t task) 
{
    
    lock_acquire(&condition_lock);
    //msg("I'm %d %d \n",task.direction, task.priority);
    /*send all senders with high priority
    if any regular senders can be sent before last hihgh priority can be sent, send that
    this can be done with a try acquire, if you can't acquire, that means you are free to go
    once last high priority sender leave block all other senders by releasing
    */
    if(task.direction == SENDER)
    {    
        lock_acquire(&send_lock);

        if(task.priority == HIGH)
        {
            senderprio = true;
            send_priority_num++;       
            while(receive.value != BUS_CAPACITY)
            {
                lock_release(&send_lock);
                cond_wait(&wait, &condition_lock);
                lock_acquire(&send_lock);
            }
            
            if(!sema_try_down(&send))
            {
                madness_locks(&receive_lock, &send_lock, &condition_lock, &send);
            }
                
            send_priority_num--;
        }
        else
        { 
            while(recprio == true || send_priority_num > 0 || receive.value != BUS_CAPACITY)
            {
                lock_release(&send_lock);
                cond_wait(&wait, &condition_lock);
                lock_acquire(&send_lock);
            }        
            
            if(!sema_try_down(&send))
            {
                madness_locks(&receive_lock, &send_lock, &condition_lock, &send);
            }
        }
        lock_release(&send_lock);
    }
    else
    {
        //msg("Trying to acquire receive_lock");
        lock_acquire(&receive_lock);

        //msg("Acquired receive_lock");
        if(task.priority == HIGH)
        {        
            recprio = true;
            receive_priority_num++;
            while(senderprio == true || send.value != BUS_CAPACITY)
            {
                lock_release(&receive_lock);
                cond_wait(&wait, &condition_lock);
                lock_acquire(&receive_lock);
            }
            msg("GOT HERE");
            if(!sema_try_down(&receive))
            {
                madness_locks(&send_lock, &receive_lock, &condition_lock, &receive);
            }
            receive_priority_num--;
        }
        else
        {
            while(senderprio == true || receive_priority_num > 0 || send.value != BUS_CAPACITY)
            {
                lock_release(&receive_lock);
                cond_wait(&wait, &condition_lock);
                lock_acquire(&receive_lock);
            }

            if(!sema_try_down(&receive))
            {
                madness_locks(&send_lock, &receive_lock, &condition_lock, &receive);
            }
        }
        lock_release(&receive_lock);
    }
    lock_release(&condition_lock);
}

void madness_locks(struct lock *lock1, struct lock *lock2, struct lock *cond_lock, struct semaphore *sema)
{
    lock_acquire(lock1);
    lock_release(lock2);                
    lock_release(cond_lock);
    sema_down(sema);
    lock_acquire(cond_lock);
    lock_acquire(lock2);
    lock_release(lock1);
}


/* task processes data on the bus send/receive */
void transferData(task_t task) 
{
    timer_sleep(random_ulong() % 11);
}

/* task releases the slot */
void leaveSlot(task_t task) 
{   
    msg("outside");
    lock_acquire(&condition_lock);
    msg("I got in");
    if(task.direction == SENDER)
    {
        sema_up(&send);
        if(task.priority == HIGH && send_priority_num == 0)
                senderprio = false;
    }else
    {
        sema_up(&receive);
        if (task.priority == HIGH)
        {
            if(receive_priority_num == 0)
                recprio = false;
        }
    }
    if(send.value == BUS_CAPACITY && receive.value == BUS_CAPACITY)
    {
        msg("broadcast");    
        cond_broadcast(&wait,&condition_lock);
    }
    if(lock_held_by_current_thread(&condition_lock))
        lock_release(&condition_lock);
}


