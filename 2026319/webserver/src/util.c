//
// Created by gqx on 3/19/26.
//
#include <stdio.h>
#include <malloc.h>
#include <string.h>
#include <unistd.h>

int read_file(char *path, char **data){
    //1 打开文件
    FILE* file = fopen(path, "rb");
    if(!file){
        perror("文件打开失败！\n");
        return 0;
    }

    //２ 获取文件的大小
    //打开文件后，将文件位置指针移动到文件末尾
    fseek(file, 0, SEEK_END);
    //获取当前我呢见指针的位置
    int len = ftell(file);
    rewind(file);
    //fseek(file, 0, SEEK_SET);
    //3 给ｄａｔａ分配对应大小的空间
    *data = (char*)malloc(len + 1);
    if(!data){
        fclose(file);
        return 0;
    }

    //4 读取文件，将内容存储在data中
    //size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
    fread(*data, 1, len, file);
    (*data)[len] = '\0';  //末尾加上结束标记\0，就相当于data变成了一个字符串，使得可以按普通字符串处理

    fclose(file);
    return len;
}

int file_exists(char *path){
    // 使用access()函数检查文件权限,返回1表示可读，0表示不可读
    if (access(path, R_OK) == 0) {
        return 1;  // 可读
    } else {
        return 0;  // 不可读或无此文件
    }
}



//int main(){
//    char *buf = NULL;
//    int len = read_file("/home/gqx/CLionProjects/webserver/static/index.html", buf);
//    printf("len = %d,\n content: %s\n", len, buf);
//}


//void swap1(int a, int b){
//    int temp = a;
//    a = b;
//    b = temp;
//}

//void swap(int *a, int *b){
//    int temp = *a;
//    *a = *b;
//    *b = temp;
//}
//
//int main(){
//    int a = 1, b = 2;
//    swap(&a, &b);
//    printf("a = %d, b= %d", a, b);
//}





//您的错误写法
//void wrong_func(char *ptr) {
//    ptr = malloc(100);  // 只修改了局部副本
//    strcpy(ptr, "Hello");
//}
//
//正确写法
//void correct_func(char **ptr) {
//    *ptr = malloc(100);  // 修改了调用者的指针
//    strcpy(*ptr, "World");
//}
//
//int main() {
//    char *p1 = NULL;
//    char *p2 = NULL;
//    wrong_func(p1);
//    printf("p1 = %s\n", p1);  // 输出: (null) 或 崩溃！
//
//    correct_func(&p2);
//    printf("p2 = %s\n", p2);  // 输出: World
//    free(p2);
//    return 0;
//}
