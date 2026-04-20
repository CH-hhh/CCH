#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "db_helper.h"
#include "file_io.h"
#include "cmd_validator.h"
#include "sys_monitor.h"

#define PORT 8080
#define BUFFER_SIZE 1048576 // 增加至 1MB, 解除非正常小规模拦截限制

// 读取静态 HTML 文件返回给浏览器
void serve_html(int client_sock) {
    FILE *html_file = fopen("web/index.html", "r");
    char *response_header = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n";
    write(client_sock, response_header, strlen(response_header));
    
    if (html_file) {
        char buffer[1024];
        size_t bytes_read;
        while ((bytes_read = fread(buffer, 1, sizeof(buffer), html_file)) > 0) {
            write(client_sock, buffer, bytes_read);
        }
        fclose(html_file);
    } else {
        char *error_msg = "<h1>404 Not Found (index.html is missing)</h1>";
        write(client_sock, error_msg, strlen(error_msg));
    }
}

/**
 * -----------------------------------------------------------------------------
 * 【HTTP 请求处理心跳区 + JSON 协议手撕解包】
 * 每个开启的独立线程都在执行这个函数。
 * 它的职责极其重要：
 * 1. 它要从长长的 TCP 通道拿取浏览器的 HTTP GET 或者 POST 请求包。
 * 2. 如果包里藏有 "POST /command"，代表浏览器送来了想要执行的一句 bash 命令，
 *    包体则是 JSON 格式，比如 {"path":"/usr", "cmd":"ls"}.
 * 3. 作为原生 C 代码，我们不加第三方庞大的库，纯手工用指针进行字符串扫描（strchr），
 *    遇到 \ 紧跟着 " 或 n，我们能灵巧避开，将其合并为 "真实" 的语义命令送给底层
 * 4. 最后把这些命令分别交给: cd拦截器、vim拦截器、保存拦截器或者统统摔给系统底层的 bash
 * -----------------------------------------------------------------------------
 */
