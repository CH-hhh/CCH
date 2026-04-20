//
// Created by gqx on 3/19/26.
//

#ifndef WEBSERVER_UTIL_H
#define WEBSERVER_UTIL_H

/**
 * 读取文件
 * @param path　　文件路径
 * @param data  　读取到的内容
 * @return
 */
int read_file(char *path, char *data);

/**
 * 检查路径文件是否存在
 * @param path　输入路径
 * @return　　１：存在　　　０：不存在
 */
int file_exists(char *path);

#endif //WEBSERVER_UTIL_H
