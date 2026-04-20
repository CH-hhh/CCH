def run():
    new_hc = """// Created by gqx on 3/15/26.
#include "../include/server.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include<sys/epoll.h>
#include<errno.h>
#include <string.h>
#include <stdlib.h>
#include "../include/reques.h"
#include "../include/util.h"
#include "../include/response.h"
#include <sqlite3.h>
#include <pthread.h>
#include <ctype.h>

int server_running = 1;
#define MAX_EVENTS 64

int create_socket(int port){
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if(fd < 0)  return -1;
    int reuse = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        perror("setsockopt");
        close(fd);
        return -1;
    }
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = INADDR_ANY;
    if(bind(fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) return -1;
    if(listen(fd, 128) < 0) return -1;
    return fd;
}

int set_nonblocking(int fd){
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

typedef struct Task {
    char cmd[4096];
    char *file_content;
    int is_save;
    char *result_out;
    int done;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    struct Task *next;
} Task;

Task *head_task = NULL;
Task *tail_task = NULL;
pthread_mutex_t queue_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t queue_cond = PTHREAD_COND_INITIALIZER;
static int thread_started = 0;

void* executor_thread(void* arg) {
    while(1) {
        pthread_mutex_lock(&queue_lock);
        while(head_task == NULL) pthread_cond_wait(&queue_cond, &queue_lock);
        Task *t = head_task;
        head_task = head_task->next;
        if(head_task == NULL) tail_task = NULL;
        pthread_mutex_unlock(&queue_lock);
        
        t->result_out = malloc(1024 * 1024);
        memset(t->result_out, 0, 1024 * 1024);
        
        if (t->is_save) {
            FILE *f = fopen(t->cmd, "w");
            if (f) {
                if(t->file_content) fwrite(t->file_content, 1, strlen(t->file_content), f);
                fclose(f);
                sprintf(t->result_out, "[System] Saved file: %s", t->cmd);
            } else {
                sprintf(t->result_out, "[Error] Failed to save: %s", t->cmd);
            }
        } else {
            char safe_cmd[8192] = {0};
            if (strncmp(t->cmd, "cd ", 3) == 0 || strcmp(t->cmd, "cd") == 0) {
                char *dir = t->cmd + 3;
                if (strlen(t->cmd) == 2 || strcmp(dir, "~") == 0) dir = getenv("HOME");
                if (dir && chdir(dir) == 0) sprintf(t->result_out, "[chdir] Changed directory to %s", dir);
                else sprintf(t->result_out, "cd: %s: No such file or directory", dir ? dir : "unknown");
            } else {
                if (strcmp(t->cmd, "top") == 0 || strncmp(t->cmd, "top ", 4) == 0) snprintf(safe_cmd, sizeof(safe_cmd), "top -b -n 1 2>&1");
                else snprintf(safe_cmd, sizeof(safe_cmd), "timeout 2s %s < /dev/null 2>&1", t->cmd);
                FILE *fp = popen(safe_cmd, "r");
                if(fp) {
                    int r = fread(t->result_out, 1, 1024*1024 - 1, fp);
                    t->result_out[r] = '\\0';
                    pclose(fp);
                }
            }
        }
        pthread_mutex_lock(&t->lock);
        t->done = 1;
        pthread_cond_signal(&t->cond);
        pthread_mutex_unlock(&t->lock);
    }
    return NULL;
}

char* escape_html(const char* src) {
    if (!src) return strdup("");
    size_t len = 0;
    for (int i=0; src[i]; i++) {
        if (src[i]=='<' || src[i]=='>') len += 4;
        else if (src[i]=='&' || src[i]=='\"') len += 5;
        else len++;
    }
    char *res = malloc(len + 1);
    size_t j = 0;
    for (int i=0; src[i]; i++) {
        if (src[i]=='<') { strcpy(&res[j], "&lt;"); j+=4; }
        else if (src[i]=='>') { strcpy(&res[j], "&gt;"); j+=4; }
        else if (src[i]=='&') { strcpy(&res[j], "&amp;"); j+=5; }
        else if (src[i]=='\"') { strcpy(&res[j], "&quot;"); j+=6; }
        else { res[j++] = src[i]; }
    }
    res[len] = '\\0';
    return res;
}

void url_decode_body(char *dst, const char *src) {
    char a, b;
    while (*src) {
        if ((*src == '%') && ((a = src[1]) && (b = src[2])) && (isxdigit(a) && isxdigit(b))) {
            if (a >= 'a') a -= 'a'-'A';
            else if (a >= 'A') a -= ('A' - 10);
            else a -= '0';
            if (b >= 'a') b -= 'a'-'A';
            else if (b >= 'A') b -= ('A' - 10);
            else b -= '0';
            *dst++ = 16*a+b;
            src+=3;
        } else if (*src == '+') { *dst++ = ' '; src++; }
        else { *dst++ = *src++; }
    }
    *dst = '\\0';
}

void handle_client(int client_fd){
    if (!thread_started) {
        thread_started = 1;
        pthread_t tid;
        pthread_create(&tid, NULL, executor_thread, NULL);
        pthread_detach(tid);
    }
    size_t buffer_size = 1024 * 512;
    char *buffer = malloc(buffer_size);
    memset(buffer, 0, buffer_size);

    int bytes = recv(client_fd, buffer, buffer_size - 1, 0);
    if(bytes <= 0){ close(client_fd); free(buffer); return; }

    char *body_ptr = strstr(buffer, "\\r\\n\\r\\n");
    if (body_ptr) {
        body_ptr += 4;
        char *cl_ptr = strstr(buffer, "Content-Length: ");
        if (cl_ptr) {
            int body_len = atoi(cl_ptr + 16);
            int current_body = bytes - (body_ptr - buffer);
            while (current_body < body_len && bytes < buffer_size - 1) {
                int r = recv(client_fd, buffer + bytes, buffer_size - 1 - bytes, 0);
                if (r <= 0) break;
                bytes += r;
                current_body += r;
            }
        }
    }
    buffer[bytes] = '\\0';
    char *req_header = strdup(buffer);
    Request req;
    char *response = NULL;

    if(parse_request(req_header, &req) == 1){
        if(strcmp(req.method, "GET") == 0 || strcmp(req.method, "POST") == 0){
            char path[512];
            if (strcmp(req.method, "POST") == 0 && strncmp(req.path, "/cmd_save", 9) == 0) {
                char *filename = NULL; char *filecontent = NULL;
                char *file_key = strstr(body_ptr, "file=");
                char *content_key = strstr(body_ptr, "&content=");
                if (file_key && content_key) {
                    *content_key = '\\0';
                    filename = file_key + 5;
                    content_key += 9;
                    filecontent = content_key;
                    char dec_filename[512] = {0};
                    url_decode_body(dec_filename, filename);
                    char *dec_content = malloc(strlen(filecontent) + 1);
                    url_decode_body(dec_content, filecontent);
                    
                    Task *t = malloc(sizeof(Task));
                    strcpy(t->cmd, dec_filename);
                    t->file_content = dec_content;
                    t->is_save = 1; t->done = 0;
                    pthread_mutex_init(&t->lock, NULL); pthread_cond_init(&t->cond, NULL);
                    t->next = NULL;
                    
                    pthread_mutex_lock(&queue_lock);
                    if(!tail_task) head_task = tail_task = t;
                    else { tail_task->next = t; tail_task = t; }
                    pthread_cond_signal(&queue_cond);
                    pthread_mutex_unlock(&queue_lock);
                    
                    pthread_mutex_lock(&t->lock);
                    while(!t->done) pthread_cond_wait(&t->cond, &t->lock);
                    pthread_mutex_unlock(&t->lock);
                    
                    char *html_template = "<!DOCTYPE html><html><body style=\\"background:#000;color:#0f0;font-family:monospace;\\">"
                                          "<div style='color:#4af626'>root@webserver:~# save %s</div><pre>%s</pre>"
                                          "<form action=\\"/cmd\\" method=\\"GET\\">root@webserver:~# "
                                          "<input type=\\"text\\" name=\\"run\\" autofocus style=\\"background:#000;color:#0f0;border:none;outline:none;width:80%%;\\"></form>"
                                          "</body><script>window.scrollTo(0,document.body.scrollHeight);</script></html>";
                    char *body = malloc(strlen(html_template) + strlen(t->cmd) + strlen(t->result_out) + 1);
                    sprintf(body, html_template, t->cmd, t->result_out);
                    char *header = "HTTP/1.1 200 OK\\r\\nContent-Type: text/html; charset=utf-8\\r\\nConnection: close\\r\\n\\r\\n";
                    response = malloc(strlen(header) + strlen(body) + 1);
                    sprintf(response, "%s%s", header, body);
                    free(t->result_out); free(t->file_content); free(t); free(body);
                }
            }
            else if(strncmp(req.path, "/cmd?run=", 9) == 0){
                char *cmd = req.path + 9;
                struct sockaddr_in addr;
                socklen_t addr_len = sizeof(addr);
                char ip_str[64] = "unknown";
                int port = 0;
                if (getpeername(client_fd, (struct sockaddr*)&addr, &addr_len) == 0) {
                    strcpy(ip_str, inet_ntoa(addr.sin_addr)); port = ntohs(addr.sin_port);
                }

                if (strncmp(cmd, "vi ", 3) == 0 || strncmp(cmd, "vim ", 4) == 0 || strncmp(cmd, "nano ", 5) == 0 || strncmp(cmd, "less ", 6) == 0) {
                    char *target = strchr(cmd, ' '); target++;
                    char file_data[1024 * 128] = {0};
                    FILE *fp = fopen(target, "r");
                    if (fp) { fread(file_data, 1, sizeof(file_data) - 1, fp); fclose(fp); }
                    char *esc_content = escape_html(file_data);
                    char *esc_target = escape_html(target);
                    
                    char *html_template = "<!DOCTYPE html><html><body style=\\"background:#000;color:#0f0;font-family:monospace;\\">"
                                          "<h3>Editing: %s</h3>"
                                          "<form action=\\"/cmd_save\\" method=\\"POST\\">"
                                          "<input type=\\"hidden\\" name=\\"file\\" value=\\"%s\\">"
                                          "<textarea name=\\"content\\" style=\\"width:90%%;height:400px;background:#111;color:#fff;border:1px solid #4af626;\\">%s</textarea><br>"
                                          "<input type=\\"submit\\" value=\\"Save and Exit\\" style=\\"margin-top:10px;padding:5px 15px;background:#4af626;color:#000;border:none;cursor:pointer;\\">"
                                          "</form>"
                                          "<form action=\\"/cmd\\" method=\\"GET\\" style=\\"margin-top:10px;\\">"
                                          "<input type=\\"hidden\\" name=\\"run\\" value=\\"ls\\"><input type=\\"submit\\" value=\\"Cancel\\" style=\\"padding:5px 15px;background:#f00;color:#fff;border:none;cursor:pointer;\\">"
                                          "</form></body></html>";
                    char *body = malloc(strlen(html_template) + strlen(esc_target)*2 + strlen(esc_content) + 1);
                    sprintf(body, html_template, esc_target, esc_target, esc_content);
                    char *header = "HTTP/1.1 200 OK\\r\\nContent-Type: text/html; charset=utf-8\\r\\nConnection: close\\r\\n\\r\\n";
                    response = malloc(strlen(header) + strlen(body) + 1);
                    sprintf(response, "%s%s", header, body);
                    free(body); free(esc_content); free(esc_target);
                } 
                else {
                    Task *t = malloc(sizeof(Task));
                    strcpy(t->cmd, cmd); t->is_save = 0; t->done = 0;
                    pthread_mutex_init(&t->lock, NULL); pthread_cond_init(&t->cond, NULL);
                    t->next = NULL;
                    pthread_mutex_lock(&queue_lock);
                    if(!tail_task) head_task = tail_task = t; else { tail_task->next = t; tail_task = t; }
                    pthread_cond_signal(&queue_cond);
                    pthread_mutex_unlock(&queue_lock);
                    
                    pthread_mutex_lock(&t->lock);
                    while(!t->done) pthread_cond_wait(&t->cond, &t->lock);
                    pthread_mutex_unlock(&t->lock);
                    
                    char *esc_out = escape_html(t->result_out);

                    sqlite3 *db;
                    if (sqlite3_open("history.db", &db) == SQLITE_OK) {
                        char *err_msg = 0;
                        sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS history (id INTEGER PRIMARY KEY AUTOINCREMENT, ip TEXT, port INTEGER, cmd TEXT, exec_time DATETIME DEFAULT CURRENT_TIMESTAMP, output TEXT);", 0, 0, &err_msg);
                        char *sql_insert = sqlite3_mprintf("INSERT INTO history (ip, port, cmd, output) VALUES ('%q', %d, '%q', '%q');", ip_str, port, cmd, esc_out);
                        sqlite3_exec(db, sql_insert, 0, 0, &err_msg);
                        sqlite3_free(sql_insert);
                        
                        char history_html[1024*1024] = {0};
                        const char *sql_select = "SELECT cmd, output FROM (SELECT id, cmd, output FROM history ORDER BY id DESC LIMIT 20) ORDER BY id ASC;";
                        sqlite3_stmt *stmt;
                        if (sqlite3_prepare_v2(db, sql_select, -1, &stmt, 0) == SQLITE_OK) {
                            while (sqlite3_step(stmt) == SQLITE_ROW) {
                                const unsigned char *h_cmd = sqlite3_column_text(stmt, 0); const unsigned char *h_out = sqlite3_column_text(stmt, 1);
                                if (h_cmd && h_out) {
                                    char block[1024*10] = {0};
                                    char *esc_cmd_val = escape_html((char*)h_cmd);
                                    snprintf(block, sizeof(block)-1, "<div style='color:#4af626'>root@webserver:~# %s</div><pre style='color:#d4d4d4;margin:0;padding-bottom:10px;'>%s</pre>", esc_cmd_val, (char*)h_out);
                                    if(strlen(history_html) + strlen(block) < sizeof(history_html)-1) strcat(history_html, block);
                                    free(esc_cmd_val);
                                }
                            }
                            sqlite3_finalize(stmt);
                        }
                        sqlite3_close(db);

                        char *html_t = "<!DOCTYPE html><html><body style=\\"background:#000;font-family:monospace;padding:10px;\\"><div id=\\"h_box\\">%s</div>"
                                       "<form action=\\"/cmd\\" method=\\"GET\\" style=\\"margin-top:10px;\\"><span style=\\"color:#4af626;\\">root@webserver:~#</span> <input type=\\"text\\" name=\\"run\\" autocomplete=\\"off\\" autofocus style=\\"background:#000;color:#0f0;border:none;outline:none;width:80%%;\\"></form></body><script>window.scrollTo(0,document.body.scrollHeight);</script></html>";
                        char *body = malloc(strlen(html_t) + strlen(history_html) + 1);
                        sprintf(body, html_t, history_html);
                        char *header = "HTTP/1.1 200 OK\\r\\nContent-Type: text/html; charset=utf-8\\r\\nConnection: close\\r\\n\\r\\n";
                        response = malloc(strlen(header) + strlen(body) + 1);
                        sprintf(response, "%s%s", header, body);
                        free(body);
                    }
                    free(t->result_out); free(t); free(esc_out);
                }
            } else {
                if(strcmp(req.path, "/") == 0) strcpy(path, "static/index.html");
                else sprintf(path, "static%s", req.path);
                
                if(file_exists(path)) response = file_response(path);
                else response = error_response(404, "Not Found");
            }
        }
    }
    if(response){ send(client_fd, response, strlen(response), 0); free(response); }
    close(client_fd); free(buffer); free(req_header);
}

int start_server(int port){
    int server_fd = create_socket(port);
    if(server_fd < 0) return -1;
    set_nonblocking(server_fd);
    int epoll_fd = epoll_create1(0);
    if(epoll_fd == -1) return -1;
    struct epoll_event event;
    event.events = EPOLLIN | EPOLLET;
    event.data.fd = server_fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &event);
    printf("webserver is running! listening port %d\\n", port);
    struct epoll_event events[MAX_EVENTS];
    while(server_running){
        int num_events = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if(num_events == -1){ if(errno == EINTR) continue; break; }
        for(int i = 0; i < num_events; i++){
            if(events[i].data.fd == server_fd){
                while (1) {
                    struct sockaddr_in client_addr;
                    socklen_t client_len = sizeof(client_addr);
                    int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
                    if(client_fd == -1){ if(errno == EAGAIN || errno == EWOULDBLOCK) break; else continue; }
                    printf("Client %s:%d connected\\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
                    struct epoll_event client_event;
                    client_event.events = EPOLLIN | EPOLLET;
                    client_event.data.fd = client_fd;
                    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &client_event);
                }
            }else{
                int client_fd = events[i].data.fd;
                handle_client(client_fd);
                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
            }
        }
    }
    close(epoll_fd); close(server_fd);
    printf("webserver stopped!\\n");
    return 0;
}
"""
    with open("src/server.c", "w") as f:
        f.write(new_hc)

run()
