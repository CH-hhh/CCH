import sys

def process():
    file_path = "src/server.c"
    with open(file_path, "r") as f:
        content = f.read()

    # 1. ADD PTHREAD AND THREADING LOGIC
    includes = """#include "../include/server.h"
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

// ------------- MULTI-THREAD COMMAND QUEUE -------------
typedef struct Task {
    char cmd[4096];
    char *file_content; // dynamically allocated for large files
    int is_save;
    
    char *result_out; // Dynamically allocated
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
        while(head_task == NULL) {
            pthread_cond_wait(&queue_cond, &queue_lock);
        }
        Task *t = head_task;
        head_task = head_task->next;
        if(head_task == NULL) tail_task = NULL;
        pthread_mutex_unlock(&queue_lock);
        
        t->result_out = malloc(1024 * 1024);
        memset(t->result_out, 0, 1024 * 1024);
        
        if (t->is_save) {
            FILE *f = fopen(t->cmd, "w");
            if (f) {
                fwrite(t->file_content, 1, strlen(t->file_content), f);
                fclose(f);
                sprintf(t->result_out, "[System] Saved file: %s", t->cmd);
            } else {
                sprintf(t->result_out, "[Error] Failed to save: %s", t->cmd);
            }
        } else {
            char safe_cmd[1024] = {0};
            if (strncmp(t->cmd, "cd ", 3) == 0 || strcmp(t->cmd, "cd") == 0) {
                char *dir = t->cmd + 3;
                if (strlen(t->cmd) == 2 || strcmp(dir, "~") == 0) dir = getenv("HOME");
                if (dir && chdir(dir) == 0) {
                    sprintf(t->result_out, "[chdir] Changed directory to %s", dir);
                } else {
                    sprintf(t->result_out, "cd: %s: No such file or directory", dir ? dir : "unknown");
                }
            } else {
                if (strcmp(t->cmd, "top") == 0 || strncmp(t->cmd, "top ", 4) == 0) {
                    snprintf(safe_cmd, sizeof(safe_cmd), "top -b -n 1 2>&1");
                } else {
                    snprintf(safe_cmd, sizeof(safe_cmd), "timeout 2s %s < /dev/null 2>&1", t->cmd);
                }
                FILE *fp = popen(safe_cmd, "r");
                if(fp) {
                    int r = fread(t->result_out, 1, 1024*1024 - 1, fp);
                    t->result_out[r] = '\0';
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
    res[len] = '\0';
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
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}
"""

    content = content.replace('#include "../include/response.h"\n#include <sqlite3.h>', includes)
    content = content.replace('#include "../include/response.h"', includes)
    if "MULTITHREAD" not in content:
        # If not already replaced... wait, let's just make it robust
        pass

    # Find handle_client
    start_hc = content.find('void handle_client(int client_fd){')
    
    new_hc = """void handle_client(int client_fd){
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
    if(bytes <= 0){
        close(client_fd);
        free(buffer);
        return;
    }

    // handle Content-Length for POST
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
            
            // EDITOR FORM SUBMIT (POST) -- Save file
            if (strcmp(req.method, "POST") == 0 && strncmp(req.path, "/cmd_save", 9) == 0) {
                char *filename = NULL;
                char *filecontent = NULL;
                
                // Extremely simple form parsing: "file=...&content=..."
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
                    
                    // Disptach to thread
                    Task *t = malloc(sizeof(Task));
                    strcpy(t->cmd, dec_filename);
                    t->file_content = dec_content;
                    t->is_save = 1;
                    t->done = 0;
                    pthread_mutex_init(&t->lock, NULL);
                    pthread_cond_init(&t->cond, NULL);
                    t->next = NULL;
                    
                    pthread_mutex_lock(&queue_lock);
                    if(!tail_task) head_task = tail_task = t;
                    else { tail_task->next = t; tail_task = t; }
                    pthread_cond_signal(&queue_cond);
                    pthread_mutex_unlock(&queue_lock);
                    
                    // wait for thread
                    pthread_mutex_lock(&t->lock);
                    while(!t->done) pthread_cond_wait(&t->cond, &t->lock);
                    pthread_mutex_unlock(&t->lock);
                    
                    // Re-route to standard view, with result
                    char *html_template = "<!DOCTYPE html><html><body style=\\"background:#000;color:#0f0;font-family:monospace;\\">"
                                          "<div style='color:#4af626'>root@webserver:~# save %s</div><pre>%s</pre>"
                                          "<form action=\\"/cmd\\" method=\\"GET\\">root@webserver:~# "
                                          "<input type=\\"text\\" name=\\"run\\" autofocus style=\\"background:#000;color:#0f0;border:none;outline:none;width:80%%;\\"></form>"
                                          "</body></html>";
                    char *body = malloc(strlen(html_template) + strlen(t->cmd) + strlen(t->result_out) + 1);
                    sprintf(body, html_template, t->cmd, t->result_out);
                    char *header = "HTTP/1.1 200 OK\\r\\nContent-Type: text/html; charset=utf-8\\r\\nConnection: close\\r\\n\\r\\n";
                    response = malloc(strlen(header) + strlen(body) + 1);
                    sprintf(response, "%s%s", header, body);
                    
                    free(t->result_out); free(t->file_content); free(t); free(body);
                }
            }
            // NORMAL COMMAND EXECUTE / EDITOR VIEW
            else if(strncmp(req.path, "/cmd?run=", 9) == 0){
                char *cmd = req.path + 9;
                
                struct sockaddr_in addr;
                socklen_t addr_len = sizeof(addr);
                char ip_str[64] = "unknown";
                int port = 0;
                if (getpeername(client_fd, (struct sockaddr*)&addr, &addr_len) == 0) {
                    strcpy(ip_str, inet_ntoa(addr.sin_addr));
                    port = ntohs(addr.sin_port);
                }

                // If vi, vim, nano, less : INTERCEPT TO EDITOR UI
                if (strncmp(cmd, "vi ", 3) == 0 || strncmp(cmd, "vim ", 4) == 0 || strncmp(cmd, "nano ", 5) == 0 || strncmp(cmd, "less ", 6) == 0) {
                    char *target = strchr(cmd, ' ');
                    target++; // skip space
                    
                    char file_data[1024 * 128] = {0};
                    FILE *fp = fopen(target, "r");
                    if (fp) {
                        fread(file_data, 1, sizeof(file_data) - 1, fp);
                        fclose(fp);
                    } else {
                        strcpy(file_data, "");
                    }
                    
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
                                          "<input type=\\"hidden\\" name=\\"run\\" value=\\"ls\\">"
                                          "<input type=\\"submit\\" value=\\"Cancel\\" style=\\"padding:5px 15px;background:#f00;color:#fff;border:none;cursor:pointer;\\">"
                                          "</form></body></html>";
                    char *body = malloc(strlen(html_template) + strlen(esc_target)*2 + strlen(esc_content) + 1);
                    sprintf(body, html_template, esc_target, esc_target, esc_content);
                    
                    char *header = "HTTP/1.1 200 OK\\r\\nContent-Type: text/html; charset=utf-8\\r\\nConnection: close\\r\\n\\r\\n";
                    response = malloc(strlen(header) + strlen(body) + 1);
                    sprintf(response, "%s%s", header, body);
                    
                    free(body); free(esc_content); free(esc_target);
                } 
                else {
                    // Send to executor thread
                    Task *t = malloc(sizeof(Task));
                    strcpy(t->cmd, cmd);
                    t->is_save = 0;
                    t->done = 0;
                    pthread_mutex_init(&t->lock, NULL);
                    pthread_cond_init(&t->cond, NULL);
                    t->next = NULL;
                    
                    pthread_mutex_lock(&queue_lock);
                    if(!tail_task) head_task = tail_task = t;
                    else { tail_task->next = t; tail_task = t; }
                    pthread_cond_signal(&queue_cond);
                    pthread_mutex_unlock(&queue_lock);
                    
                    pthread_mutex_lock(&t->lock);
                    while(!t->done) pthread_cond_wait(&t->cond, &t->lock);
                    pthread_mutex_unlock(&t->lock);
                    
                    char *esc_out = escape_html(t->result_out);

                    // SQLite DB (done in HTTP thread to simplify)
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
                                const unsigned char *h_cmd = sqlite3_column_text(stmt, 0);
                                const unsigned char *h_out = sqlite3_column_text(stmt, 1);
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

    if(response){
        send(client_fd, response, strlen(response), 0);
        free(response);
    }
    close(client_fd);
    free(buffer);
    free(req_header);
}"""

    # We need to strip the existing handle_client. Let's do a substring replace.
    end_of_file = len(content)
    # search for handle_client
    # replace everything from handle_client until the end of the file, assuming it's the last function
    front_matter = content[:start_hc]
    
    with open(file_path, "w") as f:
        f.write("#include <pthread.h>\n#include <ctype.h>\n" + front_matter + new_hc)

process()
