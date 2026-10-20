#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <arpa/inet.h>
#include <unistd.h>
#include <queue>

using namespace std;

string formatIp(uint32_t ip) {
    string s = "";

    for (int i=0; i<4; i++) {
        s = to_string(ip & (0xFF)) + s;
        ip >>= 8;
        if (i < 4 - 1) s = '.' + s;
    }

    return s;
}
struct TableEntry {
    char VN;
    uint32_t ip_addr;
    uint16_t port;
    uint32_t distance;
    char nextHop; 
};
class ForwardingTable {
    public:
    

    std::vector<TableEntry> table;
    std::vector<int> existingNodes;

    void clear() {
        table.clear();
        existingNodes.resize(26, 0);
    }
    TableEntry last(){
        if(table.size()==0){
            perror("Table size 0");
            return {};
        }
        return table[table.size()-1];
    }
    int sz(){
        return table.size();
    }
    void insertLSM(char* LSMrow) {
        // 88-bit message, 8 (VN) + 32 (ip) + 16 (port) + 32 (distance)
        TableEntry newEntry = {'\0', 0, 0, 0, '\0'};
        newEntry.VN = LSMrow[0] + 'A';

        uint32_t ip_net, distance_net;
        uint16_t port_net;

        memcpy(&ip_net, LSMrow + 1, sizeof(ip_net));
        memcpy(&port_net, LSMrow + 5, sizeof(port_net));
        memcpy(&distance_net, LSMrow + 7, sizeof(distance_net));

        newEntry.ip_addr = ntohl(ip_net);
        newEntry.port = ntohs(port_net);
        newEntry.distance = ntohl(distance_net);

        newEntry.nextHop = LSMrow[0] + 'A'; // Starting nextHop is the adjacent node itself

        existingNodes[newEntry.VN - 'A'] = 1 + table.size();
        table.push_back(newEntry);
    }

    void displayTable() {
        for (auto row : table) {
            std::cout << row.VN << " " << formatIp(row.ip_addr) << " " << row.port << " " << row.distance << " " << row.nextHop << "\n";
        }
    }
};
vector<pair<int8_t,int32_t>> dijkstra(uint8_t vn,uint8_t N,const vector<vector<uint32_t>>& dist) {
    const uint32_t INF = UINT32_MAX;
    priority_queue<vector<int32_t>,vector<vector<int32_t>>,greater<>> P;

    vector<pair<int8_t,int32_t>> A(N, {-1, -1});
    vector<uint32_t> d(N, INF);

    d[vn] = 0;
    A[vn].first = vn;
    A[vn].second = 0;

    P.push({0, vn});

    while (!P.empty()) {
        auto x = P.top();
        P.pop();
        uint32_t cost = x[0];
        int u = x[1];
        if (cost != d[u]) continue;

        for (int v = 0; v < N; v++) {
            if (dist[u][v] == INF) continue;
            uint32_t w = dist[u][v];
            if (cost > INF - w) continue;
            uint32_t nd = cost + w;
            if (nd < d[v]) {
                d[v] = nd;
                if (u == vn) {
                    A[v].first = v;
                } else {
                    A[v].first = A[u].first;
                }
                A[v].second = (int32_t)nd;
                P.push({(int32_t)nd, v});
            }
        }
    }
    return A;
}
void parse_linkstate(char* buf){
    uint8_t j;
    uint32_t ip, w;
    uint16_t port;

    memcpy(&j, buf, 1);
    memcpy(&ip, buf + 1, 4);
    ip = ntohl(ip);
    memcpy(&port, buf + 5, 2);
    port = ntohs(port);
    memcpy(&w, buf + 7, 4);
    w = ntohl(w);
    std::cout<<(char)('A'+j)<<" "<<formatIp(ip)<<" "<<port<<" "<<w<<endl;
}
uint64_t parse_int(string ip_addr, int port_no){
    uint64_t parse = 0;
    stringstream ss(ip_addr);
    string x;
    while(getline(ss,x,'.')){
        parse = (parse << 8) | stoi(x); 
    }
    parse=(parse<<16) | port_no;
    return parse;
}

