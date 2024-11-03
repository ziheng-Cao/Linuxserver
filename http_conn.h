#ifndef HTTP_CONN
#define HTTP_CONN

#include <sys/epoll.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <iostream>
#include <string>
#include <regex>
#include <stdarg.h>
#include <errno.h>
#include "locker.h"
#include <sys/uio.h>


// 定义一个任务类http_conn
class http_conn {
public:
    // 所有事件都被注册到同一个epoll实例
    static int m_epfd;
    // 统计用户的数量
    static int m_usr_count;
    // 读缓冲区大小
    static const int RDBUF_SIZE = 2048;
    // 写缓冲区大小
    static const int WRBUF_SIZE = 2048;
    static const int FILENAME_LEN = 200;        // 文件名的最大长度

    // HTTP请求方法，这里只支持GET
    enum METHOD {GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT};
    
    /*
        解析客户端请求时，主状态机的状态
        PARSE_STATE_REQUESTLINE:当前正在分析请求行
        PARSE_STATE_HEADER:当前正在分析头部字段
        PARSE_STATE_CONTENT:当前正在解析请求体
    */
    enum PARSE_STATE { PARSE_STATE_REQUESTLINE = 0, PARSE_STATE_HEADER, PARSE_STATE_CONTENT };
    
    /*
        服务器处理HTTP请求的可能结果，报文解析的结果
        NO_REQUEST          :   请求不完整，需要继续读取客户数据
        GET_REQUEST         :   表示获得了一个完整的客户请求
        BAD_REQUEST         :   表示客户请求语法错误
        NO_RESOURCE         :   表示服务器没有资源
        FORBIDDEN_REQUEST   :   表示客户对资源没有足够的访问权限
        FILE_REQUEST        :   文件请求,获取文件成功
        INTERNAL_ERROR      :   表示服务器内部错误
        CLOSED_CONNECTION   :   表示客户端已经关闭连接了
    */
    enum HTTP_CODE { NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST, FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION };
    
    // 从状态机的三种可能状态，即行的读取状态，分别表示
    // 1.读取到一个完整的行 2.行出错 3.行数据尚且不完整
    enum LINE_STATUS { LINE_OK = 0, LINE_BAD, LINE_INCOMPLETE };

    // http_conn(/* args */);
    // ~http_conn();

    //处理客户端请求并响应
    void process();

    //初始化连接，这里用的是引用
    void init(int sockfd, const sockaddr_in & addr);

    // 在解析请求时，初始化相关变量
    void init();

    // 关闭连接
    void close_conn();

    // 非阻塞的读数据，一次性读完
    bool read();
    // 非阻塞的写数据，一次性写完
    bool write();

    
private:
    // 该请求http连接的socket文件描述符
    int m_sockfd;
    //通信的socket地址
    sockaddr_in m_addr;
    // 存储读到的数据
    char m_rdbuf[RDBUF_SIZE];
    // 在循环中多次读取并存入缓冲区
    //每次循环读到的存入缓冲区的最后一个字节位置，
    //以便下一次循环从该位置+1开始存储
    int m_rd_idx;

    // 解析请求中，当前分析的字符在读缓冲区的位置
    int m_parse_idx;
    // 在解析请求中，当前分析的行的首字符位置
    int m_parse_line;

    //请求的文件名
    char * m_url;
    //http版本1.1
    char * m_version;
    //请求方法
    METHOD m_method;
    //连接的主机名
    char * m_host;
    //连接是否keep-alive
    bool m_is_keeplive;
    // HTTP请求的消息总长度
    int m_content_length;

    //主状态机所处状态
    PARSE_STATE m_parse_state;

    //解析http请求
    HTTP_CODE parse_request();
    //解析请求首行
    HTTP_CODE parse_first_line(char * text);
    //解析请求头
    HTTP_CODE parse_header(char * text);
    //解析请求体
    HTTP_CODE parse_body(char * text);
    //获取一行的状态
    LINE_STATUS parse_line();
    //获取一行内容
    char * get_line(){return m_rdbuf + m_parse_line;}
    
    HTTP_CODE do_request();
    
    bool process_write( HTTP_CODE ret );    // 填充HTTP应答

    // 这一组函数被process_write调用以填充HTTP应答。
    void unmap();
    bool add_response( const char* format, ... );
    bool add_content( const char* content );
    bool add_content_type();
    bool add_status_line( int status, const char* title );
    bool add_headers( int content_length );
    bool add_content_length( int content_length );
    bool add_linger();
    bool add_blank_line();

    char m_real_file[ FILENAME_LEN ];       // 客户请求的目标文件的完整路径，其内容等于 doc_root + m_url, doc_root是网站根目录
    char m_write_buf[ WRBUF_SIZE ];  // 写缓冲区
    int m_write_idx;                        // 写缓冲区中待发送的字节数
    char* m_file_address;                   // 客户请求的目标文件被mmap到内存中的起始位置
    struct stat m_file_stat;                // 目标文件的状态。通过它我们可以判断文件是否存在、是否为目录、是否可读，并获取文件大小等信息
    struct iovec m_iv[2];                   // 我们将采用writev来执行写操作，所以定义下面两个成员，其中m_iv_count表示被写内存块的数量。
    int m_iv_count;
    
};

// http_conn::http_conn(/* args */) {

// }

// http_conn::~http_conn() {
    
// }

bool addfd(int epfd, int fd, bool one_shot);

bool rmfd(int epfd, int fd);

bool modfd(int epfd, int fd, int ev);

#endif