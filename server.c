#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>

#define PORT 8080
#define BACKLOG 10

int main() {
    int sockfd, new_sockfd;
    struct sockaddr_in my_addr;
    struct sockaddr_in their_addr;
    int sin_size;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    my_addr.sin_family = AF_INET;
    my_addr.sin_port = htons(PORT);
    my_addr.sin_addr.s_addr = INADDR_ANY;
    memset(&(my_addr.sin_zero), '\0', 8);

    bind(sockfd, (struct sockaddr*) &my_addr, sizeof(my_addr));
    listen(sockfd, BACKLOG);

    sin_size = sizeof(struct sockaddr_in);

    new_sockfd = accept(sockfd, (struct sockaddr*) &their_addr, &sin_size);
    char buffer[1024] = {};
    ssize_t valread;

    valread = read(new_sockfd, buffer, 1023);
    printf("%s\n", buffer);

    close(new_sockfd);
    close(sockfd);
}
