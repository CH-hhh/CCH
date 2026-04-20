//
// Created by gqx on 3/15/26.
//

#ifndef WEBSERVER_SERVER_H
#define WEBSERVER_SERVER_H

/**
 * 创建并配置服务器套接字
 * @param port 端口号
 * @return
 */
int create_socket(int port);

/**
 * 启动HTTP服务器
 * @param port　监听端口号
 * @param dir　　静态文件路径
 * @return　　１：成功　　０：失败
 */
int start_server(int port);

/**
 * 处理客户端请求
 * @param client_fd　客户端套接字
 */
void handle_client(int client_fd);

#endif //WEBSERVER_SERVER_H
