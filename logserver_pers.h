#ifndef _LOGSERVER_H_
#define _LOGSERVER_H_


#include <queue>
#include <vector>

//#include "logstore.h"

#include "datatuple.h"



#include <stasis/transactional.h>
#include <pthread.h>

#undef begin
#undef try
#undef end

class logtable;



struct pthread_item;

struct pthread_data {
    std::queue<pthread_item> *idleth_queue;
    std::queue<int> *ready_queue;
    std::queue<int> *work_queue;
    pthread_mutex_t * qlock;

    pthread_cond_t *selcond;
    
    pthread_cond_t * th_cond;
    pthread_mutex_t * th_mut;
    
    int *workitem; //id of the socket to work

    //pthread_mutex_t * table_lock;
    rwl *table_lock;
    logtable *ltable;
    bool *sys_alive;
};

struct pthread_item{
    pthread_t * th_handle;
    pthread_data *data;
};


//struct work_item
//{
//    int sockd; //socket id
//    datatuple in_tuple; //request
//    datatuple out_tuple; //response
//};

struct serverth_data
{
    int *server_socket;
    int server_port;
    std::queue<pthread_item> *idleth_queue;
    std::queue<int> *ready_queue;

    pthread_cond_t *selcond;
    
    pthread_mutex_t *qlock;
    
    

};

void * thread_work_fn( void *);    

class logserver
{
public:
    //server codes
    static uint8_t OP_SUCCESS;
    static uint8_t OP_FAIL;
    static uint8_t OP_SENDING_TUPLE;

    //client codes
    static uint8_t OP_FIND;
    static uint8_t OP_INSERT;

    static uint8_t OP_DONE;
    
    static uint8_t OP_INVALID;
    
public:
    logserver(int nthreads, int server_port){
        this->nthreads = nthreads;
        this->server_port = server_port;
        //lsmlock = new pthread_mutex_t;
        //pthread_mutex_init(lsmlock,0);

        lsmlock = initlock();

        qlock = new pthread_mutex_t;
        pthread_mutex_init(qlock,0);

        ltable = 0;

    }

    ~logserver()
        {
            //delete lsmlock;
            deletelock(lsmlock);
            delete qlock;
        }
    
    void startserver(logtable *ltable);

    void stopserver();
    
    
public:

private:

    //main loop of server
    //accept connections, assign jobs to threads
    //void dispatchLoop();

    void eventLoop();
    

private:

    int server_port;
    
    int nthreads;

    bool sys_alive;
    
    int serversocket; //server socket file descriptor

    //ccqueue<int> conn_queue; //list of active connections (socket list)

    //ccqueue<pthread_item> idleth_queue; //list of idle threads

    std::queue<int> ready_queue; //connections to go inside select
    std::queue<int> work_queue;  //connections to be processed by worker threads
    std::queue<pthread_item> idleth_queue;
    pthread_mutex_t *qlock;

    pthread_t server_thread;
    serverth_data *sdata;
    pthread_cond_t *selcond; //server loop cond
    
    std::vector<pthread_item *> th_list; // list of threads

    rwl *lsmlock; //lock for using lsm table

    logtable *ltable;
    
};


#endif