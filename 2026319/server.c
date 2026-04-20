//
// Created by gqx on 3/15/26.
//
#include "../include/server.h"
#include <sys/socket.h>
#include <netinet/in.h>     // sockaddr_in结构体定义
#include <arpa/inet.h>      // 网络字节序转换函数
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include<sys/epoll.h>
#include<errno.h>

//服务器运行状态标志　　１:服务器运行  0:　服务器停止
int server_running = 1;
#define MAX_EVENTS 64

int create_socket(int port){
    /**
     * AF_INET IPv4地址族
     * SOCK_STREAM->TCP
     * 0: 默认协议 TCP
     */
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if(fd < 0)  return -1;

    /**
     * 处理Address already in use问题　设置套接字选项 允许地址和端口立即重用
     * 函数原型： int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen);
     * 参数详解：
     * sockfd：要设置选项的套接字描述符。
     * level：选项定义的层次。指定选项是应用于套接字层（SOL_SOCKET）还是特定协议层（如IPPROTO_IP、IPPROTO_TCP等）。
     * optname：要设置的选项名称。不同的level对应不同的选项,与 level参数配合使用
     * optval：指向包含新选项值的缓冲区的指针。这个值可以是指向整数的指针、结构体指针等，具体取决于选项。
     * optlen：optval缓冲区的大小（以字节为单位）。
     */
    int reuse = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        perror("setsockopt(SO_REUSEADDR) failed");
        close(fd);
        return -1;
    }

    //配置服务器地址信息
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;  // IPV4地址族
    addr.sin_addr.s_addr = INADDR_ANY; //INADDR_ANY监听所有网络接口
    addr.sin_port = htons(port); //端口转换（主机字节序－－－－＞网络字节序）

    //将套接字和服务器地址信息绑定
    if(bind(fd, (struct sockaddr_in*)&addr, sizeof(addr)) < 0){
        perror("bind failed");
        close(fd);
        return -1;
    }

    //服务开始监听
    if(listen(fd, 1000) < 0){
        perror("listen failed");
        close(fd);
        return -1;
    }

    return fd;
}

/**
 * 设置文件描述符为非阻塞模式
 * @param server_fd　套接字文件描述符
 * 在服务器编程中，将监听套接字设置为非阻塞模式，
 * 通常是为了配合select、poll、epoll等多路复用I/O机制，实现单线程同时处理多个连接，提高程序效率。
 */
void set_nonblock(int server_fd){
    //获获取文件描述符 server_fd当前的文件状态标志
    /*
     * server_fd：要操作的文件描述符（通常是一个套接字，用于服务器监听）
     * F_GETFL：命令参数，表示"获取文件状态标志"
     * 0：对于F_GETFL命令，此参数被忽略，通常设为0
     * 返回值：返回当前文件描述符的所有状态标志位（如O_RDWR、O_NONBLOCK、O_APPEND等）的按位或组合
     */
    int flags = fcntl(server_fd, F_GETFL, 0);
    /*
     * 设置文件描述符为新的状态标志（添加非阻塞标志）
     * server_fd：要操作的文件描述符
     * F_SETFL：命令参数，表示"设置文件状态标志"
     * flags | O_NONBLOCK：新的标志值
     * flags：之前获取的原始标志
     * O_NONBLOCK：非阻塞标志常量
     * |：按位或操作，将O_NONBLOCK标志添加到原有标志中
     * 结果：为server_fd添加了非阻塞模式，不会清除原有的其他标志
     */
    fcntl(server_fd, F_SETFL, flags | O_NONBLOCK);
}

/**
 * 启动HTTP服务器
 * @param port　监听端口号
 * @param dir　　静态文件路径
 * @return　　１：成功　　０：失败
 */
