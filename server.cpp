#include <iostream>
#include <fstream>
#include <unistd.h>
#include <string.h>
#include <vector>
#include <arpa/inet.h>

#define PORT 5000
#define BACKLOG 10

std::vector<int> parseLine(std::string& line) {
    std::string num = "";
    std::vector<int> v;

    for (auto c : line) {
        if (c != ' ') num += c;
        else {
            if (num.length() > 0) {
                v.push_back(atoi(num.c_str()));
                num = "";
            }
        }
    }
    if (num.length() > 0) {
        v.push_back(atoi(num.c_str()));
        num = "";
    }

    return v;
}

void fillAdj(std::vector<std::vector<int>> &adj, std::vector<int> &v, int n, int N) {
    for (int i=0; i<=n; i++) adj[n][i] = 0;
    for (int i=n+1; i<N; i++) adj[n][i] = v[i-n-1];
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <config-file>.txt\n";
        return 1;
    }

    std::string configFilePath = argv[1];
    std::ifstream configFile(configFilePath);
    if (!configFile.is_open()) {
        std::cout << "Could not open " << configFilePath << "\n";
        return 1;
    }

    // Parsing config file
    std::string line;
    int n = 0, N;
    std::vector<std::vector<int>> adj;
    while (std::getline(configFile, line)) {
        if (line.length() > 1 && line[0] != '#') {
            std::vector<int> v = parseLine(line);
            if (n == 0) {
                N = v.size() + 1;
                adj.resize(N);
                adj[n].resize(N);
                fillAdj(adj, v, n, N);
                n++;
            }
            adj[n].resize(N);
            fillAdj(adj, v, n, N);
            n++;
        }
    }

    for (auto r : adj) {
        for (auto e : r) std::cout << e << " ";
        std::cout << "\n";
    }



    // Accept N connections and their corresponding messages
    int sockfd, client_sockfd[N];
    struct sockaddr_in my_addr;
    struct sockaddr_in their_addr[N];
    socklen_t sin_size;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    my_addr.sin_family = AF_INET;
    my_addr.sin_port = htons(PORT);
    my_addr.sin_addr.s_addr = INADDR_ANY;
    memset(&(my_addr.sin_zero), '\0', 8);

    bind(sockfd, (struct sockaddr*) &my_addr, sizeof(my_addr));
    listen(sockfd, BACKLOG);

    sin_size = sizeof(struct sockaddr_in);

    char connect_msgs[N][1024];

    for (int i=0; i<N; i++) {
        client_sockfd[i] = accept(sockfd, (struct sockaddr*) &their_addr[i], &sin_size);

        ssize_t valread;
        valread = read(client_sockfd[i], connect_msgs[i], 1023);

        std::cout << "Client " << (char) ('A' + i) << ": " << connect_msgs[i] << "\n";
    }

    std::cout << "All connections done!\n";

    for (int i=0; i<N; i++) {
        std::string msg = "Hey! You are ";
        msg += 'A' + i;

        send(client_sockfd[i], msg.c_str(), msg.size(), 0);
    }

    std::cout << "Messages sent back!\n";

    for (int i=0; i<N; i++) close(client_sockfd[i]);
    close(sockfd);
}
