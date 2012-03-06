#define USE_POSIX_MQUEUE
#include "amq.h"
#include <iostream>
#include <cassert>
#include <pthread.h>
#include <cstdio>
#include <fcntl.h>
#include <cstdlib>
void open_close_test() {
    amq_attr a; 
    a.mq_flags = 0; 
    a.mq_maxmsg = 2; 
    a.mq_msgsize = sizeof(int); 
    amqd_t mq =  amq_open("/test", O_CREAT, 0644, &a);
    if (mq == -1) {
        perror("amq_open");
        abort();
    }
    // open second time to verify ref counting 
    amqd_t mq2 =  amq_open("/test", O_CREAT, 0644, &a);
    if (mq2 == -1) {
        perror("amq_open");
        amq_unlink("/test");
        abort();
    }

    // run  through valgrind to verify memory gets cleaned up. 
    amq_close(mq);
    amq_close(mq2);
}

void add_remove_test() {
    amq_attr a; 
    a.mq_flags = 0; 
    a.mq_maxmsg = 2; 
    a.mq_msgsize = sizeof(int); 
    amqd_t mq =  amq_open("/test", O_CREAT, 0644, &a);
    assert(mq != -1); 
    
    for (int i =0; i < 2; ++i) {
        std::cout << i << " << " << i << std::endl; 
        amq_send(mq, (const char*) &i, sizeof(int), 0); 
    }

    for (int i =0; i < 2; ++i) {
        int d; 
        ssize_t s = amq_receive(mq, (char*) &d, sizeof(int), 0); 
        std::cout << i << " " << s << " -> "  << d << std::endl; 
    }


    amq_close(mq);
}

bool shouldStop = false; 
void* reader(void* _mq) {
    amqd_t * mq = (amqd_t*) _mq; 

    while(!shouldStop) {
       int data;  
       ssize_t ret = amq_receive(*mq, (char*) &data, sizeof(int), 0);
       std::cout << ret << " >> " << data << std::endl; 
       sleep(1); 
       if (data == -1) {
           shouldStop = true;
           break; 
       }
    }
    return 0; 
}; 


void blockingTest() {
    amq_attr a; 
    a.mq_flags = 0; 
    a.mq_maxmsg = 2; 
    a.mq_msgsize = sizeof(int); 
    amqd_t mq =  amq_open("/test", O_CREAT, 0644, &a);
    assert(mq != -1); 
    std::cout << mq << std::endl; 

    pthread_t th; 
    pthread_create(&th, NULL, reader, &mq); 
    
    for (int i =0; i < 19; ++i) {
        std::cout << i << " << " << i << std::endl; 
        int ret = amq_send(mq, (const char*) &i, sizeof(int), 0); 
        sleep(1);
        if (ret == -1) {
            perror("amq_send");
            abort();
        }
    }

    shouldStop = true; 
    // send one more message to make sure thread stops
    int data = -1; 
    timespec  t; 
    t.tv_sec = time(0) + 2; 
    t.tv_nsec = 0; 
    amq_timedsend(mq, (const char*) &data, sizeof(int), 0, &t); 

    // thread should exit 
    pthread_join(th, 0);

}



int main(int argc, char **argv) {
    std::cout << "----- open/close test" << std::endl; 
    open_close_test();

    std::cout << "----- send/receive test" << std::endl; 
//    add_remove_test();

    std::cout << "----- blocking test" << std::endl; 
    blockingTest(); 
    
    return 0; 
}
