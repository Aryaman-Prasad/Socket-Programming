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
#include <poll.h>

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
        existingNodes.assign(26, 0);
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
vector<pair<int8_t,int32_t>> dijkstra(uint8_t vn,uint8_t N,const vector<vector<int32_t>>& dist) {
    const int32_t INF = INT32_MAX;
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

    memcpy(&j, buf+1, 1);
    memcpy(&ip, buf + 2, 4);
    ip = ntohl(ip);
    memcpy(&port, buf + 6, 2);
    port = ntohs(port);
    memcpy(&w, buf + 8, 4);
    w = ntohl(w);
    std::cout<<(char)('A'+j)<<" "<<formatIp(ip)<<" "<<port<<" "<<w<<endl;
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
    if (argc != 5) {
        cerr << "Usage: " << argv[0] << " <ON_IP> <ON_PORT> <VN_IP> <VN_UDP_PORT>\n";
        return 1;
    }
    string dst_ip=argv[1],vn_ip=argv[3];
    int dst_portno=stoi(argv[2]),src_udpport=stoi(argv[4]);
    uint8_t seqno = 0;

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
    char buf1[1024];
    status = read(virtualnodefd,buf,1023);
    ForwardingTable forwardingTable; // MAIN FORWARDING TABLE OF THE NODE
    forwardingTable.clear();

    uint8_t n;
    memcpy(&n,buf+2,1);
    uint8_t N;
    memcpy(&N,buf+1,1);
    vector<vector<int32_t>> Adj(N,vector<int32_t> (N,INT32_MAX));
    vector<uint32_t> ips(N);
    vector<uint16_t> ports(N);
    for(uint8_t i = 0;i < n; i++) {
        // parse_linkstate(buf + 1 + i * 11);
        forwardingTable.insertLSM(buf + 3 + i * 11);
        if(forwardingTable.last().distance==0){
            VN=forwardingTable.last().VN;
        }
    }
    for (int i = 0; i < n; i++) {
        char* entry = buf + 3 + i * 11;

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
    buf[1]= (char) vn;
    buf[0] = (char) seqno;
    seqno++;
    vector<uint16_t> sqns(N);
    for(int i = 0;i < fn;i++){
        if (forwardingTable.table[i].distance == 0) continue;
        struct sockaddr_in dest_addr;
        std::memset(&dest_addr, 0, sizeof(dest_addr));
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(forwardingTable.table[i].port); 
        inet_pton(AF_INET, formatIp(forwardingTable.table[i].ip_addr).c_str(), &dest_addr.sin_addr); 
        sendto(udpfd,buf,3+n*11,0,(struct sockaddr*)&dest_addr,sizeof(dest_addr));
    }
    

    while (visited<N) {

        struct pollfd pfd;
        pfd.fd = udpfd;
        pfd.events = POLLIN;
        pfd.revents = 0;

        int ready = poll(&pfd, 1, 2000);
        if(ready==0)break;
        if(ready<0){
            perror("poll");
            break;
        }
        struct sockaddr_in sender{};
        socklen_t sender_len = sizeof(sender);
        ssize_t n = recvfrom(udpfd,buf1,sizeof(buf1),0,(struct sockaddr*)&sender,&sender_len);
        uint8_t vn1 = static_cast<uint8_t>(buf1[1]);
        uint8_t no_no = static_cast<uint8_t>(buf1[2]);       
        uint16_t sqn = buf1[0];
        
        if (A[vn1] || sqn<sqns[vn1]) continue;

        A[vn1] = true;
        visited++;
        sqns[vn1]=sqn;
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
            char* entry = buf1 + 3 + i * 11;

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
    while (true) {
        struct pollfd pfds[2];

        pfds[0].fd = virtualnodefd;
        pfds[0].events = POLLIN;
        pfds[0].revents = 0;


        pfds[1].fd = udpfd;
        pfds[1].events = POLLIN;
        pfds[1].revents = 0;

        int ready = poll(pfds, 2, 15000);
        if (ready < 0) {
            if (errno == EINTR) continue;
            perror("poll");
            break;
        }
        if (ready == 0) {
            cout<<"sent "<<endl;
            int self = VN - 'A';

            char udpBuf[1024];
            uint8_t count = 0;

            for (int j = 0; j < N; j++) {
                if (Adj[self][j] == INT32_MAX) continue;

                linkstate(udpBuf + 3 + count * 11,ips[j], ports[j],Adj[self][j], (uint8_t)j);
                count++;
            }

            udpBuf[0] = (char)seqno++;
            udpBuf[1] = (char)self;
            udpBuf[2] = (char)count;

            for (int j = 0; j < N; j++) {
                if (j == self || Adj[self][j] == INT32_MAX) continue;

                struct sockaddr_in dest_addr{};
                dest_addr.sin_family = AF_INET;
                dest_addr.sin_port = htons(ports[j]);
                dest_addr.sin_addr.s_addr = htonl(ips[j]);

                sendto(udpfd, udpBuf, 3 + count * 11, 0,(struct sockaddr*)&dest_addr, sizeof(dest_addr));
            }
            continue;
        }

        bool topologyChanged = false;

        if (pfds[0].revents & POLLIN) {
            cout<<"Topology changed from server"<<endl;
            status = read(virtualnodefd, buf, 1023);
            
            if (status <= 0) {
                cerr << "Oracle Node disconnected" << endl;
                close(virtualnodefd);
                close(udpfd);
                return 1;
            }
            uint8_t count;
            memcpy(&N, buf+1, 1);
            memcpy(&count, buf + 2, 1);

            int self = VN - 'A';

            for (int j = 0; j < N; j++) {
                if (j != self) {
                    Adj[self][j] = INT32_MAX;
                    Adj[j][self] = INT32_MAX;
                }
            }

            for (int i = 0; i < count; i++) {
                char* entry = buf + 3 + i * 11;

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

                if (id != self) {
                    Adj[self][id] = cost;
                    Adj[id][self] = cost;
                }
            }

            char udpBuf[1024];

            udpBuf[0] = (char)seqno++;
            udpBuf[1] = (char)self;
            udpBuf[2] = (char)count;
            memcpy(udpBuf + 3, buf + 3, count * 11);

            for (int j = 0; j < N; j++) {
                if (j == self || Adj[self][j] == INT32_MAX)continue;
                struct sockaddr_in dest_addr{};
                dest_addr.sin_family = AF_INET;
                dest_addr.sin_port = htons(ports[j]);
                dest_addr.sin_addr.s_addr = htonl(ips[j]);

                sendto(udpfd, udpBuf, 3 + count * 11, 0,(struct sockaddr*)&dest_addr, sizeof(dest_addr));
            }

            topologyChanged = true;
        }

        if (pfds[1].revents & POLLIN) {
            struct sockaddr_in sender{};
            socklen_t sender_len = sizeof(sender);

            ssize_t received = recvfrom(udpfd, buf1, sizeof(buf1), 0,(struct sockaddr*)&sender, &sender_len);

            uint8_t vn1 = (uint8_t)buf1[1];
            uint8_t no_no = (uint8_t)buf1[2];
            uint16_t sqn = (uint8_t)buf1[0];
            if (sqn <= sqns[vn1])continue;
            
            bool changed = false;
            vector<int32_t> newRow(N, INT32_MAX);
            newRow[vn1] = 0;

            for (int i = 0; i < no_no; i++) {
                char* entry = buf1 + 3 + i * 11;

                uint8_t id;
                uint32_t ip, cost;
                uint16_t port;

                memcpy(&id, entry, 1);
                memcpy(&ip, entry + 1, 4);
                memcpy(&port, entry + 5, 2);
                memcpy(&cost, entry + 7, 4);

                if (id >= N) continue;

                ip = ntohl(ip);
                port = ntohs(port);
                cost = ntohl(cost);

                ips[id] = ip;
                ports[id] = port;

                if (id != vn1) {
                    newRow[id] = (int32_t)cost;
                }
            }

            for (int j = 0; j < N; j++) {
                if (j != vn1 && Adj[vn1][j] != newRow[j]) {
                    changed = true;
                    break;
                }
            }
            if (changed) {
                for (int j = 0; j < N; j++) {
                    if (j == vn1) continue;

                    Adj[vn1][j] = newRow[j];
                    Adj[j][vn1] = newRow[j];
                }

                topologyChanged = true;
            }

            if (changed) {
                cout << "Topology changed by " << (char)('A' + vn1) << endl;
            } else {
                cout << "No topology change from " << (char)('A' + vn1) << endl;
                forwardingTable.displayTable();
            }

        }

        if (topologyChanged) {
            vector<pair<int8_t, int32_t>> next = dijkstra(VN - 'A', N, Adj);

            forwardingTable.table.clear();

            for (int i = 0; i < N; i++) {
                if (next[i].second < 0) continue;

                TableEntry t;
                t.distance = next[i].second;
                t.nextHop = (char)('A' + next[i].first);
                t.ip_addr = ips[i];
                t.port = ports[i];
                t.VN = (char)('A' + i);

                forwardingTable.table.push_back(t);
            }

            forwardingTable.displayTable();
        }
    }
    close(virtualnodefd);
    close(udpfd);
}