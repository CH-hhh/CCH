#ifndef CMD_VALIDATOR_H
#define CMD_VALIDATOR_H

#include <stddef.h>

/**
 * 拦截检查：是否为编辑器指令
 * @return: 是返回 1 并将文件名存入 filename 中，否返回 0
 */
int is_editor_command(const char *cmd_line, char *filename, size_t max_len);

/**
 * 拦截检查：是否为前端发来的内部系统保存指令
 * @return: 是返回 1，否返回 0
 */
int is_sys_save_command(const char *cmd_line);

/**
 * 读取文件内容，并按 EDITOR_OPEN:{filename}\n{content} 协议存入 output
 */
void handle_editor_open(const char *current_path, const char *filename, char *output, size_t out_len);

/**
 * 解析系统保存指令，使用原生 C I/O 安全覆写文件
 */
void handle_sys_save(const char *current_path, const char *cmd_line, char *output, size_t out_len);

#endif