void *handle_client(void *arg) {
    int client_sock = *((int *)arg);
    free(arg); // 释放主线程分配的内存
    
    char *buffer = malloc(BUFFER_SIZE);
    if (!buffer) { close(client_sock); return NULL; }
    
    int bytes_read = read(client_sock, buffer, BUFFER_SIZE - 1);
    if (bytes_read > 0) {
        buffer[bytes_read] = '\0';
        
        // 极简路由判定：
        // 如果是 GET 请求根路径，则返回前端页面
        if (strncmp(buffer, "GET / ", 6) == 0) {
            serve_html(client_sock);
        } 
        // 提供硬件监控的 JSON 信息路由
        else if (strncmp(buffer, "GET /system_stat", 16) == 0) {
            char stat_json[2048];
            get_system_monitor_json(stat_json, sizeof(stat_json));
            char response[4096];
            snprintf(response, sizeof(response),
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: application/json\r\n"
                "Connection: close\r\n\r\n"
                "%s", stat_json);
            write(client_sock, response, strlen(response));
        }
        // 【新增】：中断当前正在执行进程的路由
        else if (strncmp(buffer, "POST /interrupt", 15) == 0) {
            interrupt_current_process();
            
            char *response = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 12\r\n\r\nInterrupted!";
            write(client_sock, response, strlen(response));
        }
        // 处理前端 AJAX/Fetch 发送的命令POST请求
        else if (strncmp(buffer, "POST /command", 13) == 0) {
            // 定位请求体
            char *body = strstr(buffer, "\r\n\r\n");
            if (body) {
                body += 4; // 跳过空白行
                
                // --- 这里开始简易解析 JSON 请求体 ---
                // 预期格式例如: {"path":"/tmp", "cmd":"ls -l"}
                char current_path[2048] = "/";
                char username[128] = "admin"; // 默认用户名
                char *cmd_line = malloc(BUFFER_SIZE);
                if (!cmd_line) { free(buffer); close(client_sock); return NULL; }
                cmd_line[0] = '\0';
                
                char *user_start = strstr(body, "\"username\":\"");
                if (user_start) {
                    user_start += 12;
                    char *user_end = strchr(user_start, '"');
                    if (user_end) {
                        int len = user_end - user_start;
                        if (len < (int)sizeof(username)) {
                            strncpy(username, user_start, len);
                            username[len] = '\0';
                        }
                    }
                }
                
                char *path_start = strstr(body, "\"path\":\"");
                if (path_start) {
                    path_start += 8;
                    char *path_end = strchr(path_start, '"');
                    if (path_end) {
                        int len = path_end - path_start;
                        if (len < (int)sizeof(current_path)) {
                            strncpy(current_path, path_start, len);
                            current_path[len] = '\0';
                        }
                    }
                }
                
                char *cmd_start = strstr(body, "\"cmd\":\"");
                if (cmd_start) {
                    cmd_start += 7;
                    int i = 0;
                    char *p = cmd_start;
                    while (*p != '\0' && i < BUFFER_SIZE - 1) {
                        if (*p == '\\' && *(p + 1) == '"') {
                            // 遇到转义的双引号，只存入双引号，跳过反斜杠
                            cmd_line[i++] = '"';
                            p += 2;
                        } else if (*p == '"') {
                            // 遇到真正的字段结束符 "
                            break;
                        } else {
                            // 其他字符直接存入
                            cmd_line[i++] = *p++;
                        }
                    }
                    cmd_line[i] = '\0';
                } else {
                    // 如果不是 JSON，尝试容错处理（假定全是命令，路径为默认根目录）
                    strncpy(cmd_line, body, BUFFER_SIZE - 1);
                }
                
                // cmd_line 如果有换行符会被切断后续逻辑，对于命令我们去尾，对于带内容的不用去
                if (!is_sys_save_command(cmd_line)) {
                    cmd_line[strcspn(cmd_line, "\r\n")] = '\0';
                }

                char *output = malloc(BUFFER_SIZE);
                if (!output) { free(cmd_line); free(buffer); close(client_sock); return NULL; }
                output[0] = '\0';
                char target_filename[256] = {0};
                
                // 获取命令名用于分发
                char cmd_name[64] = {0};
                sscanf(cmd_line, "%63s", cmd_name); // 取出第一段
                
                // ---- 高效的分发逻辑 (全面大下放策略) ----
                if (is_sys_save_command(cmd_line)) {
                    
                    handle_sys_save(current_path, cmd_line, output, BUFFER_SIZE);
                    
                } else if (is_editor_command(cmd_line, target_filename, sizeof(target_filename))) {

                    handle_editor_open(current_path, target_filename, output, BUFFER_SIZE);

                } else if (strcmp(cmd_name, "cd") == 0) {
                    
                    handle_dir_command(current_path, cmd_line, output, BUFFER_SIZE);
                        
                } else {
                    // 全面废弃手写的 ls rm touch 等，将所有的命令原封不动，连带它的复杂参数交由系统 shell 最终执行。
                    execute_long_process_command(client_sock, current_path, cmd_line, 0, username);
                    // 注意：现在日志记录逻辑 (含完整 output) 已经移步至 execute_long_process_command 内
                    close(client_sock);
                    free(output); free(cmd_line); free(buffer);
                    return NULL; // 直接退出线程，因为内部已经打过 HTTP 头并源源不断发送内容了
                }
                
                // 记录到 SQLite (非流式请求的日志，保存 output)
                log_command(username, current_path, cmd_line, output); 

                // 响应结果给前端（仅对非长耗时指令执行，如 cd 编辑等直接打回字符串的情况）
                char *response = malloc(BUFFER_SIZE + 512); // 稍微扩大缓冲以防溢出
                if (response) {
                    snprintf(response, BUFFER_SIZE + 512, 
                             "HTTP/1.1 200 OK\r\n"
                             "Content-Type: text/plain\r\n"
                             "Content-Length: %zu\r\n\r\n%s", 
                             strlen(output), output);
                    write(client_sock, response, strlen(response));
                    free(response);
                }
                
                free(output);
                free(cmd_line);
            }
        }
    }
    
    if (buffer) free(buffer);
    close(client_sock);
    return NULL;
}

/**
 * -----------------------------------------------------------------------------
 * 【网络/线程核心层】
 * 该函数负责循环接收浏览器端口发来的 TCP 请求。
 * 每接收到一个客人的连接 (accept)，为了避免由于这个客人下载大文件或者运行长命令
 * 导致后续所有的客人都被卡在门外，我们在这里采用了 "多线程并发模型"。
 * 即：来一个客人，就启动一个后台线程专门伺候（handle_client），主线程立刻回到
 * 门口继续揽客。
 * -----------------------------------------------------------------------------
 */
int main() {
    // 启动前初始化 SQLite 数据库
    init_db();
    
    int server_sock, *client_sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    
    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        perror("Socket 创建失败");
        exit(EXIT_FAILURE);
    }
    
    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    
    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind 失败");
        exit(EXIT_FAILURE);
    }
    
    if (listen(server_sock, 10) < 0) {
        perror("Listen 失败");
        exit(EXIT_FAILURE);
    }
    
    printf("WebSSH 服务器已启动，监听端口: %d\n", PORT);
    
    while (1) {
        // [并发机制说明] 主线程仅阻塞在 accept 等待新连接
        client_sock = malloc(sizeof(int));
        *client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &client_len);
        
        if (*client_sock < 0) {
            perror("Accept 失败");
            free(client_sock);
            continue;
        }
        
        // 分离新线程处理连接，有效避免由于某个请求阻塞而导致服务器停摆
        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, handle_client, (void *)client_sock) != 0) {
            perror("无法创建线程");
            free(client_sock);
        } else {
            // 设置线程分离模式，执行完自动释放资源
            pthread_detach(thread_id);
        }
    }
    
    close(server_sock);
    return 0;
}