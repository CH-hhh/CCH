#ifndef FILE_IO_H
#define FILE_IO_H

#include <stddef.h>

int is_builtin_command(const char *cmd_name);
void handle_file_command(const char *current_path, const char *cmd_line, char *output, size_t out_len);
void handle_dir_command(const char *current_path, const char *cmd_line, char *output, size_t out_len);
void execute_long_process_command(int client_sock, const char *current_path, const char *cmd_line, size_t max_buff, const char *username);
void interrupt_current_process(void);

// 旧版的占位函数可以不要了，但为了兼容之前没有清理的部分，可以暂时留一个壳或者由上面替代
void execute_shell_command(const char *cmd, char *output, size_t out_len); 

#endif