#include <stdio.h>
#include "../include/server.h"
#include <signal.h>

extern int server_running;

void handle_signal(int sig){
    server_running = 0;
}

int main(){

    int port = 8080;

    printf("启动HTTP服务器.....\n");
    printf("端口：%d\n", port);
    printf("按住Ctrl和Ｃ停止服务\n");

    //signal()函数用于设置信号处理程序，告诉操作系统当特定信号发生时应该执行什么操作。
    //SIGINT 信号编号  SIGINT---(键盘中断 (Ctrl+C))
    //信号处理函数指针
    signal(SIGINT, handle_signal);

    if(start_server(port) == 0){
        printf("服务启动失败！");
        return -1;
    }

    return 0;
}