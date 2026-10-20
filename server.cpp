#include <iostream>
#include <fstream>
#include <unistd.h>
#include <string.h>
#include <vector>
#include <cstring>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <ctime>
#define PORT 5000
#define BACKLOG 10

std::vector<int> parseLine(std::string& line) {
    std::string num = "";
    std::vector<int> v;

    for (auto c : line) {
        if (c == '#') break;  
        if (c != ' ' && c != '\t') {
            num += c;
        } else {
            if (num.length() > 0) {
                v.push_back(atoi(num.c_str()));
                num = "";
            }
        }
    }

    if (num.length() > 0) {
        v.push_back(atoi(num.c_str()));
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

bool loadConfig(std::string path,std::vector<std::vector<int>>& Adj) {
    std::ifstream file(path);
    if (!file.is_open()) {
        perror("config file");
        return false;
    }

    std::vector<std::vector<int>> rows;
    std::string line;

    while (std::getline(file, line)) {
        std::vector<int> v = parseLine(line);
        if (!v.empty())rows.push_back(v);
    }
    if (rows.empty()) return false;

    int newN = rows[0].size() + 1;
    if (newN < 2 || newN > 26 ||
        rows.size() != static_cast<size_t>(newN - 1)) {
        std::cerr << "Invalid configuration\n";
        return false;
    }

    for (int i = 0; i < newN - 1; i++) {
        if (rows[i].size() !=
            static_cast<size_t>(newN - i - 1)) {
            std::cerr << "Invalid row length for node " << char('A' + i) << '\n';
            return false;
        }
    }
    Adj.assign(newN, std::vector<int>(newN, -1));
    for (int i = 0; i < newN; i++) {
        Adj[i][i] = 0;
    }
    for (int i = 0; i < newN - 1; i++) {
        for (int j = i + 1; j < newN; j++) {
            int cost = rows[i][j - i - 1];
            if (cost == 0 || cost < -1) {
                std::cerr << "Invalid link cost\n";
                return false;
            }
            Adj[i][j] = cost;
            Adj[j][i] = cost;
        }
    }

    return true;
}

bool sendLinkState(int fd,int node,const std::vector<std::vector<int>>& adj,const uint32_t ipvn[],const uint16_t port_no[]) {
    int N = adj.size();
    char buf[3 + 26 * 11];

    buf[1] = static_cast<char>(N);

    size_t off = 3;
    uint8_t count = 0;

    for (int j = 0; j < N; j++) {
        if (adj[node][j] < 0) continue;
        linkstate(buf + off,ipvn[j],port_no[j],adj[node][j],static_cast<uint8_t>(j));
        off += 11;
        count++;
    }

    buf[2] = static_cast<char>(count);
    ssize_t sent = send(fd, buf, off, 0);
    if (sent < 0) {
        perror("send LINK-STATE");
        return false;
    }
    return true;
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
            if(v.empty())continue;
            if (n == 0) {
                N = v.size() + 1;
                adj.resize(N);
                adj[n].resize(N);
                fillAdj(adj, v, n, N);
                n++;
                continue;
            }
            if (v.size() != N - n - 1) {
                std::cerr << "Invalid row length for node " << char('A' + n) << std::endl;
                return 1;
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



    // Accept N connections and their corresponding messages
    int sockfd, client_sockfd[N];
    struct sockaddr_in my_addr;
    struct sockaddr_in their_addr[N];
    socklen_t sin_size;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return 1;
    }

    my_addr.sin_family = AF_INET;
    my_addr.sin_port = htons(PORT);
    my_addr.sin_addr.s_addr = INADDR_ANY;
    memset(&(my_addr.sin_zero), '\0', 8);

    if (bind(sockfd, (struct sockaddr*) &my_addr, sizeof(my_addr)) < 0) {
        perror("bind");
        close(sockfd);
        return 1;
    }
    if (listen(sockfd, BACKLOG) < 0) {
        perror("listen");
        close(sockfd);
        return 1;
    }

    sin_size = sizeof(struct sockaddr_in);

    uint64_t connect_msgs[N];
    uint16_t port_no[N];
    uint32_t ipvn[N];

    for (int i=0; i<N; i++) {
        client_sockfd[i] = accept(sockfd, (struct sockaddr*) &their_addr[i], &sin_size);
        if (client_sockfd[i] < 0) {
            perror("accept");
            return 1;
        }
        ssize_t valread;
        valread = read(client_sockfd[i], &connect_msgs[i], sizeof(connect_msgs[i]));
        auto p = parse_ip_port(connect_msgs[i]);
        port_no[i] = p.first;
        ipvn[i] = p.second;
        std::cout << "Client " << (char) ('A' + i) << ": " <<ipvn[i]<<" "<<port_no[i]<< "\n";
    }
    
    for(int i=0;i<N;i++){
        if (!sendLinkState(client_sockfd[i], i, adj, ipvn, port_no)) {
            return 1;
        }
    }

    struct stat lastStat{};

    if (stat(configFilePath.c_str(), &lastStat) < 0) {
        perror("stat");
        return 1;
    }

    while (true) {
        sleep(1);
        struct stat currentStat{};
        if (stat(configFilePath.c_str(), &currentStat) < 0) {
            perror("stat");
            continue;
        }
        if (currentStat.st_mtime == lastStat.st_mtime) continue;
        std::vector<std::vector<int>> newAdj;
        if (!loadConfig(configFilePath, newAdj))continue;
        if (newAdj.size() != static_cast<size_t>(N)) continue;
        bool f = true;
        for (int i = 0; i < N; i++) {
            if (!sendLinkState(client_sockfd[i], i, newAdj, ipvn, port_no)) {
                f = false;
                break;
            }
        }
        if (f) {
            adj = std::move(newAdj);
            lastStat = currentStat;
        }
    }
    for (int i=0; i<N; i++) close(client_sockfd[i]);
    close(sockfd);
}