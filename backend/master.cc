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

using namespace std;

const string IP_ADDR = "127.0.0.1";
// TODO: move all to a config file
const string BAD_SYNTAX_ERR = "400 Bad Syntax\r\n";
const string GET_SERVER_SUCCESS = "207 Get the server address for key:";


struct Server {
    string ip;
    string port;
    string key; // ending row for the tablet on this server
    string listenPort = "";  // the port number that this backend server is listening on
    bool isAwake = false; // whether the server is available or not
    long lastActiveSeqNum = 0; // the sequence number of last heart beat
    int fd = -1; // socket file descriptor
    bool isPrim = false;    // whether it is the primary backend server
    vector<int> frontendFds;    // the file descriptors of frontend using this server
    Server(string ip, string port, string key): ip(ip), port(port), key(key) {};
    Server() {};
};

vector<int> frontendSockets;
vector<int> backendSockets;
vector<pthread_t> frontendThreads;
vector<pthread_t> backendThreads;
unordered_map<string, Server> backendServers;  // key: ip_address
bool debug = true;
pthread_mutex_t primMutex;

void generateServers();
Server* getServerByKey(string key, map<string, vector<Server*>> &dictionary);
void* checkWorker(void* arg);
void* backendWorker(void *arg);
void* frontendWorker(void *arg);
bool isBackendServer(sockaddr_in clientAddr);
void broadcastPrim(string key, string ip, string port, map<string, vector<Server*>> &dictionary);
void broadcastServerDown(string key, string ip, string port, map<string, vector<Server*>> &dictionary);
char* removeCommandFromBuffer(char *buffer, char *next);
void clearArray(char* arr);
void addFdToServers(int comm_fd, string &key, map<string, vector<Server*>> &dictionary);
void readPort(int fd, Server* server);

map<string, vector<Server*>> generateDictionary();

int main(int argc, char *argv[]) {
    int port = 5000;
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
//    inet_pton(AF_INET, (char*)IP_ADDR.c_str(), &servaddr.sin_addr);
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

    // thread to check whether each server is alive or not
    pthread_t checkThread;
    pthread_create(&checkThread, NULL, &checkWorker, NULL);

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

        if (isBackendServer(clientAddr)) {
            backendSockets.emplace_back(comm_fd);
            string clientIp = inet_ntoa(clientAddr.sin_addr);
            string clientPort = to_string(ntohs(clientAddr.sin_port));
            string key = clientIp + ":" + clientPort;
            backendServers[key].fd = comm_fd;

            pthread_t backendThread;
            backendThreads.emplace_back(backendThread);
            pthread_create(&backendThread, NULL, &backendWorker, &key);
        } else {
            frontendSockets.emplace_back(comm_fd);
            pthread_t frontendThread;
            frontendThreads.emplace_back(frontendThread);
            pthread_create(&frontendThread, NULL, &frontendWorker, &comm_fd);
        }
    }
    return 0;
}

void generateServers() {
    Server s0("127.0.0.1", "8000", "z");
    Server s1("127.0.0.1", "8001", "z");
    Server s2("127.0.0.1", "8002", "z");
    backendServers.insert({"127.0.0.1:8000", s0});
    backendServers.insert({"127.0.0.1:8001", s1});
    backendServers.insert({"127.0.0.1:8002", s2});

    vector<Server*> group0;
    vector<Server*> group1;
    group0.emplace_back(&s0);
    group1.emplace_back(&s1);
    group1.emplace_back(&s2);

}