int main(int argc,char* argv[]) {
    // taking inputs in Virtual Node
    string dst_ip=argv[1],vn_ip=argv[3];
    int dst_portno=stoi(argv[2]),src_udpport=stoi(argv[4]);

    uint64_t msg1 = parse_int(vn_ip,src_udpport);
    string msg = to_string(msg1);

    int virtualnodefd, status;
    struct sockaddr_in server_addr,udp_addr;

    virtualnodefd = socket(AF_INET, SOCK_STREAM, 0);
    if(virtualnodefd<0){
        perror("socket");
        return 1;
    }

    int udpfd = socket(AF_INET,SOCK_DGRAM,0);
    char VN;
    if(udpfd<0){
        perror("socket");
        return 1;
    }
    
    udp_addr.sin_family = AF_INET;
    udp_addr.sin_port = htons(src_udpport);
    udp_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(udpfd, (struct sockaddr*)&udp_addr,sizeof(udp_addr)) < 0) {
        perror("UDP bind failed");
        close(udpfd);
        return 1;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(dst_portno);
    if (inet_pton(AF_INET, dst_ip.c_str(),&server_addr.sin_addr) <=0 ) {
        cerr << "Invalid destination IP\n";
        close(virtualnodefd);
        return 0;
    }

    status = connect(virtualnodefd, (struct sockaddr*) &server_addr, sizeof(server_addr));
    if(status<0){
        perror("connect_vn");
        close(virtualnodefd);
        return 1;
    }
    
    status = send(virtualnodefd, &msg1, sizeof(msg1), 0);
    if(status<0){
        perror("send_vn");
        close(virtualnodefd);
        return 1;
    }
    printf("Message sent\n");

    char buf[1024];
    status = read(virtualnodefd,buf,1023);
    if (status > 0) {
        buf[status] = '\0';
    }

    ForwardingTable forwardingTable; // MAIN FORWARDING TABLE OF THE NODE
    forwardingTable.clear();

    uint8_t n;
    memcpy(&n,buf+1,1);
    uint8_t N;
    memcpy(&N,buf,1);
    vector<vector<uint32_t>> Adj(N,vector<uint32_t> (N,INT32_MAX));
    vector<uint32_t> ips(N);
    vector<uint16_t> ports(N);
    for(uint8_t i = 0;i < n; i++) {
        // parse_linkstate(buf + 1 + i * 11);
        forwardingTable.insertLSM(buf + 2 + i * 11);
        if(forwardingTable.last().distance==0){
            VN=forwardingTable.last().VN;
        }
    }
    for (int i = 0; i < n; i++) {
        char* entry = buf + 2 + i * 11;

        uint8_t id;
        uint32_t ip, cost;
        uint16_t port;

        memcpy(&id, entry, 1);
        memcpy(&ip, entry + 1, 4);
        memcpy(&port, entry + 5, 2);
        memcpy(&cost, entry + 7, 4);

        ip = ntohl(ip);
        port = ntohs(port);
        cost = ntohl(cost);

        ips[id] = ip;
        ports[id] = port;

        if (id != VN - 'A') {
            Adj[VN - 'A'][id] = cost;
            Adj[id][VN - 'A'] = cost;
        }
    }

    Adj[VN - 'A'][VN - 'A'] = 0;
    vector<bool> A(N);
    A[VN-'A']=1;
    uint8_t visited = 1;
    int fn = forwardingTable.sz();
    for(int i=0;i<N;i++)Adj[i][i]=0;
    uint8_t vn = VN-'A';
    buf[0]= (char) vn;
    for(int i = 0;i < fn;i++){
        if (forwardingTable.table[i].distance == 0) continue;
        struct sockaddr_in dest_addr;
        std::memset(&dest_addr, 0, sizeof(dest_addr));
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(forwardingTable.table[i].port); 
        inet_pton(AF_INET, formatIp(forwardingTable.table[i].ip_addr).c_str(), &dest_addr.sin_addr); 
        sendto(udpfd,buf,status,0,(struct sockaddr*)&dest_addr,sizeof(dest_addr));
    }
    
    char buf1[1024];

    while (visited<N) {
        struct sockaddr_in sender{};
        socklen_t sender_len = sizeof(sender);
        ssize_t n = recvfrom(udpfd,buf1,sizeof(buf1),0,(struct sockaddr*)&sender,&sender_len);
        uint8_t vn1 = static_cast<uint8_t>(buf1[0]);
        uint8_t no_no = static_cast<uint8_t>(buf1[1]);       

        if (A[vn1]) continue;

        A[vn1] = true;
        visited++;
        for(int i = 0;i < fn;i++){
            
            if (forwardingTable.table[i].distance == 0) continue;
            struct sockaddr_in dest_addr;
            std::memset(&dest_addr, 0, sizeof(dest_addr));
            dest_addr.sin_family = AF_INET;
            dest_addr.sin_port = htons(forwardingTable.table[i].port); 
            inet_pton(AF_INET, formatIp(forwardingTable.table[i].ip_addr).c_str(), &dest_addr.sin_addr); 
            sendto(udpfd,buf1,n,0,(struct sockaddr*)&dest_addr,sizeof(dest_addr));
        }
        std::cout << "Received: " << (char)('A'+vn1) << '\n';
        for(int i=0;i<no_no;i++){
            char* entry = buf1 + 2 + i * 11;

            uint8_t id;
            uint32_t ip, cost;
            uint16_t port;

            memcpy(&id, entry, 1);
            memcpy(&ip, entry + 1, 4);
            memcpy(&port, entry + 5, 2);
            memcpy(&cost, entry + 7, 4);

            if (id >= N) continue;

            ips[id] = ntohl(ip);
            ports[id] = ntohs(port);
            cost = ntohl(cost);

            Adj[vn1][id] = cost;
            Adj[id][vn1] = cost;
        }
    }
    forwardingTable.table.clear();
    vector<pair<int8_t,int32_t>> next=dijkstra(VN-'A',N,Adj);
    for(int i=0;i<N;i++){
        if(next[i].second<0)continue;
        TableEntry t;
        t.distance = next[i].second;
        t.nextHop = (char)('A'+next[i].first);
        t.ip_addr = ips[i];
        t.port = ports[i];
        t.VN = ('A'+i);
        forwardingTable.table.push_back(t);
    }
    forwardingTable.displayTable();
    close(virtualnodefd);
    close(udpfd);
}
