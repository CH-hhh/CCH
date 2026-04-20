//模拟HTTP请求

#ifndef REQUEST_H
#define REQUEST_H

//127.0.0.1/index.html?a=1&b=2
//请求行 GET/POST/...   /index.html?a=1&b=2  HTTP/1.1
//请求头  Accept: text/html;image/*;....
//请求体

//请求行 GET/POST/...   /index.html?a=1&b=2  HTTP/1.1
typedef struct{
    char method[16];    //请求方法　　最长15个字符＋终止字符
    char path[256];     //请求路径　　最长255个字符＋终止字符
    char version[16];   //HTTP版本　　最长15个字符＋终止字符
}Request;

//解析HTTP请求报文，填充到Request结构体
int parse_request(char* data, Request* req);


//对url进行解码操作
void decode_url(char *url);

#endif