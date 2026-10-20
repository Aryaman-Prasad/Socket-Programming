#include <iostream>
#include <fstream>
#include <unistd.h>
#include <string.h>
#include <vector>
#include <cstring>
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

void fullFillAdj(std::vector<std::vector<int>> &adj, int N) {
    for (int i=0; i<N-1; i++) {
        for (int j=i+1; j<N; j++) adj[j][i] = adj[i][j];
    }
}

std::pair<uint16_t,uint32_t> parse_ip_port(uint64_t connect_msgs){

    uint16_t port_no=(((0xFFFF)) & (uint64_t)connect_msgs);
    connect_msgs>>=16;
    return {port_no,(uint32_t)connect_msgs};

}

void linkstate(char* buf,uint32_t ip,uint16_t port,uint32_t w,uint8_t j){

    std::memcpy(buf, &j, 1);

    ip = htonl(ip);
    std::memcpy(buf + 1, &ip, 4);

    port = htons(port);
    std::memcpy(buf + 5, &port, 2);

    w = htonl(w);
    std::memcpy(buf + 7, &w, 4);
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
                continue;
            }
            adj[n].resize(N);
            fillAdj(adj, v, n, N);
            n++;
        }
    }
    
    // Extra line added to adj matrix to make it N x N
    adj[n].resize(N);
    std::vector<int> dummy(n, 0);
    fillAdj(adj, dummy, n, N);

    // We want to make adj filled fully (not just upper triangle)
    fullFillAdj(adj, N);

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

    uint64_t connect_msgs[N];
    uint16_t port_no[N];
    uint32_t ipvn[N];

    for (int i=0; i<N; i++) {
        client_sockfd[i] = accept(sockfd, (struct sockaddr*) &their_addr[i], &sin_size);

        ssize_t valread;
        valread = read(client_sockfd[i], &connect_msgs[i], sizeof(connect_msgs));
        auto p = parse_ip_port(connect_msgs[i]);
        port_no[i] = p.first;
        ipvn[i] = p.second;
        std::cout << "Client " << (char) ('A' + i) << ": " <<ipvn[i]<<" "<<port_no[i]<< "\n";
    }
    char buf[N*11+2];
    
    for(int i=0;i<N;i++){
        std::memcpy(buf,&N,1);
        std::string s = "";
        size_t off =2;
        uint8_t c =0;
        for(int j=0;j<N;j++){
            if(adj[i][j]<0) continue;
            linkstate(buf+off,ipvn[j],port_no[j],adj[i][j],j);
            off += 11;
            c++;
        }
        std::memcpy(buf+1, &c, 1);
        send(client_sockfd[i], buf, off,0);
    }



    std::cout << "All connections done!\n";

    // for (int i=0; i<N; i++) {
    //     std::string msg = "Hey! You are ";
    //     msg += 'A' + i;
    //     msg+="\n";
    //     send(client_sockfd[i], msg.c_str(), msg.size(), 0);
    // }

    std::cout << "Messages sent back!\n";

    for (int i=0; i<N; i++) close(client_sockfd[i]);
    close(sockfd);
}