#ifndef DB_HELPER_H
#define DB_HELPER_H

void init_db();
void log_command(const char *username, const char *path, const char *command, const char *output);

#endif