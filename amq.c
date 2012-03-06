#include "amq.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
typedef struct circular_buffer
{
    void *buffer;     // data buffer
    void *buffer_end; // end of data buffer
    size_t capacity;  // maximum number of items in the buffer
    size_t count;     // number of items in the buffer
    size_t sz;        // size of each item in the buffer
    void *head;       // pointer to head
    void *tail;       // pointer to tail
} circular_buffer;

static void cb_init(circular_buffer *cb, size_t capacity, size_t sz)
{
    cb->buffer = malloc(capacity * sz);
    if(cb->buffer == NULL) {
        fprintf(stderr, "%s:%d: Unable to malloc memory for buffer\n",  
                __FILE__, __LINE__);
    }
    cb->buffer_end = (char *)cb->buffer + capacity * sz;
    cb->capacity = capacity;
    cb->count = 0;
    cb->sz = sz;
    cb->head = cb->buffer;
    cb->tail = cb->buffer;
}

static void cb_free(circular_buffer *cb)
{
    free(cb->buffer);
    // clear out other fields too, just to be safe
}

static void cb_push_back(circular_buffer *cb, const void *item)
{
    assert(cb->count != cb->capacity);
    memcpy(cb->head, item, cb->sz);
    cb->head = (char*)cb->head + cb->sz;
    if(cb->head == cb->buffer_end)
        cb->head = cb->buffer;
    cb->count++;
}

static void cb_pop_front(circular_buffer *cb, void *item)
{
    assert(cb->count > 0); 
    memcpy(item, cb->tail, cb->sz);
    cb->tail = (char*)cb->tail + cb->sz;
    if(cb->tail == cb->buffer_end)
        cb->tail = cb->buffer;
    cb->count--;
}

struct impl_t{
    char * name; 
    circular_buffer  cb; 
    struct impl_t * next; 
    struct impl_t * prev; 
    pthread_mutex_t mutex; 
    pthread_cond_t cond; 
    struct data_t * tmp;
    int use_count; 
} ; 

struct data_t{
    size_t size; 
    char data[1]; 
} ;

static struct impl_t * queues = NULL;

static struct impl_t * find_queue(const char * name) {
    struct impl_t * p = queues; 
    while (p != NULL) {
        if ((strcmp(name, p->name)) == 0) {
            break; 
        }
        p = p->next; 
    }
    return p;  
}

#if 0 
/** Open a message queue with a name  */
amqd_t amqd_open(const char *name, int oflag) {
    struct impl_t * q;
    q = find_queue(name);
    if (q == NULL) {
        return -1; 
    } else {
        q->use_count++; 
        return (amqd_t) q; 
    }
}
#endif 

amqd_t amq_open(const char *name, int oflags, aemode_t mode, struct amq_attr * attr) {
    struct impl_t * q;
    q = find_queue(name);

    // create new queue if not found 
    if (q == NULL) {
        if (oflags & O_CREAT) { 
            q = (struct impl_t*) malloc(sizeof(struct impl_t));
            if (q == NULL) {
                return -1;
            }
            q->name = (char*) malloc(strlen(name) +1); 
            strcpy(q->name, name);
            q->next = NULL;
            q->prev = NULL;
            q->use_count = 1; 
            /* allocate mem for temp buffer */
            q->tmp= (struct data_t*)  malloc(attr->mq_msgsize + sizeof(size_t)); 
            pthread_mutex_init(&q->mutex, 0);
            pthread_cond_init(&q->cond,0);

            cb_init(&q->cb,attr->mq_maxmsg, attr->mq_msgsize + sizeof(size_t) );
            // insert new queue at front 
            q->next = queues; 
            queues = q; 
        } else {
            errno = ENOENT; 
            return -1; 
        }
    } else {
        q->use_count++; 
    }
    return (amqd_t) q; 
}

