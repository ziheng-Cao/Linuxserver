#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <pthread.h>
#include <list>
#include <exception>
#include "locker.h"

//线程池类
//模板的类型是http请求
template<typename T>
class Threadpool {
public:
    Threadpool(int thread_num = 8, int max_requests = 1000);
    ~Threadpool();
    bool append_request(T* request);
private:
    // 线程数量
    int m_thread_number;

    // 线程池数组
    pthread_t * m_threads;

    // 请求队列中最多允许等待的处理的请求数量
    int m_max_requests;

    //请求队列
    std::list<T*> m_workqueue;
    
    //互斥锁
    Locker m_queue_mutex;

    //信号量，用于判断是否有任务需要处理
    Sem m_queue_sem;
    
    // 是否结束进程
    bool m_stop;
    // 线程处理函数
    static void * worker(void * );

    void run();
};

// 函数定义
template<typename T>
Threadpool<T>::Threadpool(int thread_num, int max_requests) :
    m_thread_number(thread_num), m_max_requests(max_requests),
    m_stop(false), m_threads(NULL) {
    if((thread_num <= 0) || (max_requests <= 0)){
        throw std::exception();
    }

    //创建线程池数组 
    m_threads = new pthread_t[m_thread_number];
    if(!m_threads){
        throw std::exception();
    }

    // 创建线程，将他们放入线程池，并且设置线程分离
    for(int i = 0; i < m_thread_number; i++) {
        // c++中的线程处理方法必须是静态方法，然而静态方法
        // 又不能调用成员变量，因此使用this来传入对象的引用
        if(pthread_create(m_threads + i, NULL, worker, this)) {
            delete[] m_threads;
            throw std::exception();
        }
        printf("线程%d：%ld已创建\n", i, m_threads[i]);
        if(pthread_detach(m_threads[i])){
            delete[] m_threads;
            throw std::exception();
        }
    }
}

template<typename T>
Threadpool<T>::~Threadpool(){
    delete[] m_threads;
    m_stop = true;
}

template<typename T>
bool Threadpool<T>::append_request(T* request) {
    m_queue_mutex.lock();
    if (m_workqueue.size() > m_max_requests) {
        m_queue_mutex.unlock();
        return false;
    }
    
    m_workqueue.push_back(request);
    m_queue_mutex.unlock();
    m_queue_sem.post();
    return true;
} 
    
template<typename T>
void * Threadpool<T>::worker(void * arg) {
    Threadpool * pool = (Threadpool *) arg;
    pool->run();
    return pool;
}

template<typename T>
void Threadpool<T>::run() {
    //线程池运行函数
    while(!m_stop) {
        m_queue_sem.wait();
        m_queue_mutex.lock();
        if (m_workqueue.empty())
        {
            m_queue_mutex.unlock();
            continue;
        }
        
        T* request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queue_mutex.unlock();

        // 看是否获取到request
        if (!request){
            continue;
        }

        //获取到request后，执行request中的任务
        request->process();
    }
}
#endif