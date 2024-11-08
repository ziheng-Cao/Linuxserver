#include "http_conn.h"

int http_conn::m_epfd = -1;
int http_conn::m_usr_count = 0;

// 定义HTTP响应的一些状态信息
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";

// 网站的根目录
const char* doc_root = "/home/hndr01d/study/c/resource";

//设置文件描述符非阻塞
void setnonblock(int fd){
    int old_flag = fcntl(fd, F_GETFL);
    int new_flag = old_flag | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_flag);
}
//向epoll中添加文件描述符
bool addfd(int epfd, int fd, bool one_shot){
    struct epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLRDHUP;
    // event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;

    // epoll中的ONESHOT事件（只触发一次），防止一个线程在操作fd时，
    // 有数据进来，再次触发epoll，导致另一个线程也来操作该fd
    // 注意操作完毕后，需要重新加上ONESHOT
    if(one_shot){
        event.events | EPOLLONESHOT;
    }
    if(epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &event)){
        return false;
    }

    //设置文件描述符非阻塞
    setnonblock(fd);
    printf("向epoll中添加文件描述符\n");
    return true;
}

// 删除文件描述符
bool rmfd(int epfd, int fd){
    if(epoll_ctl(epfd, EPOLL_CTL_DEL, fd, 0)){
        return false;
    }
    close(fd);
    return true;
}

//修改epoll中的文件描述符监测事件
bool modfd(int epfd, int fd, int ev){
    struct epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
    if(epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &event)){
        return false;
    }
    return true;
}

// 初始化连接
void http_conn::init(int sockfd, const sockaddr_in & addr){
    m_sockfd = sockfd;
    m_addr = addr;
    // 获取客户端信息
    char ip_str[INET_ADDRSTRLEN]; // IPv4 地址字符串的最大长度
    int port;

    inet_ntop(AF_INET, &m_addr.sin_addr, ip_str, sizeof(ip_str));
    port = ntohs(m_addr.sin_port); // 将端口从网络字节序转换为主机字节序

    printf("客户端 IP: %s, 端口: %d\n", ip_str, port);

    //设置端口重用
    int reuse = 1;
    setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    // 在epoll实例中添加sockfd
    addfd(m_epfd, sockfd, true);
    m_usr_count++;

    init();
}

//关闭连接
void http_conn::close_conn() {
    if(m_sockfd != -1){
        //从epoll中将该连接删除，
        rmfd(m_epfd, m_sockfd);
        m_sockfd = -1;
        m_usr_count--;

    }
}

// 非阻塞的读数据，一次性读完
bool http_conn::read(){
    // printf("read");
    // 循环读取数据，直到读完
    if(m_rd_idx >= RDBUF_SIZE){
        printf("缓冲区满了");
        return false;
    }
    // 已经读取到的字节
    int rd_bytes = 0;
    while(true){
        // printf("reading");
        rd_bytes = recv(m_sockfd, m_rdbuf + m_rd_idx, RDBUF_SIZE - m_rd_idx, 0);
        if(rd_bytes == -1){
            if(errno == EAGAIN || errno == EWOULDBLOCK){
                // 没有数据
                printf("没数据了");
                break;
            }
            return false;
        } else if(rd_bytes == 0){
            // 对方关闭连接（没有写端）
            printf("对方已关闭连接");
            return false;
        }
        printf("正在读取");
        m_rd_idx += rd_bytes;
    }
    printf("读取到的数据：\n%s\n", m_rdbuf);
    return true;
}

// 解析请求时，初始化相关变量
void http_conn::init(){
    m_parse_idx = 0;
    m_parse_line = 0;
    // 首先解析请求首行
    m_parse_state = PARSE_STATE_REQUESTLINE;
    m_rd_idx = 0;
    m_method = GET;
    m_url = 0;
    m_version = 0;
    bzero(m_rdbuf, RDBUF_SIZE); 
    m_host = 0;
    m_is_keeplive = false;
    m_content_length = 0;
}

