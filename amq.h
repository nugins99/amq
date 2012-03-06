#ifndef AEMQ_H
#define AEMQ_H


/**
 * A Message Queue Library.   
 *
 *  This library is intended to be used when message queues are needed 
 *  but a posix message queue is too much.   The API is intended to mimic 
 *  the posix message queue library to some extend.  
 */ 
#ifdef VXWORKS
/* For vxWorks,  just delegate to the posix versions */
#define USE_POSIX_QUEUE
#endif

#ifdef USE_POSIX_QUEUE
#define amqd_t mqd_t
#define amq_open mq_open
#define amq_close mq_close
#define amq_send mq_send
#define amq_receive mq_receive
#define amq_notify mq_notify
#define amq_timedreceive mq_timedreceive
#define amq_unlink mq_unlink 
#else 

#include <stdint.h>
#include <sys/types.h>
#include <time.h> /* Timespec */
/*  Define a subset of the posix standard in this 
 *  library 
 */

#ifdef __cplusplus
extern "C" {
#endif


typedef intptr_t amqd_t; 

struct amq_attr {
    long mq_flags; 
    long mq_maxmsg; 
    long mq_msgsize; 
    long mq_curmsgs; 
};

typedef int aemode_t;

/** Open a message queue with a name  */
//XXX amqd_t amq_open(const char *name, int oflag);
//really should be amq_open(const char*, int, ...); 

/** Open a message queue with a name  
 * @see man mq_open
 * @param name - Name of message queue
 * @param oflags - Open flags (Only O_CREAT & O_EXCL are honored, O_RDWR is
 * assumed) 
 * @param mode - Ignored (present for compatibility with mqueue 
 *
 * Opens a new queue if O_CREAT is specified in oflags,  returns a handle 
 * to an existing queue if the named queue already exists.
 *  
 */
amqd_t amq_open(const char *name, int oflags, aemode_t, struct amq_attr * attr); 

/**  close a message queue by mq handler. */
amqd_t amq_close(amqd_t  mqdes);

/** close a queue by name. */ 
amqd_t amq_unlink(const char * path); 

/** send a message */
int amq_send(amqd_t mqdes, const char * msg_ptr, size_t msg_len, 
        unsigned msg_prio);

int amq_timedsend(amqd_t mqdes, const char * msg_ptr, size_t msg_len, 
        unsigned msg_prio, const struct timespec * abs_time); 

/** receive a message */
ssize_t amq_receive(amqd_t mqdes, char*msg_ptr, size_t msg_len, 
        unsigned msg_prio);

ssize_t amq_timedreceive(amqd_t mqdes, char*msg_ptr, size_t msg_len, 
        unsigned msg_prio, const struct timespec * abs_time);

#ifdef __cplusplus
}
#endif


#endif



#endif
