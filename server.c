
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>

#define PORT 5000
#define BACKLOG 10
#define MAX_N 26
#define ENTRY_SIZE 11
#define HEADER_SIZE 3
#define MAX_BUF (HEADER_SIZE + MAX_N * ENTRY_SIZE)

int parseLine(char *line, int *v, int maxCount) {
    int count = 0;
    char *p = line;

    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
        if (*p == '\0' || *p == '#') break;
        char *end;
        long num = strtol(p, &end, 10);
        if (end == p) break;
        if (count >= maxCount) return -1;
        v[count++] = (int)num;
        p = end;
    }

    return count;
}

int loadConfig(const char *path, int Adj[MAX_N][MAX_N], int *N) {
    FILE *file = fopen(path, "r");
    if (!file) {
        perror("config file");
        return 0;
    }

    int rows[MAX_N - 1][MAX_N - 1];
    int rowLengths[MAX_N - 1];
    int rowCount = 0;
    char line[4096];

    while (fgets(line, sizeof(line), file)) {
        int values[MAX_N];
        int count = parseLine(line, values, MAX_N);

        if (count < 0) {
            fprintf(stderr, "Too many values in configuration row\n");
            fclose(file);
            return 0;
        }

        if (count == 0) continue;

        if (rowCount >= MAX_N - 1) {
            fprintf(stderr, "Too many configuration rows\n");
            fclose(file);
            return 0;
        }

        for (int j = 0; j < count; j++) rows[rowCount][j] = values[j];
        rowLengths[rowCount] = count;
        rowCount++;
    }

    fclose(file);

    if (rowCount == 0) {
        fprintf(stderr, "Empty configuration\n");
        return 0;
    }

    int newN = rowLengths[0] + 1;

    if (newN < 2 || newN > MAX_N || rowCount != newN - 1) {
        fprintf(stderr, "Invalid configuration\n");
        return 0;
    }

    for (int i = 0; i < newN - 1; i++) {
        if (rowLengths[i] != newN - i - 1) {
            fprintf(stderr, "Invalid row length for node %c\n", 'A' + i);
            return 0;
        }
    }

    for (int i = 0; i < newN; i++) {
        for (int j = 0; j < newN; j++) Adj[i][j] = -1;
        Adj[i][i] = 0;
    }

    for (int i = 0; i < newN - 1; i++) {
        for (int j = i + 1; j < newN; j++) {
            int cost = rows[i][j - i - 1];
            if (cost == 0 || cost < -1) {
                fprintf(stderr, "Invalid link cost\n");
                return 0;
            }
            Adj[i][j] = cost;
            Adj[j][i] = cost;
        }
    }

    *N = newN;
    return 1;
}

void parse_ip_port(uint64_t msg, uint16_t *port, uint32_t *ip) {
    *port = (uint16_t)(msg & 0xFFFF);
    *ip = (uint32_t)(msg >> 16);
}

void linkstate(char *buf, uint32_t ip, uint16_t port, uint32_t weight, uint8_t node) {
    memcpy(buf, &node, 1);
    ip = htonl(ip);
    memcpy(buf + 1, &ip, 4);
    port = htons(port);
    memcpy(buf + 5, &port, 2);
    weight = htonl(weight);
    memcpy(buf + 7, &weight, 4);
}

int sendLinkState(int fd, int node, int Adj[MAX_N][MAX_N], int N, const uint32_t ipvn[], const uint16_t port_no[]) {
    char buf[MAX_BUF];
    buf[0] = 0;
    buf[1] = (char)N;
    buf[2] = 0;
    size_t off = HEADER_SIZE;
    uint8_t count = 0;

    for (int j = 0; j < N; j++) {
        if (Adj[node][j] < 0) continue;
        linkstate(buf + off, ipvn[j], port_no[j], (uint32_t)Adj[node][j], (uint8_t)j);
        off += ENTRY_SIZE;
        count++;
    }

    buf[2] = (char)count;
    ssize_t sent = send(fd, buf, off, 0);
    if (sent < 0) {
        perror("send LINK-STATE");
        return 0;
    }

    return 1;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <config-file>.txt\n", argv[0]);
        return 1;
    }

    const char *configFilePath = argv[1];
    int Adj[MAX_N][MAX_N];
    int N;

    if (!loadConfig(configFilePath, Adj, &N)) return 1;

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in my_addr;
    memset(&my_addr, 0, sizeof(my_addr));
    my_addr.sin_family = AF_INET;
    my_addr.sin_port = htons(PORT);
    my_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sockfd, (struct sockaddr *)&my_addr, sizeof(my_addr)) < 0) {
        perror("bind");
        close(sockfd);
        return 1;
    }

    if (listen(sockfd, BACKLOG) < 0) {
        perror("listen");
        close(sockfd);
        return 1;
    }

    int client_sockfd[MAX_N];
    struct sockaddr_in their_addr[MAX_N];
    uint16_t port_no[MAX_N];
    uint32_t ipvn[MAX_N];

    for (int i = 0; i < N; i++) {
        socklen_t sin_size = sizeof(their_addr[i]);
        client_sockfd[i] = accept(sockfd, (struct sockaddr *)&their_addr[i], &sin_size);

        if (client_sockfd[i] < 0) {
            perror("accept");
            return 1;
        }

        uint64_t connect_msg;
        ssize_t valread = read(client_sockfd[i], &connect_msg, sizeof(connect_msg));
        if (valread != sizeof(connect_msg)) {
            perror("read CONNECT");
            return 1;
        }

        parse_ip_port(connect_msg, &port_no[i], &ipvn[i]);
        printf("Client %c: %u %u\n", 'A' + i, (unsigned int)ipvn[i], (unsigned int)port_no[i]);
    }

    for (int i = 0; i < N; i++) {
        if (!sendLinkState(client_sockfd[i], i, Adj, N, ipvn, port_no)) return 1;
    }

    struct stat lastStat;
    if (stat(configFilePath, &lastStat) < 0) {
        perror("stat");
        return 1;
    }

    while (1) {
        sleep(1);
        struct stat currentStat;

        if (stat(configFilePath, &currentStat) < 0) {
            perror("stat");
            continue;
        }

        if (currentStat.st_mtime == lastStat.st_mtime) continue;

        int newAdj[MAX_N][MAX_N];
        int newN;

        if (!loadConfig(configFilePath, newAdj, &newN)) continue;
        if (newN != N) continue;

        int success = 1;

        for (int i = 0; i < N; i++) {
            if (!sendLinkState(client_sockfd[i], i, newAdj, N, ipvn, port_no)) {
                success = 0;
                break;
            }
        }

        if (success) {
            memcpy(Adj, newAdj, sizeof(Adj));
            lastStat = currentStat;
            printf("Topology updated and broadcast successfully\n");
        }
    }

    for (int i = 0; i < N; i++) close(client_sockfd[i]);
    close(sockfd);
    return 0;
}
