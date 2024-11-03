#ifndef LOCKER_H
#define LOCKER_H
//线程同步机制封装类

#include <stdio.h>
#include <pthread.h>
#include <exception>
#include <iostream>
#include <semaphore.h>

// 定义一个互斥锁类
class Locker {
public:
    // 构造方法
    Locker() {
        // 初始化互斥锁
        if (pthread_mutex_init(&m_mutex, NULL))
        {
            throw std::exception();
        } 
        // std::cout << " 互斥锁创建" << std::endl; 
    }
    
    //析构方法
    ~Locker() {
        pthread_mutex_destroy(&m_mutex);
        // std::cout << "互斥锁销毁" << std::endl;
    }

    // 上锁
    bool lock(){
        if (pthread_mutex_lock(&m_mutex))
        {
            throw std::exception();
        }
        // std::cout << "成功上锁" << std::endl;
        return 1;
    }

    // 解锁
    bool unlock() {
        if (pthread_mutex_unlock(&m_mutex))
        {
            throw std::exception();
        }
        // std::cout << "成功解锁" << std::endl;
        return 1;
    }

    pthread_mutex_t * get(){
        return &m_mutex;
    }

private:
    // 定义一个互斥锁变量
    pthread_mutex_t m_mutex;
};

//条件变量类
class Cond {
public:
    // 构造方法
    Cond() {
        // 初始化条件变量
        if (pthread_cond_init(&m_cond, NULL))
        {
            throw std::exception();
        } 
        // std::cout << " 互斥锁创建" << std::endl; 
    }
    
    //析构方法
    ~Cond() {
        pthread_cond_destroy(&m_cond);
        // std::cout << "互斥锁销毁" << std::endl;
    }

    // 等待
    bool wait(pthread_mutex_t* mutex){
        if (pthread_cond_wait(&m_cond, mutex))
        {
            throw std::exception();
        }
        // std::cout << "条件等待" << std::endl;
        return 1;
    }

    // 等待固定时间
    bool timedwait(pthread_mutex_t* mutex, timespec *abstime){
        return pthread_cond_timedwait(&m_cond, mutex, abstime) == 0;
    }

    bool signal(){
        return pthread_cond_signal(&m_cond) == 0;
    }

    bool broadcast(){
        return pthread_cond_broadcast(&m_cond) == 0;
    }

private:
    // 定义一个条件变量
    pthread_cond_t m_cond;
};

//信号量类
class Sem {
public:
    Sem(int value = 0){
        sem_init(&m_sem, 0, value);
    }

    ~Sem(){
        sem_destroy(&m_sem);
    }

    bool wait() {
        return sem_wait(&m_sem) == 0;
    }

    bool timedwait(timespec *abstime) {
        return sem_timedwait(&m_sem, abstime) == 0;
    }

    bool post() {
        return sem_post(&m_sem) == 0;
    }

    int getvalue() {
        int sval;
        sem_getvalue(&m_sem, &sval);
        return sval;
    }
private:
    sem_t m_sem;
};
#endif
