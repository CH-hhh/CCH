#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cmd_validator.h"

// 极简绝对路径拼接
static void build_validator_abs_path(const char *current_path, const char *arg, char *result, size_t res_len) {
    if (arg && arg[0] == '/') {
        snprintf(result, res_len, "%s", arg);
    } else {
        snprintf(result, res_len, "%s/%s", current_path, arg ? arg : "");
    }
}

int is_editor_command(const char *cmd_line, char *filename, size_t max_len) {
    char cmd_copy[512];
    strncpy(cmd_copy, cmd_line, sizeof(cmd_copy) - 1);
    cmd_copy[sizeof(cmd_copy) - 1] = '\0';
    
    char *cmd = strtok(cmd_copy, " \t\n\r");
    if (!cmd) return 0;
    
    if (strcmp(cmd, "vi") == 0 || strcmp(cmd, "vim") == 0 || strcmp(cmd, "nano") == 0) {
        char *arg = strtok(NULL, " \t\n\r");
        if (arg) {
            strncpy(filename, arg, max_len - 1);
            filename[max_len - 1] = '\0';
            return 1;
        }
    }
    return 0;
}

int is_sys_save_command(const char *cmd_line) {
    return (strncmp(cmd_line, "SYS_SAVE_FILE ", 14) == 0);
}

void handle_editor_open(const char *current_path, const char *filename, char *output, size_t out_len) {
    char abs_target[1024];
    build_validator_abs_path(current_path, filename, abs_target, sizeof(abs_target));
    
    // 头部协议写入
    int header_len = snprintf(output, out_len, "EDITOR_OPEN:%s\n", filename);
    if (header_len < 0 || (size_t)header_len >= out_len) return;
    
    FILE *f = fopen(abs_target, "r");
    if (f) {
        // 将文件内容附加到 header 之后
        char *content_ptr = output + header_len;
        size_t remain_len = out_len - header_len - 1; 
        
        size_t n = fread(content_ptr, 1, remain_len, f);
        content_ptr[n] = '\0';
        fclose(f);
    } else {
        // 文件不存在则视作空文件（新建），什么也不追加
        output[header_len] = '\0';
    }
}

/**
 * -----------------------------------------------------------------------------
 * 【文件系统降维打击与特殊转义反覆写协议层】
 * 前端其实发过来的一直是在一个普通的 TextArea 编辑框中编辑的普通文本，
 * 我们收到的可能带有 JSON 包装所带来的双引号、回车符号转义（例：\\n 这实际上是俩字符）。
 * 
 * 绝不能把含有 "\\n" 字面量的东西傻乎乎地写入文件，否则打开就是一个长串怪代码。
 * 
 * 本模块负责：
 * 1. 拆解指令里前端告诉我们要写的文件目标（SYS_SAVE_FILE file.txt，后面的紧跟内容）。
 * 2. 分配庞大的堆内存，开始一个字符一个字符循环，当遇到反斜杠 \\ 且后面是 n 时，
 *    我们将指针指向真实的 `\n` 回车符。并对 `"`, `t` 等逐个排查清洗。
 * 3. 最终通过最纯净安全的 C 原生 API（fopen 为 w 写模式，连同内容）将解码完毕
 *    的干净文本重新冲到硬盘上面。
 * -----------------------------------------------------------------------------
 */
void handle_sys_save(const char *current_path, const char *cmd_line, char *output, size_t out_len) {
    // cmd_line 格式为: SYS_SAVE_FILE filename\ncontent_start...
    // 注意：由于前端使用了 JSON.stringify，换行符在 cmd_line 中可能变成了字面量 "\\n" (两个字符: \ 和 n)，
    // 我们需要兼容解析这两种形式。
    const char *prefix = "SYS_SAVE_FILE ";
    const char *p = cmd_line + strlen(prefix);
    
    // 寻找真实换行符 '\n'，或者转义换行符 "\\n"
    const char *newline_pos = strchr(p, '\n');
    const char *content = NULL;
    int is_escaped_newline = 0;

    if (!newline_pos) {
        newline_pos = strstr(p, "\\n");
        if (newline_pos) {
            is_escaped_newline = 1;
        }
    }
    
    if (!newline_pos) {
        snprintf(output, out_len, "SYS_SAVE_ERROR: 缺少协议换行符\n");
        return;
    }
    
    char filename[256];
    size_t name_len = newline_pos - p;
    if (name_len >= sizeof(filename)) name_len = sizeof(filename) - 1;
    strncpy(filename, p, name_len);
    filename[name_len] = '\0';
    
    // 定位内容起点
    if (is_escaped_newline) {
        content = newline_pos + 2; // 跳过 "\\n"
    } else {
        content = newline_pos + 1; // 跳过 '\n'
    }
    
    char abs_target[1024];
    build_validator_abs_path(current_path, filename, abs_target, sizeof(abs_target));
    
    FILE *f = fopen(abs_target, "w");
    if (f) {
        // 如果使用了真实换行符直接存即可，如果是 JSON 转义我们为了简单暂时不再解码全文的 "\\n"，
        // 由于前端通常是通过 JS 的 fetch 参数传给我们的，如果是 application/json，后端简单解析（用 strstr 截取字符串），
        // 如果遇到 `\n` 被转义为 `\\n` 的问题，我们需要把写入文件的内容还原转换。
        size_t to_write = strlen(content);
        if (to_write > 0) {
            // 反转义：将 "\\n" 还原为 '\n' 等，将 "\\"" 还原为 '"', 等等极简处理
            char *decoded = malloc(to_write + 1);
            if (decoded) {
                int j = 0;
                for (size_t i = 0; i < to_write; i++) {
                    if (content[i] == '\\' && i + 1 < to_write) {
                        if (content[i+1] == 'n') { decoded[j++] = '\n'; i++; continue; }
                        if (content[i+1] == 'r') { decoded[j++] = '\r'; i++; continue; }
                        if (content[i+1] == 't') { decoded[j++] = '\t'; i++; continue; }
                        if (content[i+1] == '\\') { decoded[j++] = '\\'; i++; continue; }
                        if (content[i+1] == '"') { decoded[j++] = '"'; i++; continue; }
                    }
                    decoded[j++] = content[i];
                }
                decoded[j] = '\0';
                fwrite(decoded, 1, j, f);
                free(decoded);
            } else {
                fwrite(content, 1, to_write, f); // fallback
            }
        }
        fclose(f);
        snprintf(output, out_len, "SYS_SAVE_OK: 文件 %s 已安全保存\n", filename);
    } else {
        snprintf(output, out_len, "SYS_SAVE_ERROR: 无法写入 %s，权限不足或目录不存在\n", abs_target);
    }
}