// check if each server is still alive and inform the change of prim.
void* checkWorker(void* arg) {
    unordered_map<string, long> activeSeqNum;   // key: ip_addr, value: lastActiveSeqNum
    map<string, vector<Server*>> dictionary = generateDictionary();
    // initialize
    for (auto &s : backendServers) {
        activeSeqNum.insert({s.first, -1});
    }

    while (true) {
        for (auto &s : backendServers) {
            if (s.second.isAwake) {
                long curr = s.second.lastActiveSeqNum;
                if (curr > activeSeqNum[s.first]) {    // if active, update activeSeqNum
                    activeSeqNum[s.first] = curr;
                } else {    // the server is down
                    cout << "server " << s.first << " is down\n";
                    pthread_mutex_lock(&primMutex);
                    s.second.isAwake = false;

                    activeSeqNum[s.first] = -1;
                    s.second.lastActiveSeqNum = -1;

                    broadcastServerDown(s.second.key, s.second.ip, s.second.listenPort, dictionary);

                    if (s.second.isPrim) {  // if it is prim, change the prim
                        string key = s.second.key;
                        for (auto &i : dictionary[key]) {
                            if (i->isAwake && !i->isPrim) {
                                s.second.isPrim = false;
                                i->isPrim = true;   // switch prim to i
                                broadcastPrim(key, i->ip, i->listenPort, dictionary);

                                // inform all sockets in FrontFds about the Prim change
                                string msg = "SERVER " + i->ip + ":" + i->listenPort + "\r\n";
                                for (int f : i->frontendFds) {
                                    write(f, (char*)msg.c_str(), msg.size());
                                    if (debug) {
                                        fprintf(stderr, "[%d]: %s", f, msg.c_str());
                                    }
                                }
                                break;
                            }
                        }
                    }
                    pthread_mutex_unlock(&primMutex);
                }
            }
        }
        sleep(5);
    }
    pthread_exit(0);
}

void* backendWorker(void *arg) {
    string key = *(string *) arg;
    Server* server = &backendServers[key];
    map<string, vector<Server*>> dictionary = generateDictionary();
    int comm_fd = server->fd;
//    readPort(comm_fd, server);

    char buffer[1000] = {'\0'};
    char* curr = buffer;

    // READ POST
    while(true) {
        read(comm_fd, curr, 1000);
        char command[512] = {'\0'};
        char* left = buffer;
        char* right;

        if (strstr(left, "\r\n") != NULL) {
            int i = 0;
            right = strstr(left, "\r\n");
            while (left[i] != ' ' && left[i] != '\r') {
                command[i] = left[i];
                i++;
            }
            command[i] = '\0';
            if (debug) {
                fprintf(stderr, "[%d] Received: %.*s\n", comm_fd, (int) (right - left), left);
            }
            left += i + 1;  // move to the start of message

            if (strcmp(command, "PORT") == 0) {
                if (right - left < 1) { // query not provided
                    write(comm_fd, (char*)BAD_SYNTAX_ERR.c_str(), BAD_SYNTAX_ERR.size());
                } else {
                    char portBuff[1000] = {'\0'};
                    strncpy(portBuff, left, right - left);
                    string portNum(portBuff);
                    server->listenPort = portNum;
                }
            } else {
                write(comm_fd, (char*)BAD_SYNTAX_ERR.c_str(), BAD_SYNTAX_ERR.size());
            }

            clearArray(command);
            left = right + 2;   // move to the character after '\n'
        }
        // clear command from buffer, move the remaining chars to the start of the buffer and set curr to the end of the buffer.
        curr = removeCommandFromBuffer(buffer, left);
        break;
    }

    //    inform about the prim
    pthread_mutex_lock(&primMutex);
    bool hasPrim = false;
    for (auto s : dictionary[server->key]) {
        if (s->isPrim && s->isAwake) {    // find the prim for this group
            string primMessage = "PRIM " + s->ip + ":" + s->listenPort + "\r\n";
            write(comm_fd, (char*)primMessage.c_str(), primMessage.size());
            hasPrim = true;
            if (debug) {
                fprintf(stderr, "Send to [%s:%s]: %s", server->ip.c_str(), server->port.c_str(), primMessage.c_str());
            }
            break;
        }
    }

    // if none of the servers are prim in this group, set itself as prim.
    if (!hasPrim) {
        server->isPrim = true;
        string primMessage = "PRIM " + server->ip + ":" + server->listenPort + "\r\n";
        write(comm_fd, (char*)primMessage.c_str(), primMessage.size());
        if (debug) {
            fprintf(stderr, "Send to [%s:%s]: %s", server->ip.c_str(), server->port.c_str(), primMessage.c_str());
        }
    }
    server->isAwake = true;
    pthread_mutex_unlock(&primMutex);


    // listen to HEARTBEAT
    while(true) {
        read(comm_fd, curr, 1000);
        char command[512] = {'\0'};
        char* left = buffer;
        char* right;

        while (strstr(left, "\r\n") != NULL) {
            int i = 0;
            right = strstr(left, "\r\n");
            while (left[i] != ' ' && left[i] != '\r') {
                command[i] = left[i];
                i++;
            }
            command[i] = '\0';
            if (debug) {
                fprintf(stderr, "[%d] Received: %.*s\n", comm_fd, (int) (right - left), left);
            }
            left += i + 1;  // move to the start of message

            if (strcmp(command, "HEART") == 0) {
                if (right - left < 1) { // query not provided
                    write(comm_fd, (char*)BAD_SYNTAX_ERR.c_str(), BAD_SYNTAX_ERR.size());
                } else {
                    char seqBuff[1000] = {'\0'};
                    strncpy(seqBuff, left, right - left);
                    string seqNum(seqBuff);
                    server->isAwake = true;
                    if (server->lastActiveSeqNum < stol(seqNum)) {
                        server->lastActiveSeqNum = stol(seqNum);
                    }
                }
            } else {
                write(comm_fd, (char*)BAD_SYNTAX_ERR.c_str(), BAD_SYNTAX_ERR.size());
            }

            clearArray(command);
            left = right + 2;   // move to the character after '\n'
        }
        // clear command from buffer, move the remaining chars to the start of the buffer and set curr to the end of the buffer.
        curr = removeCommandFromBuffer(buffer, left);
    }
    pthread_exit(0);
}


