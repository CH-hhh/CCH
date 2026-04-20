#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <time.h>

#define MAX_EVENTS 10
#define PORT 8888

int main() {
    int listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    
    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    bind(listen_sock, (struct sockaddr*)&server_addr, sizeof(server_addr));
    listen(listen_sock, SOMAXCONN);

    int epoll_fd = epoll_create1(0);
    struct epoll_event event;
    struct epoll_event events[MAX_EVENTS];

    event.events = EPOLLIN; 
    event.data.fd = listen_sock;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_sock, &event);

    while (1) {
        int event_count = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        
        for (int i = 0; i < event_count; i++) {
            if (events[i].data.fd == listen_sock) {
                struct sockaddr_in client_addr;
                socklen_t client_len = sizeof(client_addr);
                int client_sock = accept(listen_sock, (struct sockaddr*)&client_addr, &client_len);
                
                if (client_sock >= 0) {
                    time_t ticks = time(NULL);
                    char time_str[64];
                    snprintf(time_str, sizeof(time_str), "%.24s\n", ctime(&ticks));
                    
                    write(client_sock, time_str, strlen(time_str));
                    close(client_sock);
                }
            }
        }
    }
    
    close(listen_sock);
    return 0;
}