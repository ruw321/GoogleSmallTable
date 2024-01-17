#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>
#include <string>
#include <cstring>
#include <vector>
#include <algorithm>
#include <unordered_map>
#include <map>
#include <iostream>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstdlib>
#include <unistd.h>
#include <sstream>

using namespace std;

bool debug = true;

struct FrontendServer {
    string ip;
    string port;
    string listenPort = "";  // the port number that this backend server is listening on
    int numUsers = 0;    // the users connecting to this server
    int fd = -1;
    bool isAwake = true; // whether the server is available or not
    long lastActiveSeqNum = -1; // the sequence number of last heart beat
    FrontendServer(string ip, string port, string listenPort): ip(ip), port(port), listenPort(listenPort) {};
    FrontendServer() {};
};

unordered_map<string, FrontendServer> frontendServers;

bool isFrontendServer(sockaddr_in clientAddr);
void generateServers();
void* frontendWorker(void* arg);
bool is_session_valid(string& req, string& username);
string generate_link(string ip, string port);
string generate_body(string all_component);
string generate_response(string protocol, int response_code, string code_reason, string content_type, string body, string cookie = "") {
    stringstream resp;
    resp << protocol << " " << response_code << " " << code_reason << "\r\n";
    if (!content_type.empty()) {
        resp << "Content-type: " << content_type << "\r\n";
    }
    if (cookie != "") {
        resp << "Set-cookie: sessionid=" << cookie << "\r\n";
    }
    resp << "Content-length: " << body.size() << "\r\n";
    resp << "\r\n";
    resp << body;
    return resp.str();
}


int main(int argc, char *argv[]) {
    int port = 3000;
    int opt;
    while ((opt = getopt(argc, argv, "vp:")) != -1) {
        switch (opt) {
            case 'p':
                port = atoi(optarg);
                if (port == 0) {
                    fprintf(stderr, "Error: Invalid port number\n");
                    exit(1);
                }
                break;
            case 'v':
                // print out debug output
                debug = true;
                break;
            default: /* '?' */
                fprintf(stderr, "Please check the input format.\n");
                exit(1);
        }
    }

    fprintf(stderr, "Server is ready\n");

    // create the dictionary of backend servers
    generateServers();

    struct sockaddr_in my_addr;
    int server_fd;
    // creating the port
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        fprintf(stderr, "Cannot open socket (%s)\n", strerror(errno));
        exit(1);
    }
    // binding the ip address and port to the socket
    int op = 1;
    int res = setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR|SO_REUSEPORT, &op, sizeof(op));
    my_addr.sin_family = AF_INET;
    my_addr.sin_addr.s_addr = INADDR_ANY;
    my_addr.sin_port = htons(port);
    if (bind(server_fd, (struct sockaddr *) &my_addr, sizeof(my_addr)) < 0) {
        fprintf(stderr, "Binding the port to the socket failed: (%s)\n", strerror(errno));
        exit(1);
    }

    // listening
    if (listen(server_fd, SOMAXCONN)) {
        fprintf(stderr, "Listening failed\n");
        exit(1);
    }
    if (debug) {
        fprintf(stderr, "Server waiting for connection at port %d...\n", port);
    }

    while (true) {
        int comm_fd;
        // wait for a connection
        struct sockaddr_in clientAddr;
        socklen_t clientAddrlen = sizeof(clientAddr);
        if ((comm_fd = accept(server_fd, (sockaddr *) &clientAddr, (socklen_t * ) & clientAddrlen)) < 0) {
            fprintf(stderr, "Error with accepting new connections\n");
            exit(1);
        }

        if (debug) {
            fprintf(stderr, "[%d] New connection\n", comm_fd);
        }

        if (isFrontendServer(clientAddr)) {
            string clientIp = inet_ntoa(clientAddr.sin_addr);
            string clientPort = to_string(ntohs(clientAddr.sin_port));
            string key = clientIp + ":" + clientPort;
            frontendServers[key].fd = comm_fd;

            pthread_t frontendThread;
            pthread_create(&frontendThread, NULL, &frontendWorker, &comm_fd);
        } else {
            char buffer[10000] = {0};
            read( comm_fd , buffer, 10000);
            string minUserKey;
            int minUser = 100000;
            for (auto &s : frontendServers) {
                if (s.second.numUsers <= minUser && s.second.isAwake) {
                    minUser = s.second.numUsers;
                    minUserKey = s.first;
                }
            }
            // use the frontend with least users
            string body = generate_link(frontendServers[minUserKey].ip, frontendServers[minUserKey].listenPort);
            body = generate_body(body);
            string res = generate_response("HTTP/1.1", 200, "Success", "text/html", body);
            frontendServers[minUserKey].numUsers += 1;
            write(comm_fd, (char*) res.c_str(), res.size());
        }
    }
    return 0;
}

bool isFrontendServer(sockaddr_in clientAddr) {
    string clientIp = inet_ntoa(clientAddr.sin_addr);
    string clientPort = to_string(ntohs(clientAddr.sin_port));
    fprintf(stderr, "IP address %s:%s\n", clientIp.c_str(), clientPort.c_str());
    for (auto &s: frontendServers) {
        string ip = s.second.ip;
        string port = s.second.port;
        if (clientIp == ip && clientPort == port) {
            return true;
        }
    }
    return false;
}

// check if cookie is applied to this session, and fetch the username from the request
bool is_session_valid(string& req, string& username) {
    size_t start = req.find("Cookie: sessionid=");
    if (start != string::npos) {
        size_t end = req.find("\n", start + 1);
        int len = end - start - 19;
        username = req.substr(start + 18, len);
    }
    return false;
}

void generateServers() {
    FrontendServer s0("127.0.0.1", "7000", "8080");
    FrontendServer s1("127.0.0.1", "7001", "8081");
    frontendServers.insert({"127.0.0.1:7000", s0});
    frontendServers.insert({"127.0.0.1:7001", s1});
}


void* frontendWorker(void* arg) {
    pthread_exit(0);
}

string generate_link(string ip, string port) {
    string res;
    cout << "Assigned to address " << ip << ":" << port << endl;
    res = "<a href='//"+ip+":"+port+"'>Frontend url</a>";
    return res;
}


string generate_body(string all_component) {
    string res;
    res = "<html><body>" + all_component + "</body></html>";
    return res;
}