//主状态机，解析http请求
http_conn::HTTP_CODE http_conn::parse_request(){
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char * text = 0;

    // while (((line_status = parse_line()) == LINE_OK) || (m_parse_state == PARSE_STATE_CONTENT && line_status  == LINE_OK)){
    while (1){
        line_status = parse_line();
        if((m_parse_state == PARSE_STATE_CONTENT && line_status  == LINE_OK) || (line_status == LINE_OK)){
            // 正在解析请求体且数据完整，或解析到了一行完整的数据，感觉不用这样，直接判断数据完整行吗？
            text = get_line();
            m_parse_line = m_parse_idx;
            printf("获取到的一行内容：%s\n", text);

            switch (m_parse_state){
                case PARSE_STATE_REQUESTLINE:{
                    ret = parse_first_line(text);
                    if(ret == BAD_REQUEST){
                        printf("##########\n1\n#########\n");
                        return BAD_REQUEST;
                    }
                    break;
                }
                case PARSE_STATE_HEADER:{
                    ret = parse_header(text);
                    if(ret == BAD_REQUEST){
                        printf("##########\n2\n#########\n");
                        return BAD_REQUEST;
                    }else if(ret == GET_REQUEST){
                        return do_request();
                    }
                    break;
                }
                case PARSE_STATE_CONTENT:{
                    ret = parse_body(text);
                    if(ret == GET_REQUEST){
                        return do_request();
                    }
                    line_status = LINE_INCOMPLETE;
                    break;
                }
                default :{
                    return INTERNAL_ERROR;
                }
            }
        }else{
            break;
        }

    }
    return NO_REQUEST;
}

//解析请求首行->请求方法、目标URL和HTTP版本
http_conn::HTTP_CODE http_conn::parse_first_line(char * text){
    // GET /index.html HTTP/1.1
    printf("version:%s\n", text);
    m_url = strpbrk(text, " \t"); // 判断第二个参数中的字符哪个在text中最先出现
    if (! m_url) { 
        printf("##########\n3\n#########\n");
        return BAD_REQUEST;
    }
    // GET\0/index.html HTTP/1.1
    *m_url++ = '\0';    // 置位空字符，字符串结束符
    char* method = text;
    if ( strcasecmp(method, "GET") == 0 ) { // 忽略大小写比较
        m_method = GET;
    } else {
        printf("##########\n4\n#########\n");
        return BAD_REQUEST;
    }
    // /index.html HTTP/1.1
    // 检索字符串 str1 中第一个不在字符串 str2 中出现的字符下标。
    m_version = strpbrk( m_url, " \t" );
    if (!m_version) {
        printf("##########\n5\n#########\n");
        return BAD_REQUEST;
    }
    // /index.html\0HTTP/1.1
    *m_version++ = '\0';
    if (strcasecmp(m_version, "HTTP/1.1") != 0 && strcasecmp(m_version, "HTTP/1.0") != 0) {
        printf("##########\n6\n#########\n");
        printf("version:%s\n", m_version);
        return BAD_REQUEST;
    }

    /**
     * http://192.168.110.129:10000/index.html
    */
    if (strncasecmp(m_url, "http://", 7) == 0 ) {   
        m_url += 7;
        // 在参数 str 所指向的字符串中搜索第一次出现字符 c（一个无符号字符）的位置。
        m_url = strchr( m_url, '/' );
    }
    if ( !m_url || m_url[0] != '/' ) {
        printf("##########\n7\n#########\n");
        return BAD_REQUEST;
    }
    m_parse_state = PARSE_STATE_HEADER; // 检查状态变成检查头
    return NO_REQUEST;
}

//解析请求头
http_conn::HTTP_CODE http_conn::parse_header(char * text){
    // 遇到空行，表示头部字段解析完毕
    if( text[0] == '\0' ) {
        // 如果HTTP请求有消息体，则还需要读取m_content_length字节的消息体，
        // 状态机转移到CHECK_STATE_CONTENT状态
        if ( m_content_length != 0 ) {
            m_parse_state = PARSE_STATE_CONTENT;
            return NO_REQUEST;
        }
        // 否则说明我们已经得到了一个完整的HTTP请求
        return GET_REQUEST;
    } else if ( strncasecmp( text, "Connection:", 11 ) == 0 ) {
        // 处理Connection 头部字段  Connection: keep-alive
        text += 11;
        text += strspn( text, " \t" );
        if ( strcasecmp( text, "keep-alive" ) == 0 ) {
            m_is_keeplive = true;
        }
    } else if ( strncasecmp( text, "Content-Length:", 15 ) == 0 ) {
        // 处理Content-Length头部字段
        text += 15;
        text += strspn( text, " \t" );
        m_content_length = atol(text);
    } else if ( strncasecmp( text, "Host:", 5 ) == 0 ) {
        // 处理Host头部字段
        text += 5;
        text += strspn( text, " \t" );
        m_host = text;
    } else {
        printf( "oop! unknow header %s\n", text );
    }
    return NO_REQUEST;
}

