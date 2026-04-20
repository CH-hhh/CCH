#include <stdio.h>
#include <string.h>

int main() {
    char data[] = "GET / HTTP/1.1\r\nAccept: */*\r\n\r\nbodycontent";
    char *body = strstr(data, "\r\n\r\n");
    if(body) body += 4;
    char *method = strtok(data, " ");
    printf("Method: %s\nBody: %s\n", method, body);
    return 0;
}
