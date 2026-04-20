#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <signal.h>
#include <fcntl.h>
#include <time.h>
#include "file_io.h"
#include "db_helper.h"

// 记录当前阻塞执行命令进程组的 PID
pid_t global_running_pid = -1;

void interrupt_current_process() {
    if (global_running_pid > 0) {
        // 利用负号杀掉整个进程组，确保 ping 之类产生的子进程也都统统死掉
        kill(-global_running_pid, SIGKILL); 
    }
}

// 为了向下兼容，但实际上大部分原生处理已经全部废弃
void execute_shell_command(const char *cmd, char *output, size_t out_len) {
    snprintf(output, out_len, "Executing... %s", cmd);
}

// 现在仅仅拦截特定的极其有限的内置命令逻辑 (例如只有 cd 需要我们特殊处理状态)
int is_builtin_command(const char *cmd_name) {
    const char *builtins[] = {"cd"};
    int count = sizeof(builtins) / sizeof(builtins[0]);
    for (int i = 0; i < count; ++i) {
        if (strcmp(cmd_name, builtins[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

// 提取完整命令中的参数部分
static char* get_command_args(const char *cmd_line) {
    char *space = strchr(cmd_line, ' ');
    if (space) {
        while (*space == ' ') space++;
        return space; 
    }
    return NULL;
}

// 废除了之前的 handle_file_command (因为交给了底层的 sh)
void handle_file_command(const char *current_path, const char *cmd_line, char *output, size_t out_len) {
    (void)current_path; (void)cmd_line;
    output[0] = '\0';
}

// 现在只保留最为核心和特殊的 cd 命令的拦截与状态修正
void handle_dir_command(const char *current_path, const char *cmd_line, char *output, size_t out_len) {
    char cmd_copy[1024];
    strncpy(cmd_copy, cmd_line, sizeof(cmd_copy) - 1);
    cmd_copy[sizeof(cmd_copy) - 1] = '\0';
    
    char *cmd_name = strtok(cmd_copy, " ");
    char *arg = strtok(NULL, " "); 
    if (arg) arg[strcspn(arg, "\r\n")] = 0;

    output[0] = '\0';

    if (strcmp(cmd_name, "cd") == 0) {
        if (!arg) arg = "/"; // 默认回到根或者什么都不做
        
        char popen_cmd[2048];
        snprintf(popen_cmd, sizeof(popen_cmd), "cd %s && cd %s && pwd", current_path, arg);
        
        FILE *fp = popen(popen_cmd, "r");
        if (fp) {
            char new_pwd[1024] = {0};
            if (fgets(new_pwd, sizeof(new_pwd), fp) != NULL) {
                new_pwd[strcspn(new_pwd, "\r\n")] = 0;
                snprintf(output, out_len, "NEW_DIR_STATE:%s\n", new_pwd);
            } else {
                snprintf(output, out_len, "cd: %s: No such file or directory\n", arg);
            }
            pclose(fp);
        } else {
             snprintf(output, out_len, "cd Failed executing shell check.\n");
        }
    }
}

void execute_long_process_command(int client_sock, const char *current_path, const char *cmd_line, size_t max_buff, const char *username) {
    (void)max_buff;
    if (!cmd_line || strlen(cmd_line) == 0) return;
    
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        char *err_msg = "Server Error: pipe failed.\n";
        write(client_sock, err_msg, strlen(err_msg));
        return;
    }

    pid_t pid = fork();
    if (pid == -1) {
        char *err_msg = "Server Error: fork failed.\n";
        write(client_sock, err_msg, strlen(err_msg));
        close(pipefd[0]);
        close(pipefd[1]);
        return;
    }

    if (pid == 0) {
        // --- 子进程 ---
        setpgid(0, 0); 
        
        close(pipefd[0]); // 关闭读端
        dup2(pipefd[1], STDOUT_FILENO); // 重定向标准输出
        dup2(pipefd[1], STDERR_FILENO); // 重定向标准错误
        close(pipefd[1]);
        
        // 针对 top 命令的极其特殊的兼容性处理：
        // 纯终端下的 top 会发送清屏等 ANSI 字符并且难以被普通管道捕捉长流，
        // 强制追加批处理模式（-b）让它以纯文本数据流行式不断输出！
        char final_cmd[4096];
        if (strncmp(cmd_line, "top", 3) == 0 && (cmd_line[3] == ' ' || cmd_line[3] == '\0')) {
            snprintf(final_cmd, sizeof(final_cmd), "%s -b", cmd_line);
        } else {
            snprintf(final_cmd, sizeof(final_cmd), "%s", cmd_line);
        }

        // 难点解决：用 stdbuf -oL -eL 强行关闭那些采用行缓冲甚至块缓冲（如 grep）的 C 程序的缓冲区
        // 将标准输出流变为行缓冲模式（每产生一行就 flush），这样它在管道中产生的数据就不会被憋住
        char full_cmd[8192];
        snprintf(full_cmd, sizeof(full_cmd), "cd %s && stdbuf -oL -eL %s 2>&1", current_path, final_cmd);

        // 利用系统 /bin/sh -c 的超强解析能力，直接处理带引号/空格/管道等情况
        execl("/bin/sh", "sh", "-c", full_cmd, (char *)NULL);
        
        printf("Server Error: exec failed.\n");
        exit(1);
    } else {
        // --- 父进程 ---
        close(pipefd[1]); 
        
        // 记录全局 PID，允许其他线程（如 /interrupt 路由）来杀死它
        global_running_pid = pid;
        
        // 对于原生流式请求，我们需要把普通指令原本在后方响应的 HTTP 头，搬到这个源头去立即发送
        // 1.0 的 Chunked 或关闭方式最简单
        char *http_header = "HTTP/1.0 200 OK\r\n"
                            "Content-Type: text/plain\r\n"
                            // 不带 Content-Length, 利用 socket 断开作为结束标志
                            "\r\n";
        write(client_sock, http_header, strlen(http_header));
        
        char buffer[1024];
        ssize_t n;
        
        // 分配缓冲区以捕获完整输出存入数据库
        size_t full_output_size = 65536; // 64KB
        char *full_output = (char *)malloc(full_output_size);
        if (full_output) full_output[0] = '\0';
        size_t total_len = 0;
        
        signal(SIGPIPE, SIG_IGN); // 防止客户端突然断开(如 curl 超时退出)导致 server 收到 SIGPIPE 异常崩溃
        
        // 我们利用标准的 read 每次不管读出几个字节，都立刻通过 socket 发射出去
        while ((n = read(pipefd[0], buffer, sizeof(buffer) - 1)) > 0) {
            write(client_sock, buffer, n);
            
            // 同时保存到 full_output 缓冲区，防溢出处理
            if (full_output && total_len + n < full_output_size - 1) {
                buffer[n] = '\0';
                strncat(full_output, buffer, full_output_size - total_len - 1);
                total_len += n;
            }
        }
        
        close(pipefd[0]);
        // 收尸拦截
        waitpid(pid, NULL, 0); 
        
        global_running_pid = -1; // 恢复初始状态
        
        if (full_output) {
            log_command(username, current_path, cmd_line, full_output);
            free(full_output);
        }
    }
}