/**  close a message queue */
amqd_t amq_close(amqd_t mqdes) {
    struct impl_t * q = (struct impl_t *) mqdes; 
    q->use_count--;
    if (q->use_count == 0) {
        /* unlink */ 
        if (queues == q) {
            queues->prev = NULL;
            queues = q->next; 
        } else {
            if (q->prev)
                q->prev->next = q->next; 
            if (q->next)
                q->next->prev = q->prev; 
        }
        // free memory
        cb_free(&q->cb);
        free(q->name);
        free(q->tmp);
        free(q);

    } 
}

/** send a message */
int amq_send(amqd_t mqdes, const char * msg_ptr, size_t msg_len, 
        unsigned msg_prio) {
    struct impl_t * q = (struct impl_t *)mqdes;
    pthread_mutex_lock(&q->mutex); 
    while (q->cb.count == q->cb.capacity) {
        pthread_cond_wait(&q->cond, &q->mutex); 
    }
    q->tmp->size  = msg_len; 
    // copy into temp buffer 
    memcpy(q->tmp->data, msg_ptr, msg_len);
    cb_push_back(&q->cb, q->tmp); 
    pthread_cond_broadcast(&q->cond);
    pthread_mutex_unlock(&q->mutex); 

};

/** send a message */
int amq_timedsend(amqd_t mqdes, const char * msg_ptr, size_t msg_len, 
        unsigned msg_prio, const struct timespec * abs_time) {
    struct impl_t * q = (struct impl_t *)mqdes;
    pthread_mutex_lock(&q->mutex); 
    while (q->cb.count == q->cb.capacity) {
        if (ETIMEDOUT == pthread_cond_timedwait(&q->cond, 
                    &q->mutex, abs_time)) { 
            pthread_mutex_unlock(&q->mutex);
            errno = ETIMEDOUT; 
            return -1; 
        }
    }
    q->tmp->size  = msg_len; 
    // copy into temp buffer 
    memcpy(q->tmp->data, msg_ptr, msg_len);
    cb_push_back(&q->cb, q->tmp); 
    pthread_cond_broadcast(&q->cond);
    pthread_mutex_unlock(&q->mutex); 

};

/** receive a message */
ssize_t amq_receive(amqd_t mqdes, char*msg_ptr, size_t msg_len, 
        unsigned msg_prio) {
    struct impl_t * q = (struct impl_t *)mqdes;
    pthread_mutex_lock(&q->mutex); 
    // wait for next message
    while (q->cb.count == 0) {
        pthread_cond_wait(&q->cond, &q->mutex); 
    }
    cb_pop_front(&q->cb, q->tmp);
    assert(q->tmp->size <= msg_len);
    memcpy(msg_ptr, q->tmp->data, q->tmp->size); 

    // notify any pending senders that there is room
    if (q->cb.capacity-1 == q->cb.count)
        pthread_cond_broadcast(&q->cond);

    pthread_mutex_unlock(&q->mutex); 
    return q->tmp->size;
};

/** receive a message */
ssize_t amq_timedreceive(amqd_t mqdes, char*msg_ptr, size_t msg_len, 
        unsigned msg_prio, const struct timespec * abs_time) {
    struct impl_t * q = (struct impl_t *)mqdes;
    pthread_mutex_lock(&q->mutex); 
    // wait for next message
    while (q->cb.count == 0) {
        if (ETIMEDOUT == pthread_cond_timedwait(&q->cond, 
                    &q->mutex, abs_time) ){
            errno = ETIMEDOUT; 
            pthread_mutex_unlock(&q->mutex);
            return -1; 
        }
    }
    cb_pop_front(&q->cb, q->tmp);
    assert(q->tmp->size <= msg_len);
    memcpy(msg_ptr, q->tmp->data, q->tmp->size); 

    // notify any pending senders that there is room
    if (q->cb.capacity-1 == q->cb.count)
        pthread_cond_broadcast(&q->cond);

    pthread_mutex_unlock(&q->mutex); 
    return q->tmp->size;
};

amqd_t amq_unlink(const char * name) {
    amqd_t q = find_queue(name);
    if (q) {
        return amq_close(q);
    } else {
        errno =  ENOENT; 
        return -1; 
    }
}
