#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <arpa/inet.h>
#include <unistd.h>

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

class ForwardingTable {
    struct TableEntry {
        char VN;
        uint32_t ip_addr;
        uint16_t port;
        uint32_t distance;
        char nextHop; 
    };

    std::vector<TableEntry> table;
    std::vector<int> existingNodes;

  public:
    void clear() {
        table.clear();
        existingNodes.resize(26, 0);
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
        printf("%s", buf);
    }

    ForwardingTable forwardingTable; // MAIN FORWARDING TABLE OF THE NODE
    forwardingTable.clear();
    
    uint8_t n;
    memcpy(&n,buf,1);

    for(uint8_t i = 0;i < n; i++) {
        // parse_linkstate(buf + 1 + i * 11);
        forwardingTable.insertLSM(buf + 1 + i * 11);
    }

    forwardingTable.displayTable();
    

    close(virtualnodefd);
}
