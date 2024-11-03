#include <stdio.h>
#include <string.h>
#include <stdlib.h> 
#include <iostream>
#include <unistd.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <errno.h>
#include "locker.h"
#include "threadpool.h"
#include <signal.h>
#include "http_conn.h"

#define MAX_FD 65535
#define MAX_EVENT_NUMBER 10000
//添加信号捕捉
void addsig(int signum, void (*handler)(int)){
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    sa.sa_flags = 0;
    sigfillset(&sa.sa_mask);
    sigaction(signum, &sa, NULL);
    printf("对%d进行信号捕捉\n", signum);
}

// epoll中添加文件描述符
extern bool addfd(int epfd, int fd, bool one_shot);
// 删除epoll中的文件描述符
extern bool rmfd(int epfd, int fd);
// 修改epoll中的文件描述符监测事件
extern bool modfd(int epfd, int fd, int ev);

int main(int argc, char* argv[]){
    
    if(argc < 2){
        printf("请输入参数，端口号");
        exit(-1);   
    }

    // 获取端口号，atoi将字符串转换为整形数值
    int port = atoi(argv[1]);

    // 对SIGPIPE信号进行捕捉处理
    addsig(SIGPIPE, SIG_IGN);

    //创建线程池，指定的任务类型为http_conn类
    Threadpool<http_conn> * threadpool = NULL;
    try {
        threadpool = new Threadpool<http_conn>;
    } catch(...) {
        exit(-1);
    } 

    //创建一个数组用于保存所有连接的客户端的信息
    http_conn * usrs = new http_conn[MAX_FD];
    printf("here");
    // 创建socket接口
    int listenfd = socket(PF_INET, SOCK_STREAM, 0);

    // 设置端口重用，必须在绑定之前设置
    int reuse = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    // 绑定socket
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
    bind(listenfd, (struct sockaddr *)&address, sizeof(address));
    printf("设置socket监听\n");
    //监听
    listen(listenfd, 5);

    //创建epoll对象，事件数组，添加监听描述符
    struct epoll_event events[MAX_EVENT_NUMBER];
    int epfd = epoll_create(1);
    
    // 添加监听文件描述符
    addfd(epfd, listenfd, false);

    http_conn::m_epfd = epfd;

    while (true)
    {
        int num = epoll_wait(epfd, events, MAX_EVENT_NUMBER, -1);
        if((num < 0) && (errno != EINTR)){
            printf("epoll fail\n");
            break;
        }

        //循环遍历epoll事件数组
        for (int i = 0; i < num; i++)
        {
            int sockfd = events[i].data.fd;
            if(sockfd == listenfd) {
                //有客户端请求连接
                struct sockaddr_in client_addr;
                socklen_t client_addr_len = sizeof(client_addr);
                int connfd = accept(listenfd, (struct sockaddr *)&client_addr, &client_addr_len);

                if(http_conn::m_usr_count >= MAX_FD){
                    //连接已满
                    // 给客户端回显信息

                    close(connfd);
                    continue;
                }
                // 将新的连接客户端数据初始化放到数组中
                // usrs[http_conn::m_usr_count].init(connfd, client_addr);   
                printf("有新客户端连接:%d\n", connfd);   
                usrs[connfd].init(connfd, client_addr);
            }else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)){
                //对方异常断开连接或出错
                //感觉用sockfd做数组下标有点问题
                usrs[sockfd].close_conn();
            }else if(events[i].events & EPOLLIN) {
                //一次性读取客户端发送的所有内容
                printf("我要开始读取啦\n");
                if(usrs[sockfd].read()){
                    printf("我读到啦\n");
                    threadpool->append_request(usrs + sockfd);
                }else {
                    printf("读取失败");
                    usrs[sockfd].close_conn();
                }
            }else if(events[i].events & EPOLLOUT){
                //一次性写完数据
                if(!usrs[sockfd].write()){
                    usrs[sockfd].close_conn();
                }
            }

        }
        

    }
    
    close(epfd);
    close(listenfd);
    delete [] usrs;
    delete threadpool;

    return 0;
}   