#include <iostream>
#include <sstream>
#include <string>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <arpa/inet.h>
#include <unistd.h>

using namespace std;

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
    cout<<(char)('A'+j)<<" "<<ip<<" "<<port<<" "<<w<<endl;
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
    
    uint8_t n;
    memcpy(&n,buf,1);

    for(uint8_t i = 0;i < n; i++) parse_linkstate(buf + 1 + i * 11);
    

    close(virtualnodefd);
}
