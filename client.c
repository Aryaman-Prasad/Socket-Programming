#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define IP_ADDR "10.0.0.1"
#define PORT 8000
#define BACKLOG 10

int main() {
    int clientfd, status;
    struct sockaddr_in server_addr;

    clientfd = socket(AF_INET, SOCK_STREAM, 0);

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);

    status = connect(clientfd, (struct sockaddr*) &server_addr, sizeof(server_addr));

    char* msg = "hello";
    send(clientfd, msg, strlen(msg), 0);
    printf("Message sent\n");

    close(clientfd);
}