//解析请求体
http_conn::HTTP_CODE http_conn::parse_body(char * text){
    if ( m_rd_idx >= ( m_content_length + m_parse_idx ) )
    {
        text[ m_content_length ] = '\0';
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

// 解析一行，判断一行的依据：http请求包中的"\r\n"
http_conn::LINE_STATUS http_conn::parse_line(){
    char tmp;
    //m_rd_idx此时表示缓冲区中的最后一个数据所在位置
    for( ; m_parse_idx < m_rd_idx; ++m_parse_idx){
        tmp = m_rdbuf[m_parse_idx];
        if(tmp == '\r'){
            if((m_parse_idx + 1) == m_rd_idx){
                //最后一个字符是\r，后面没有字符了
                return LINE_INCOMPLETE;
            } else if(m_rdbuf[m_parse_idx + 1] == '\n'){
                m_rdbuf[m_parse_idx++] = '\0';
                m_rdbuf[m_parse_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }else if(tmp == '\n'){
            if((m_parse_idx > 1) && (m_rdbuf[m_parse_idx - 1] == '\r')){
                m_rdbuf[m_parse_idx-1] = '\0';
                m_rdbuf[m_parse_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_INCOMPLETE;
}

// 当得到一个完整、正确的HTTP请求时，我们就分析目标文件的属性，
// 如果目标文件存在、对所有用户可读，且不是目录，则使用mmap将其
// 映射到内存地址m_file_address处，并告诉调用者获取文件成功
http_conn::HTTP_CODE http_conn::do_request()
{
    // "/home/nowcoder/webserver/resources"
    strcpy( m_real_file, doc_root );
    int len = strlen( doc_root );
    strncpy( m_real_file + len, m_url, FILENAME_LEN - len - 1 );
    // 获取m_real_file文件的相关的状态信息，-1失败，0成功
    if ( stat( m_real_file, &m_file_stat ) < 0 ) {
        return NO_RESOURCE;
    }

    // 判断访问权限
    if ( ! ( m_file_stat.st_mode & S_IROTH ) ) {
        return FORBIDDEN_REQUEST;
    }

    // 判断是否是目录
    if ( S_ISDIR( m_file_stat.st_mode ) ) {
        return BAD_REQUEST;
    }

    // 以只读方式打开文件
    int fd = open( m_real_file, O_RDONLY );
    // 创建内存映射
    m_file_address = ( char* )mmap( 0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0 );
    close( fd );
    return FILE_REQUEST;
}

// 对内存映射区执行munmap操作
void http_conn::unmap() {
    if( m_file_address )
    {
        munmap( m_file_address, m_file_stat.st_size );
        m_file_address = 0;
    }
}

// 写HTTP响应
bool http_conn::write()
{
    int temp = 0;
    int bytes_have_send = 0;    // 已经发送的字节
    int bytes_to_send = m_write_idx;// 将要发送的字节 （m_write_idx）写缓冲区中待发送的字节数
    
    if ( bytes_to_send == 0 ) {
        // 将要发送的字节为0，这一次响应结束。
        modfd( m_epfd, m_sockfd, EPOLLIN ); 
        init();
        return true;
    }

    while(1) {
        // 分散写
        temp = writev(m_sockfd, m_iv, m_iv_count);
        if ( temp <= -1 ) {
            // 如果TCP写缓冲没有空间，则等待下一轮EPOLLOUT事件，虽然在此期间，
            // 服务器无法立即接收到同一客户的下一个请求，但可以保证连接的完整性。
            if( errno == EAGAIN ) {
                modfd( m_epfd, m_sockfd, EPOLLOUT );
                return true;
            }
            unmap();
            return false;
        }
        bytes_to_send -= temp;
        bytes_have_send += temp;
        if ( bytes_to_send <= bytes_have_send ) {
            // 发送HTTP响应成功，根据HTTP请求中的Connection字段决定是否立即关闭连接
            unmap();
            if(m_is_keeplive) {
                init();
                modfd( m_epfd, m_sockfd, EPOLLIN );
                return true;
            } else {
                modfd( m_epfd, m_sockfd, EPOLLIN );
                return false;
            } 
        }
    }
}

// 往写缓冲中写入待发送的数据
bool http_conn::add_response( const char* format, ... ) {
    if( m_write_idx >= WRBUF_SIZE ) {
        return false;
    }
    va_list arg_list;
    va_start( arg_list, format );
    int len = vsnprintf( m_write_buf + m_write_idx, WRBUF_SIZE - 1 - m_write_idx, format, arg_list );
    if( len >= ( WRBUF_SIZE - 1 - m_write_idx ) ) {
        return false;
    }
    m_write_idx += len;
    va_end( arg_list );
    return true;
}

bool http_conn::add_status_line( int status, const char* title ) {
    return add_response( "%s %d %s\r\n", "HTTP/1.1", status, title );
}

bool http_conn::add_headers(int content_len) {
    add_content_length(content_len);
    add_content_type();
    add_linger();
    add_blank_line();
    return true;
}

bool http_conn::add_content_length(int content_len) {
    return add_response( "Content-Length: %d\r\n", content_len );
}

bool http_conn::add_linger()
{
    return add_response( "Connection: %s\r\n", ( m_is_keeplive == true ) ? "keep-alive" : "close" );
}

bool http_conn::add_blank_line()
{
    return add_response( "%s", "\r\n" );
}

bool http_conn::add_content( const char* content )
{
    return add_response( "%s", content );
}

bool http_conn::add_content_type() {
    return add_response("Content-Type:%s\r\n", "text/html");
}

// 根据服务器处理HTTP请求的结果，决定返回给客户端的内容
bool http_conn::process_write(HTTP_CODE ret) {
    switch (ret)
    {
        case INTERNAL_ERROR:
            add_status_line( 500, error_500_title );
            add_headers( strlen( error_500_form ) );
            if ( ! add_content( error_500_form ) ) {
                return false;
            }
            break;
        case BAD_REQUEST:
            add_status_line( 400, error_400_title );
            add_headers( strlen( error_400_form ) );
            if ( ! add_content( error_400_form ) ) {
                return false;
            }
            break;
        case NO_RESOURCE:
            add_status_line( 404, error_404_title );
            add_headers( strlen( error_404_form ) );
            if ( ! add_content( error_404_form ) ) {
                return false;
            }
            break;
        case FORBIDDEN_REQUEST:
            add_status_line( 403, error_403_title );
            add_headers(strlen( error_403_form));
            if ( ! add_content( error_403_form ) ) {
                return false;
            }
            break;
        case FILE_REQUEST:
            add_status_line(200, ok_200_title );
            add_headers(m_file_stat.st_size);
            m_iv[ 0 ].iov_base = m_write_buf;
            m_iv[ 0 ].iov_len = m_write_idx;
            m_iv[ 1 ].iov_base = m_file_address;
            m_iv[ 1 ].iov_len = m_file_stat.st_size;
            m_iv_count = 2;
            return true;
        default:
            return false;
    }

    m_iv[ 0 ].iov_base = m_write_buf;
    m_iv[ 0 ].iov_len = m_write_idx;
    m_iv_count = 1;
    return true;
}

// 由线程池中的工作线程调用，这是处理HTTP的入口函数
// 业务逻辑代码
void http_conn::process(){
    //解析客户端发送的http请求
    // printf("已解析，创建响应\n");
    HTTP_CODE rd_ret = parse_request();
    if(rd_ret == NO_REQUEST){
        modfd(m_epfd, m_sockfd, EPOLLIN);
        return ;
    }

    //生成响应
    bool write_ret = process_write( rd_ret );
    if ( !write_ret ) {
        close_conn();
    }
    modfd( m_epfd, m_sockfd, EPOLLOUT);
}