int start_server(int port, char *dir){
    //就绪事件数组
    struct epoll_event events[MAX_EVENTS];

    struct sockaddr_in client_addr;  //执行accept函数时，输出的参数，用于返回客户端地址信息
    socklen_t client_len = sizeof(client_addr);

    //1 创建服务器套接字
    int server_fd = create_socket(port);
    if(server_fd < 0){
        perror("创建socket失败！");
        return 0;
    }

    //当有连接请求时，accept会立刻返回新客户端的socket
    //如果没有连接请求时，accept会立刻返回-1（通常设置 errno为 EAGAIN或 EWOULDBLOCK）
    set_nonblock(server_fd);

    //创建epoll实例
    //EPOLL_CLOEXEC 执行时关闭(父子进程／创建新进程时需要注意使用)
    int epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd == -1) {
        perror("epoll_create1 failed");
        close(server_fd);
        return 0;
    }

    struct epoll_event ev;
    ev.events = EPOLLIN;        // 监控可读事件，水平触发
    ev.data.fd = server_fd;     // 事件触发时返回的数据
    //epoll_ctl是 Linux 系统中 epoll 机制的核心控制函数，用于管理 epoll 实例监控的文件描述符。
    //将服务器套接字添加到epoll监控　
    //epoll_fd:  epoll 实例的文件描述符（由 epoll_create()返回）
    //EPOLL_CTL_ADD: 添加监控操作
    //server_fd: 要被监控的文件描述符
    //ev: 事件配置结构体指针
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev) == -1) {
        perror("epoll_ctl: ADD failed");
        close(server_fd);
        return 0;
    }

    while(server_running){
        //epoll_wait是 epoll 机制中等待和收集事件的核心函数，相当于 epoll 系统的"接收器"。
        /*
         * epfd：epoll 实例的文件描述符，由 epoll_create 或 epoll_create1 创建。
         * events：指向一个 struct epoll_event 数组的指针，用于存储就绪的事件。
         * maxevents：指定 events 数组的大小，必须大于0。
         * timeout：等待的超时时间（毫秒）。-1 表示无限等待，直到有事件发生。0 表示立即返回，即使没有事件发生。
         */
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);

        for(int i = 0; i < nfds; i++){

            //检查就绪事件类型
            if(events[i].data.fd == server_fd){
                //新客户端请求连接（新的客户端连接可能有多个）
                while(1){
                    //client_addr: 输出参数，用于返回客户端地址信息
                    //client_len :输入输出参数，传入地址结构大小，返回实际大小
                    int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
                    if(client_fd < 0){
                        if(errno == EAGAIN || errno == EWOULDBLOCK){
                            //已接受所有的新连接，退出当前处理新连接的循环．
                            break;
                        }
                        perror("接收新的连接出问题了！");
                        break;
                    }

                    //获取客户端的相关信息
                    char ip[16]; // ip地址
                    //inet_ntop函数用于将网络字节序的二进制IP地址转换为人类可读的字符串形式。
                    /*
                     * af：地址族，可以是 AF_INET（IPv4）或 AF_INET6（IPv6）。
                     * 二进制IP地址的指针（指向 struct in_addr或 struct in6_addr）。
                     * dst：指向存储结果字符串的缓冲区。
                     * size：缓冲区的大小。对于IPv4，至少需要16字节；对于IPv6，至少需要46字节。建议使用 INET_ADDRSTRLEN和 INET6_ADDRSTRLEN定义足够大的缓冲区。
                     */
                    inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof(ip));
                    int port = ntohs(client_addr.sin_port);
                    printf("新客户端已连接上: %s: %d\n", ip, port);

                    //将客户端套接字设置为无阻塞模式
                    set_nonblock(client_fd);

                    //epoll_ctl用于管理 epoll 实例监控的文件描述符，监听新连接客户端套接字
                    ev.events = EPOLLIN;        // 监控可读事件，水平触发
                    ev.data.fd = client_fd;     // 事件触发时返回的数据
                    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &ev) == -1) {
                        perror("epoll_ctl: ADD failed");
                        close(client_fd);
                        break;
                    }
                }
            }else{
                //处理之前已经建立连接的客户端

            }

        }

    }

    close(epoll_fd);
    close(server_fd);
    return 1;
}

