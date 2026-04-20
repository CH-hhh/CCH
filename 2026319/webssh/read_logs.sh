#!/bin/bash

# 一个简易的查看 WebSSH 命令日志的脚本
DB_FILE="webssh_log.db"

if [ ! -f "$DB_FILE" ]; then
    echo "错误：找不到日志数据库文件 $DB_FILE"
    exit 1
fi

echo "====================== WebSSH 全部日志记录 ======================"
# 使用 sqlite3 的 column 模式输出，更易于阅读
sqlite3 -header -column "$DB_FILE" "SELECT * FROM command_log;"
echo "============================================================================"
