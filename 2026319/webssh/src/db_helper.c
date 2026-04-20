#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sqlite3.h>
#include "db_helper.h"

static sqlite3 *db = NULL;

void init_db() {
    int rc = sqlite3_open("webssh_log.db", &db);
    if (rc) {
        fprintf(stderr, "无法打开数据库: %s\n", sqlite3_errmsg(db));
        return;
    }
    const char *sql = "CREATE TABLE IF NOT EXISTS command_log ("
                      "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                      "username TEXT, "
                      "path TEXT, "
                      "command TEXT, "
                      "output TEXT, "
                      "time DATETIME DEFAULT CURRENT_TIMESTAMP);";
    char *err_msg = 0;
    rc = sqlite3_exec(db, sql, 0, 0, &err_msg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL 错误: %s\n", err_msg);
        sqlite3_free(err_msg);
    }
}

/**
 * -----------------------------------------------------------------------------
 * 【安全参数化写入引擎及截断崩溃规避手段】
 * 这个函数将承接包含有 64KB 之内任何用户可能造成的不可预知结果输出文本。
 * 由于文本内不乏会存在乱码、奇怪单引号双引号或者故意写入的 `'); DROP TABLE ...`，
 * 我们在此绝不可以用平时最省事的 `sprintf(sql, "INSERT...%s")` 法则去送货，
 * 这会让数据库解析器瞬间卡死或遭到清空劫持。
 * 
 * 于是我们采用了高阶防护：
 * sqlite3_prepare_v2 制作带有参数坑位 `?` 的预编译语句柄。
 * sqlite3_bind_text 将那些带毒刺或包含回车的超级长文本绑在其内。
 * 尤其绑定函数带有 `SQLITE_TRANSIENT` 这个魔法参数，这让数据库在吃进资料前
 * "自己好好拷贝一份"，以免等下 C 语言这边的调用栈退出清理了这块内存，引向
 * 野指针的悲剧！
 * -----------------------------------------------------------------------------
 */
void log_command(const char *username, const char *path, const char *command, const char *output) {
    if (!db) return;
    
    // 参数化查询，防止特殊字符（如引号、换行符）导致 SQL 拼接断裂并防御注入
    const char *sql = "INSERT INTO command_log (username, path, command, output) VALUES (?, ?, ?, ?);";
    sqlite3_stmt *stmt;
    
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL 准备失败: %s\n", sqlite3_errmsg(db));
        return;
    }
    
    // 绑定参数：参数索引从 1 开始
    sqlite3_bind_text(stmt, 1, username ? username : "unknown", -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, path ? path : "/", -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, command ? command : "", -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, output ? output : "", -1, SQLITE_TRANSIENT);
    
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "SQL 插入失败: %s\n", sqlite3_errmsg(db));
    }
    
    sqlite3_finalize(stmt);
}