void* frontendWorker(void *arg) {
    int comm_fd = *(int *) arg;
    map<string, vector<Server*>> dictionary = generateDictionary();
    char buffer[1000] = {'\0'};
    char* curr = buffer;


    while(true) {
        read(comm_fd, curr, 1000);
        char command[512] = {'\0'};
        char* left = buffer;
        char* right;

        while (strstr(left, "\r\n") != NULL) {
            int i = 0;
            right = strstr(left, "\r\n");
            while (left[i] != ' ' && left[i] != '\r') {
                command[i] = left[i];
                i++;
            }
            command[i] = '\0';
            if (debug) {
                fprintf(stderr, "[%d] Received: %.*s\n", comm_fd, (int) (right - left), left);
            }
            left += i + 1;  // move to the start of message

            if (strcmp(command, "QUERY") == 0) {
                if (right - left < 1) { // query not provided
                    write(comm_fd, (char*)BAD_SYNTAX_ERR.c_str(), BAD_SYNTAX_ERR.size());
                } else {
                    char keyBuff[1000] = {'\0'};
                    strncpy(keyBuff, left, right - left);
                    string key(keyBuff);
                    Server* s = getServerByKey(key, dictionary);
                    // message format: "SERVER XXX.X.X.X:XXXX\r\n"
                    string extend_msg = "SERVER " + s->ip + ":" + s->listenPort + "\r\n";
                    write(comm_fd, (char*)extend_msg.c_str(), extend_msg.size());

                    // add fd to the fd list of the servers in this group
                    addFdToServers(comm_fd, key, dictionary);
                }
            } else {
                write(comm_fd, (char*)BAD_SYNTAX_ERR.c_str(), BAD_SYNTAX_ERR.size());
            }

            clearArray(command);
            left = right + 2;   // move to the character after '\n'
        }
        // clear command from buffer, move the remaining chars to the start of the buffer and set curr to the end of the buffer.
        curr = removeCommandFromBuffer(buffer, left);
    }
    pthread_exit(0);
}

void readPort(int fd, Server* server) {
    char buffer[1000] = {'\0'};
    char* curr = buffer;

    char prevChar = '\0';
    char curChar = '\0';
    while (!(prevChar == '\r' && curChar == '\n')) {
        read(fd, curr, 1);
        prevChar = curChar;
        curChar = *curr;
        curr += 1;
    }
    fprintf(stderr, "Command received: %s", &buffer[0]);
    char* command = strtok(&buffer[0], " ");
    char* listenPort = strtok(NULL, " ");
    if (strcmp(command, "PORT") == 0) {
        string tmp(listenPort);
        server->listenPort = tmp;
        cout << "get initial server: " << server->listenPort << endl;
    }
}

