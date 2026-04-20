//
// Created by gqx on 3/15/26.
//

#ifndef WEBSERVER_RESPONSE_H
#define WEBSERVER_RESPONSE_H

//生成完整的HTTP响应报文
char *make_response(int code, char *type, char *body, int length);

//读取请求路径对应的文件生成HTTP响应报文
char *file_response(char *path);


/**
 * 生成错误响应页面
 * @param code　错误码
 * @param msg　　错误信息
 * @return
 */
char *error_response(int code, char* msg);

#endif //WEBSERVER_RESPONSE_H
