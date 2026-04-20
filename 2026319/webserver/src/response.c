//
// Created by gqx on 3/15/26.
//
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <memory.h>
#include "../include/util.h"
#include "../include/reques.h"
/**
 * 构造完整的响应报文
 * @param code　　HTTP状态码（例如200 404 500）
 * @param type　　内容类型（响应体的数据类型）
 * @param body　　响应体的数据指针
 * @param length　响应体数据的长度
 * @return
 */
char *make_response(int code, char *type, char *body, int length){
    //1　创建储存数据的响应体字符串变量
    char *response = (char*)malloc(512 + length); //512为相应行和响应头的最大长度
    if(!response)  return NULL;  //申请失败

    //２　状态描述映射：将数字状态转换为对应的文本描述
    char *status = "OK";
    if(code == 404){
        status = "Not Found";
    }else if(code == 400){
        status = "Bad Request";
    }else if(code == 403){
        status = "Forbidden";
    }else if(code == 500){
        status = "Internal Server Error";
    }

    //3 生成时间戳生成ＧＭＴ格式的时间戳
    time_t now = time(NULL);
    char time_str[100];
    //将time_t转化为字符串格式
    strftime(time_str, sizeof(time_str),"%a, %d %b %Y %H:%M:%S GMT", gmtime(&now));

    /*
     * HTTP/1.1 200 ok
     * Date: Thu, 05 Mar 2026 12:09:54 GMT
     * Server: webServer
     * (pos)
     */
    int pos = snprintf(response, 512,
                       "HTTP/1.1 %d %s\r\n"
                       "Date: %s\r\n"
                       "Content-Length: %d\r\n"
                       "Server: webServer\r\n", code, status, time_str, length);

    if(type){
        /*
         * HTTP/1.1 200 ok
         * Date: Thu, 05 Mar 2026 12:09:54 GMT
         * Server: webServer
         * Content-type: application/x-javascript
         * (pos)
         */
        int loc = snprintf(response + pos, 512 - pos, "Content-type: %s\r\n", type);
        pos = pos + loc;
    }

    /*
     * HTTP/1.1 200 ok
     * Date: Thu, 05 Mar 2026 12:09:54 GMT
     * Server: webServer
     * Content-type: application/x-javascript
     * (空行)
     * (pos)
     */
    int loc = snprintf(response + pos, 512 - pos, "\r\n");  //响应头和响应体之间存在一个空行
    pos = pos + loc;

    if(body && length > 0){
        memcpy(response + pos, body, length);
    }

    return response;
}

//根据文件扩展名返回ＭＩＭＥ类型
char* get_mime_type(char *path){
    // 查找最后一个点号
    char* ext = strrchr(path, '.');
    if(!ext){
        return "application/octet-stream";
    }
    if(strcmp(ext, ".html") == 0){
        return "text/html;charset=utf-8";
    }else if(strcmp(ext, ".css") == 0){
        return "text/css";
    }else if(strcmp(ext, ".js") == 0){
        return "text/javascript";
    }else if(strcmp(ext, ".jpg") == 0){
        return "image/jpeg";
    }else if(strcmp(ext, ".png") == 0){
        return "image/png";
    }
    return "application/octet-stream";
}

/**
 * 生成错误响应页面
 * @param code　错误码
 * @param msg　　错误信息
 * @return
 */
char *error_response(int code, char* msg){
    char *html = (char*)malloc(512);
    if(!html) return NULL;
    memset(html, 0, 512);
    //格式化字符串
    snprintf(html, 512, "<html><head><meta charset=\"utf-8\"></head><body><h1>%d 错误</h1>"
                        "<p> 错误信息：%s</p>"
                        "</body></html>", code, msg);

    char *response = make_response(code, "text/html", html, strlen(html));
    free(html);
    return response;
}


//读取请求路径对应的文件生成HTTP响应报文
char* file_response(char *path){
    char *data = NULL;
    int len = 0;
    //读取路径上的文件
    len = read_file(path, &data);
    if(len > 0){
        //根据文件尾部的扩展名获取类型
        //get_mime_type函数是根据文件扩展名（extension）获取对应的 MIME 类型
        //输入: 文件扩展名字符串（如 "txt"、"jpg"、"pdf"）---对应的标准 MIME 类型字符串（如 "text/plain"、"image/jpeg"、"application/pdf"）
        char *type = get_mime_type(path);
        char *response = make_response(200, type, data, len);
        return response;
    }
    free(data);
    return error_response(500, "读取失败");
}



//int main02(){
//    char *body = "hello world!";
//    char *resp = make_response(200, "text/html", body, 12);
//    printf("%s\n", resp);
//}