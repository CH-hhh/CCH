#include "../include/reques.h"
#include <string.h>
#include <stdio.h>
//原始数据
//请求行　　
//请求头　　
//请求体
//GET/POST/...   /search?p=w&q=1  HTTP/1.1
//Accept: text/html;image:*
//Request
int parse_request(char* data, Request* req){
    //1 使用strｔok函数以换行符\n进行切割　　只取第一行
    char* line = strtok(data, "\n");   //GET /search?p=w&q=1 HTTP/1.1
    if(!line)
        return 0;

    //2 再次使用strｔok函数切割，换取三部分数据
    char* method = strtok(line, " ");  //  (1)GET (2)/search?p=w&q=1 HTTP/1.1
    char* path = strtok(NULL, " ");  //(1)/search?p=w&q=1 (2)HTTP/1.1
    char* version = strtok(NULL, " ");  //HTTP/1.1

    if(!method || !path || !version)
        return 0;

    //3 将解析出来的数据填充到req
    strncpy(req->method, method, 15);
    strncpy(req->version, version, 15);

    //４　对路径字符串进行url_decode  search?q=%26 ---> search?q=&
    decode_url(path);
    strncpy(req->path, path, 255);

    //简易处理，在字符串最尾部加上结束标记
    req->method[15] = '\0';
    req->version[15] = '\0';
    req->path[255] = '\0';
    return 1;
}

//对url进行解码操作
//处理　％　＋
// %36 --> &
void decode_url(char *url){
    char *src = url, *dst = url;
    // src  /se?q=%38
    // dst  /se?q=
    while(*src != '\0'){
        if(*src == '%' && src[1] && src[2]){
            int hex = 0;
            sscanf(src + 1, "%2x", &hex);   //hex = 38  &
            *dst = hex;
            src += 3;
            dst++;
        }else if(*src == '+'){
            *dst = ' ';
            src++;
            dst++;
        }else{
            *dst = *src;
            dst++;
            src++;
        }
    }
    *dst = '\0';
}



//int main(){
//    char url[] = "/se?q=%26";
//    decode_url(url);
//    printf("%s\n", url);
//}