void broadcastPrim(string key, string ip, string port, map<string, vector<Server*>> &dictionary) {
    string primMessage = "PRIM " + ip + ":" + port + "\r\n";
    for (auto s : dictionary[key]) {
        if (s->isAwake) {
            write(s->fd, (char*)primMessage.c_str(), primMessage.size());
            if (debug) {
                fprintf(stderr, "Broad to [%s:%s]: %s", s->ip.c_str(), s->port.c_str(), primMessage.c_str());
            }
        }
    }
}

void broadcastServerDown(string key, string ip, string port, map<string, vector<Server*>> &dictionary) {
    string primMessage = "DOWN " + ip + ":" + port + "\r\n";
    cout << "sending " << primMessage << endl;
    for (auto s : dictionary[key]) {
        if (s->isAwake) {
            write(s->fd, (char*)primMessage.c_str(), primMessage.size());
            if (debug) {
                fprintf(stderr, "Broad to [%s:%s]: %s", s->ip.c_str(), s->port.c_str(), primMessage.c_str());
            }
        }
    }
}


bool isBackendServer(sockaddr_in clientAddr) {
    string clientIp = inet_ntoa(clientAddr.sin_addr);
    string clientPort = to_string(ntohs(clientAddr.sin_port));
    fprintf(stderr, "IP address %s:%s\n", clientIp.c_str(), clientPort.c_str());
    for (auto s: backendServers) {
        string ip = s.second.ip;
        string port = s.second.port;
        if (clientIp == ip && clientPort == port) {
            return true;
        }
    }
    return false;
}

Server* getServerByKey(string key, map<string, vector<Server*>> &dictionary) {
    int i = 0;
    for (auto it = dictionary.begin(); it != dictionary.end(); it++) {
        if (it->first.compare(key) >= 0 || i == dictionary.size() - 1) {  // search for the group the key falls in
            for (Server* s : it->second) {  // return the PRIM server in this group
                if (s->isAwake && s->isPrim) {
                    return s;
                }
            }
            fprintf(stderr, "All servers unavailable for key [%s]", key.c_str());
        }
        i += 1;
    }
    exit(1);
}

void addFdToServers(int comm_fd, string &key, map<string, vector<Server*>> &dictionary) {
    int i = 0;
    for (auto it = dictionary.begin(); it != dictionary.end(); it++) {
        if (it->first.compare(key) >= 0 || i == dictionary.size() - 1) {  // search for the group the key falls in
            for (Server* s : it->second) {  // return the PRIM server in this group
                s->frontendFds.emplace_back(comm_fd);
            }
        }
        i += 1;
    }
}

char* removeCommandFromBuffer(char *buffer, char *next){
    int len_remain = strlen(next);
    char* pt = buffer;
    for (int i = 0; i < len_remain; i++) {
        *pt = *next;
        pt++;
        next++;
    }
    char* curr = pt;    // The end of buffer, which will be the next place to write in

    clearArray(curr);
    return curr;
}

void clearArray(char* arr) {
    char *curr = arr;
    while (*curr != '\0') {
        *curr = '\0';
        curr++;
    }
}

/* used for looking up server
 * tablet is divided in alphanumeric order. The keys of dictionary are the ending row for a certain tablet.
 * For each vector, the first element is the primary backend server, and the others are back-up servers. */
map<string, vector<Server*>> generateDictionary() {
    map<string, vector<Server*>> dictionary;
    for (auto &s : backendServers) {
        string key = s.second.key;
        if (!dictionary.count(key)) {
            vector<Server*> servers;
            servers.emplace_back(&(s.second));
            dictionary.insert({key, servers});
        } else {
            dictionary[key].emplace_back(&(s.second));
        }
    }
    return dictionary;
}
