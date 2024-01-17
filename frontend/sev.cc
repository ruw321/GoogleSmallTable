#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <string>
#include <algorithm>
#include <string.h>
#include <iostream>
#include <vector>
#include <sstream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <chrono>

#include <sys/stat.h>
#include <fcntl.h>
#include <iostream>
#include <fstream>
#include <unordered_map>
#include <bits/stdc++.h>
#include <tuple>
#include <algorithm>

using namespace std;

bool debug;
vector<string> loggedInUsers;
//unordered_map<string,string> userContentMap;
int portNo;
int clientNo;
int hostfd;
//string fileContents;
//string getFilename;
string timestamp;
//string allFiles;
// {0: {'fileContents': ...,
//      'getFilename': ...,
//      'allFiles': ...,
//      'username': ...,
//     }
// }
unordered_map<int, unordered_map<string, string>> clientInfo;
unordered_map<string, string> address_and_health_map;

string frontend_servers = "";
string backend_servers = "";


string style = "<style>"
               "ul {"
               "list-style-type: none;"
               "margin: 0;"
               "padding: 0;"
               "overflow: hidden;"
               "background-color: #f3f3f3;"
               "border: 1px solid #e7e7e7;"
               "}"
               "li {float: left}"
               "li a {display: block;"
               "color: #667;"
               "text-align: center;"
               "padding: 14px 16px;"
               "}"
               "</style>";


string navbarBody = "<ul>\n"
                    "<li><a href='/login'> Login </a></li>\n"
                    "<li><a href='/register'> Register </a></li>\n"
                    "<li><a href='/compose'> Compose </a></li>\n"
                    "<li><a href='/inbox'> Inbox </a></li>\n"
                    "<li><a href='/drive'> Drive </a></li>\n"
                    "<li><a href='/changepassword'> Change Password </a></li>\n"
                    "<li><a href='/admin'> Admin </a></li>\n"
                    "</ul>";

string navbar = "\n\n" + style + navbarBody;

static const char* login_page =
        "<form method='post' action='/login'>\n"
        "<p>Username\n"
        "<input type='text' name='username' size='40'></input></p>\n"
        "<p>Password\n"
        "<input type='text' name='password' size='40'></input></p>\n"
        "<input type='submit' value='Login'></input></form>\n";

static const char* register_page =
        "<form method='post' action='/register'>\n"
        "<p>Username\n"
        "<input type='text' name='username' size='40'></input></p>\n"

        "<p>Password\n"
        "<input type='text' name='password1' size='40'></input></p>\n"

        "<p>Confirm Password\n"
        "<input type='text' name='password2' size='40'></input></p>\n"
        "<input type='submit' value='Register'></input></form>\n";

static const char* compose_page =
        "<form method='post' action='/sendmail'>\n"
        "<p>Please specify a recipient\n"
        "<input type='text' name='recipient' size='40'></input></p>\n"
        "<p>Please specify a subject\n"
        "<input type='text' name='subject' size='40'></input></p>\n"
        "<p>Please specify a mail body <textarea type='text' name='mailcontent' size='40' height='200' width='400'></textarea></p>\n"
        "<input type='submit' value='Send'></input></form>\n";

static const char* empty_inbox_page =
        "<table><tr><th>Sender</th><th>Subject</th><th>Time</th></tr></table>\n" ;


string replaceAll(string str, string replaceOrig, string replaceWith);

static const char* changePassword_page =
        "<form method='post' action='/changepassword'>\n"

        "<p>Original Password\n"
        "<input type='text' name='oriPassword' size='40'></input></p>\n"

        "<p>New Password\n"
        "<input type='text' name='newPassword' size='40'></input></p>\n"
        "<input type='submit' value='Confirm'></input></form>\n";

const string MASTER_ADDR = "127.0.0.1:5000";

void debugMsg(string msg, bool debug){
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    char time_str[100];
    strftime(time_str, 100, "%T", gmtime(&ts.tv_sec));

    if (debug){
        printf("%s.%06ld %s\n", time_str, ts.tv_nsec % 1000000, msg.c_str());
    }
}

// split full command by the delimiter
vector<string> split(string fullcommand, string delimiter) {
    vector<string> result;
    while (fullcommand.size()) {
        int index = fullcommand.find(delimiter);
        if (index != string::npos) {
            result.push_back(fullcommand.substr(0, index));
            fullcommand = fullcommand.substr(index+delimiter.size());
        } else {
            result.push_back(fullcommand);
            break;
        }
    }
    return result;
}

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


void handleArguments(int argc, char* argv[], int& portNo, bool& debug){
    int opt;
    string str;
    bool validNumber;

    while ((opt = getopt(argc, argv, ":p:v")) != -1){
        switch(opt){
            case 'p':
                str = optarg;
                validNumber = true;
                // value cannot be 0 or negative
                if (str[0] == '0' || str[0] == '-'){
                    validNumber = false;
                } else {
                    for (int j = 0; j < str.length(); j++){
                        if (isdigit(str[j]) == 0){
                            validNumber = false;
                        }
                    }
                }
                if (validNumber){
                    portNo = atoi(str.c_str());
                } else {
                    cerr << "-p parameter must be positive" << endl;
                    exit(1);
                }
                break;
            case 'v':
                debug = true;
                break;
        }
    }
}

string generate_body(string all_component) {
    string res;
    res = "<html><body>" + all_component + "</body></html>";
    return res;
}


void getFromAndToPaths(string http_request, string& filepath1, string& filepath2){
    cout << "TO AND FROM \n" << endl;
    cout << http_request << endl;
    string splitOnMoveFrom = "movefrom=";
    string movefrom = http_request.substr(http_request.find(splitOnMoveFrom));

    string splitOnMoveTo = "moveto=";
    string moveto = http_request.substr(http_request.find(splitOnMoveTo));

    movefrom = replaceAll(movefrom, "%2F", "\/");
    movefrom = movefrom.substr(splitOnMoveFrom.length());
    movefrom = movefrom.substr(0, movefrom.find("&"));

    moveto = replaceAll(moveto, "%2F", "\/");
    moveto = moveto.substr(splitOnMoveTo.length());
    moveto = moveto.substr(0, moveto.find("\n"));

    cout << "move from: " << movefrom << endl;
    cout << "move to: " << moveto << endl;

    filepath1 = movefrom;
    filepath2 = moveto;
}



void getFileContents(string http_request, string& filepath, string& filename, string& body){

    string split_on_filename = "filename=\"";
    filename = http_request.substr(http_request.find(split_on_filename));
    filename = filename.substr(split_on_filename.length());

    // subtract -2 to not include \n and trailing quote
    filename = filename.substr(0, filename.find('\n') - 2);

    string split_on_boundary = "boundary=";
    string boundary = http_request.substr(http_request.find(split_on_boundary));
    boundary = boundary.substr(split_on_boundary.length());
    boundary = boundary.substr(0, boundary.find('\n') - 1);

    string split_on_content = "Content-Type:";
    body = http_request.substr(http_request.find(split_on_content));
    body = body.substr(body.find(split_on_content, split_on_content.length()));
    body = body.substr(body.find("\n") + 3);

    // subtract two since there are an additional -- at the end of boundary
    body = body.substr(0, body.find(boundary) - 2);

    string split_on_filepath = "filepath";
    int pos = http_request.find(split_on_filepath);

    filepath = http_request.substr(http_request.find(split_on_filepath));

    filepath = filepath.substr(filepath.find("\n") + 3);
    filepath = filepath.substr(0, filepath.find("\n") - 1);
    filename = replaceAll(filename, "%2F", "\/");

    filename = filepath + filename;
}

vector<pair<string, string>> filesStrToVec(string fullcommand, string delimiter) {
    vector<pair<string, string>> result;
    if (fullcommand == "\r\n"){
        return result;
    }
    //remove \r\n
    fullcommand.pop_back();
    fullcommand.pop_back();
    fullcommand += "\n";
    while (fullcommand.size()) {
        int index = fullcommand.find(delimiter);
        if (index != string::npos) {
            pair<string, string> pair = make_pair("00:34:49", fullcommand.substr(0, index));
            result.push_back(pair);
            fullcommand = fullcommand.substr(index+delimiter.size());
        } else {
            break;
        }
    }
    return result;
}

bool newFolderIsValid(string newFolder){
    if (newFolder.find("\/") == string::npos){
        return true;
    }
    newFolder = "root\/" + newFolder;
    string matchingFolder = newFolder.substr(0, newFolder.find_last_of("\/"));
    cout << "matching folder: " << matchingFolder << endl;
    string allFilesStr = clientInfo[clientNo]["allFiles"];
    cout << "allfilesstr: " << allFilesStr << endl;
    vector<pair<string, string>> data = filesStrToVec(allFilesStr, "\n");
    bool isValid = false;
    for (pair<string, string> fileInfo: data){
        if (fileInfo.second[0] == 'D'){// directory
            string existingFolder = fileInfo.second.substr(2);
            cout << "existing folder: |" << existingFolder << "|" << endl;
            cout << "matching folder: |" << matchingFolder << "|" << endl;
            bool check = existingFolder == matchingFolder;
            cout << check << endl;
            if (existingFolder == matchingFolder){
                isValid = true;
                break;
            }
        }
    }
    return isValid;
}

string replaceAll(string str, string replaceOrig, string replaceWith){
    string result = "";
    while (str.size()){
        int index = str.find(replaceOrig);
        if (index != string::npos){
            result += str.substr(0, index) + replaceWith;
            str = str.substr(index+replaceOrig.size());
        } else {
            result += str;
            break;
        }
    }

    return result;
}

string drivePage(vector<pair<string, string>> data, int& backend_fd){

    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    char time_str[100];

    string driveBody = ""
                       "<head>"
                       "<link href='https://fonts.googleapis.com/icon?family=Material+Icons' rel='stylesheet'>"
                       "</head>"
                       "<h1> Drive</h1>"
                       "<h3> Upload File </h3>"
                       "<form action='/drive/file' method='POST' enctype='multipart/form-data'>"
                       "<input type='file' name='filename'>"
                       "<input type='text' name='filepath'>"
                       "<input type='submit'>"
                       "</form>"
                       "<h3> Create Folder </h3>"
                       "<form action='/drive/dir' method='POST' accept-charset='utf-8'>"
                       "<input type='text' name='filename'>"
                       "<input type='submit'>"
                       "</form>"

                       "<h3> Move and Rename File </h3>"
                       "<form action='/drive/move' method='POST'>"
                       "<input type='text' name='movefrom'>"
                       "<input type='text' name='moveto'>"
                       "<input type='submit'>"
                       "</form>"

                       "<h3> Move Folders </h3>"
                       "<form action='/drive/dir/move' method='POST'>"
                       "<input type='text' name='movefrom'>"
                       "<input type='text' name='moveto'>"
                       "<input type='submit'>"
                       "</form>"

                       "<h3> Stored Files </h3>"
                       "<table>"
                       "<tr>"
                       "<th> Filename </th>"
                       "<th> Download </th>"
                       "<th> Delete </th>"
                       "<tr>";

    vector<tuple<string, char, string>> sortedData;
    for (pair<string, string> fileInfo: data){
        string::difference_type n = std::count(fileInfo.second.begin(), fileInfo.second.end(), '/');
        string space = "";
        for (int i = 0; i < n * 2; i++){
            space += "&nbsp;";
        }
        tuple<string, char, string> newTriple = make_tuple(fileInfo.second.substr(2),fileInfo.second[0], space);
        // get<0>(newTriple) = fileInfo.second.substr(2);
        // get<1>(newTriple) = fileInfo.second[0];
        // get<2>(newTriple) = n;
        sortedData.emplace_back(newTriple);
    }

    sort(sortedData.begin(), sortedData.end());

    for (tuple<string, char, string> fileInfo: sortedData){
        //if (fileInfo.second[0] == 'F'){
        if (get<1>(fileInfo) == 'F'){
            driveBody +=
                    "<tr>"
                    "<td><i class='material-icons'>attachment</i>" + get<2>(fileInfo) + get<0>(fileInfo) + "</td>"
                                                                                                           "<td>"
                                                                                                           "<form action='/drive/download/" + get<0>(fileInfo) + "' method='GET'>"
                                                                                                                                                                 "<input type='submit' value='Download'>"
                                                                                                                                                                 "</form>"
                                                                                                                                                                 "</td>"
                                                                                                                                                                 "<td>"
                                                                                                                                                                 "<form action='/drive/delete/" + get<0>(fileInfo) + "' method='GET'>"
                                                                                                                                                                                                                     "<input type='submit' value='Delete'>"
                                                                                                                                                                                                                     "</form>"
                                                                                                                                                                                                                     "</td>"
                                                                                                                                                                                                                     "</tr>";

        } else {
            driveBody +=
                    "<tr>"
                    "<td><i class='material-icons'>folder</i>" + get<2>(fileInfo) + get<0>(fileInfo) + "</td>"
                                                                                                       "<td>"
                                                                                                       "</td>"
                                                                                                       "<td>"
                                                                                                       "<form action='/drive/dir/delete/" + get<0>(fileInfo) + "' method='GET'>"
                                                                                                                                                               "<input type='submit' value='Delete'>"
                                                                                                                                                               "</form>"
                                                                                                                                                               "</td>"
                                                                                                                                                               "</tr>";
        }
    }
    //  driveBody +=
    //  "<tr>"
    //    "<td>" + fileInfo.second.substr(2) + "</td>"
    //    "<td>"
    //    "<form action='/drive/download/" + fileInfo.second.substr(2) + "' method='GET'>"
    //      "<input type='submit' value='Download'>"
    //      "</form>"
    //    "</td>"
    //    "<td>"
    //    "<form action='/drive/delete/" + fileInfo.second.substr(2) + "' method='GET'>"
    //      "<input type='submit' value='Delete'>"
    //      "</form>"
    //    "</td>"
    //  "</tr>";
    // }

    driveBody += "</table>";
    return driveBody;
}

// check if cookie is applied to this session, and fetch the username from the request
bool is_session_valid(string& req, string& username) {
    size_t start = req.find("Cookie: sessionid=");
    if (start != string::npos) {
        size_t end = req.find("\n", start + 1);
        int len = end - start - 19;
        username = req.substr(start + 18, len);

        if (find(loggedInUsers.begin(), loggedInUsers.end(), username) != loggedInUsers.end()) {
            return true;
        }
    }
    return false;
}

void generate_username(string& req, string& username) {
    size_t start = req.find("Cookie: sessionid=");
    if (start != string::npos) {
        size_t end = req.find("\n", start + 1);
        int len = end - start - 19;
        username = req.substr(start + 18, len);
    }
}

// get local date and time
// adapted from https://www.tutorialspoint.com/cplusplus/cpp_date_time.htm
string getTimestamp(){
    time_t now = time(0);
    tm *ltm = localtime(&now);

    int year = 1900 + ltm->tm_year;
    int month = 1 + ltm->tm_mon;
    int day = ltm->tm_mday;
    int hour = ltm->tm_hour;
    int min = ltm->tm_min;
    int sec = ltm->tm_sec;

    string timestamp = to_string(year) + "/" + to_string(month) + "/" + to_string(day) + " "
                       + to_string(hour) + ":" + to_string(min) + ":" + to_string(sec);

    return timestamp;
}

sockaddr_in to_sockaddr(string address) {
    char* addr = (char*) address.c_str();
    char* ip_addr = strtok(addr, ":");
    char* port = strtok(NULL, ":");

    struct sockaddr_in sockaddr;
    bzero(&sockaddr, sizeof(sockaddr));
    sockaddr.sin_family = AF_INET;
    sockaddr.sin_port = htons(atoi(port));
    inet_pton(AF_INET, ip_addr, &sockaddr.sin_addr);

    return sockaddr;
}

void readPrim(int fd, string &address) {
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
    char* addr = strtok(NULL, " ");
    if (strcmp(command, "SERVER") == 0) {
        string tmp(addr);
        address = tmp;
        cout << "get initial server: " << address << endl;
    }
}

void store_address_and_health_map(vector<string> address_and_health) {
    vector<string> all_address;
    vector<string> all_health;
    for (int i =0; i < (address_and_health.size() / 2); i++) {
        all_address.push_back(address_and_health.at(i*2));
        all_health.push_back(address_and_health.at(i*2 + 1));
    }
    unordered_map<string, string> new_map;
    for (int i=0; i<all_address.size(); i++) {
        new_map.insert({all_address.at(i), all_health.at(i)});
    }
    address_and_health_map = new_map;
}

// we added a backend_fd here. now frontend server could directly talk to backend server
void process_http_request(string& req, int& browser_fd, int& master_fd, int& backend_fd, string &backendAddr) {
    cout << req << endl;
    string user;
    if (is_session_valid(req, user) && backendAddr == "\0") {
        string query = "QUERY " + user + "\r\n";
        write(master_fd, (char*)query.c_str(), query.size());
        readPrim(master_fd, backendAddr);
        // create socket for backend server
        if ((backend_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
            fprintf(stderr, "Cannot open socket (%s)\n", strerror(errno));
            exit(1);
        }
        sockaddr_in backAddr = to_sockaddr(backendAddr);
        if (connect(backend_fd, (struct sockaddr*)&backAddr, sizeof(backAddr)) == -1){
            perror("Failed to connect to the backend node");
            exit(1);
        }
    }


    int first_space_pos = req.find(" ");
    string method = req.substr(0,first_space_pos);
    int second_space_pos = req.find(" ", first_space_pos+1);
    string pageCommand = req.substr(first_space_pos+1,second_space_pos - first_space_pos - 1 );

    debugMsg("method: " + method, debug);
    debugMsg("pageCommand: " + pageCommand, debug);

    if (method == "GET" && pageCommand == "/drive") {
        string username;
        generate_username(req, username);
        if (is_session_valid(req, username)) {
            string write_msg = "GETALL " + username + "\r\n";
            cout << write_msg << endl;
            write(backend_fd , write_msg.c_str() , write_msg.size());
        } else {
            vector<pair<string, string>> data;
            string body = drivePage(data, backend_fd);
            body = navbar + body;
            string res = generate_response("HTTP/1.1", 200, "Success", "text/html", body);
            write(browser_fd, res.c_str(), strlen(res.c_str()));
        }

    } else if (method == "POST" && pageCommand.substr(0,16) == "/drive/file"){
        string username;
        generate_username(req, username);
        if (is_session_valid(req, username)) {
            string filename;
            string filepath;
            string fileContents;
            getFileContents(req, filepath, filename, fileContents);
            cout << "POST /drive" << endl;
            bool isValid = true;//newFolderIsValid(filepath);
            if (isValid) {
                clientInfo[clientNo]["fileContents"] = fileContents;
                // prepend file with F: to indicate we are storing file contents
                string write_msg = "PUT " + username + " F:root/" + filename + " " + to_string(fileContents.length() - 2) + "\r\n";
                int write_msg_len = strlen(write_msg.c_str());

                // send PUT command to backend
                write(backend_fd , write_msg.c_str() , write_msg_len);

                vector<pair<string, string>> data;
                string body = drivePage(data, backend_fd);
                body = navbar + body;
                string res = generate_response("HTTP/1.1", 200, "Success", "text/html", body);
                write(browser_fd, res.c_str(), strlen(res.c_str()));

            } else {// if not valid do not PUT into bigtable
                vector<pair<string, string>> data;
                string body = drivePage(data, backend_fd);
                body = navbar + body;
                string res = generate_response("HTTP/1.1", 200, "Success", "text/html", body);
                write(browser_fd, res.c_str(), strlen(res.c_str()));
            }
        } else {
            vector<pair<string, string>> data;
            string body = drivePage(data, backend_fd);
            body = navbar + body;
            string res = generate_response("HTTP/1.1", 200, "Success", "text/html", body);
            write(browser_fd, res.c_str(), strlen(res.c_str()));
        }

    } else if (method == "POST" && pageCommand.substr(0,16) == "/drive/dir") {
        string username;
        generate_username(req, username);
        if (is_session_valid(req, username)) {

            string filename;
            string fileContents;
            string split_on_filename = "filename=";
            filename = req.substr(req.find(split_on_filename));
            filename = filename.substr(split_on_filename.length());

            // subtract -2 to not include \n and trailing quote
            filename = filename.substr(0, filename.find('\n') - 2);
            filename = replaceAll(filename, "%2F", "\/");
            cout << "filename: " << filename << endl;
            bool isValid = true;//newFolderIsValid(filename);
            cout << "################################" << endl;
            cout << isValid << endl;
            if (isValid){
                fileContents = "dir\r\n";
                clientInfo[clientNo]["fileContents"] = fileContents;

                // prepend file with D: to indicate we are storing a directory
                string write_msg = "PUT " + username + " D:root/" + filename + " " + to_string(fileContents.length() - 2) + "\r\n";
                int write_msg_len = strlen(write_msg.c_str());

                // send PUT command to backend
                write(backend_fd , write_msg.c_str() , write_msg_len);

                vector<pair<string, string>> data;
                string body = drivePage(data, backend_fd);
                body = navbar + body;
                string res = generate_response("HTTP/1.1", 200, "Success", "text/html", body);
                write(browser_fd, res.c_str(), strlen(res.c_str()));
            } else {// if not valid do not PUT into bigtable
                vector<pair<string, string>> data;
                string body = drivePage(data, backend_fd);
                body = navbar + body;
                string res = generate_response("HTTP/1.1", 200, "Success", "text/html", body);
                write(browser_fd, res.c_str(), strlen(res.c_str()));
            }
        } else {
            vector<pair<string, string>> data;
            string body = drivePage(data, backend_fd);
            body = navbar + body;
            string res = generate_response("HTTP/1.1", 200, "Success", "text/html", body);
            write(browser_fd, res.c_str(), strlen(res.c_str()));
        }

    } else if (method == "GET" && pageCommand.substr(0,14) == "/drive/delete/"){
        string username;
        generate_username(req, username);
        if (is_session_valid(req, username)) {
            string filename = pageCommand.substr(14);
            filename.pop_back(); // remove ? at the end
            string write_msg = "DELETE " + username + " F:" + filename + "\r\n";
            cout << write_msg << endl;
            write(backend_fd , write_msg.c_str() , write_msg.size());
        } else {
            vector<pair<string, string>> data;
            string body = drivePage(data, backend_fd);
            body = navbar + body;
            string res = generate_response("HTTP/1.1", 200, "Success", "text/html", body);
            write(browser_fd, res.c_str(), strlen(res.c_str()));
        }
    } else if (method == "GET" && pageCommand.substr(0,18) == "/drive/dir/delete/"){
        string username;
        generate_username(req, username);
        if (is_session_valid(req, username)) {
            string filename = pageCommand.substr(18);
            filename.pop_back(); // remove ? at the end
            string write_msg = "DELETEALL " + username + " D:" + filename + "\r\n";
            cout << write_msg << endl;
            write(backend_fd , write_msg.c_str() , write_msg.size());
        } else {
            vector<pair<string, string>> data;
            string body = drivePage(data, backend_fd);
            body = navbar + body;
            string res = generate_response("HTTP/1.1", 200, "Success", "text/html", body);
            write(browser_fd, res.c_str(), strlen(res.c_str()));
        }

    } else if (method == "GET" && pageCommand.substr(0,16) == "/drive/download/"){
        string username;
        generate_username(req, username);
        if (is_session_valid(req, username)) {
            string filename = pageCommand.substr(16);
            filename.pop_back(); // remove ? at the end
            string getFilename;
            getFilename = filename;
            clientInfo[clientNo]["getFilename"] = getFilename;
            string write_msg = "GET " + username + " F:" + filename + "\r\n";
            cout << write_msg << endl;
            write(backend_fd , write_msg.c_str() , write_msg.size());
        } else {
            vector<pair<string, string>> data;
            string body = drivePage(data, backend_fd);
            body = navbar + body;
            string res = generate_response("HTTP/1.1", 200, "Success", "text/html", body);
            write(browser_fd, res.c_str(), strlen(res.c_str()));
        }

    } else if (method == "POST" && pageCommand.substr(0,11) == "/drive/move"){
        string username;
        generate_username(req, username);
        if (is_session_valid(req, username)) {
            string filepath1;
            string filepath2;
            getFromAndToPaths(req, filepath1, filepath2);

            string write_msg = "MOVE " + username + " F:root/" + filepath1 + " F:root/" + filepath2 + "\r\n";
            cout << write_msg << endl;
            write(backend_fd , write_msg.c_str() , write_msg.size());
        } else {
            vector<pair<string, string>> data;
            string body = drivePage(data, backend_fd);
            body = navbar + body;
            string res = generate_response("HTTP/1.1", 200, "Success", "text/html", body);
            write(browser_fd, res.c_str(), strlen(res.c_str()));
        }


    } else if (method == "POST" && pageCommand.substr(0,15) == "/drive/dir/move"){
        string username;
        generate_username(req, username);
        if (is_session_valid(req, username)) {
            string filepath1;
            string filepath2;
            getFromAndToPaths(req, filepath1, filepath2);

            string write_msg = "MOVEDIR " + username + " D:root/" + filepath1 + " D:root/" + filepath2 + "\r\n";
            cout << write_msg << endl;
            write(backend_fd , write_msg.c_str() , write_msg.size());
        } else {
            vector<pair<string, string>> data;
            string body = drivePage(data, backend_fd);
            body = navbar + body;
            string res = generate_response("HTTP/1.1", 200, "Success", "text/html", body);
            write(browser_fd, res.c_str(), strlen(res.c_str()));
        }

    } else if (method == "GET" && pageCommand == "/compose") {
        string body = string(navbar) + string(compose_page);
        body = generate_body(body);
        string res = generate_response("HTTP/1.1", 200, "Success", "text/html", body);
        write(browser_fd, res.c_str(), strlen(res.c_str()));
    } else if (method == "GET" && pageCommand == "/inbox") {

        string username;
        generate_username(req, username);
        if (is_session_valid(req, username)) {
            cout << "is_session_valid = true; write GETINBOX to backend" << endl;
            // first write request to backend
            string write_msg = "GETINBOX "+ username +"\r\n";
            write(backend_fd , write_msg.c_str() ,  write_msg.size());
        } else {
            cout << "is_session_valid = false" << endl;
            string body = string(navbar) + string(empty_inbox_page);
            body = generate_body(body);
            // TODO change into 400
            string res = generate_response("HTTP/1.1", 200, "Success", "text/html", body);
            write(browser_fd, res.c_str(), strlen(res.c_str()));
        }

    } else if (method == "GET" && pageCommand == "/login") {
        string body = string(navbar) + string(login_page);
        body = generate_body(body);
        string res = generate_response("HTTP/1.1", 200, "Success", "text/html", body);
        write(browser_fd, res.c_str(), strlen(res.c_str()));

    } else if (method == "GET" && pageCommand == "/register") {
        string body = string(navbar) + string(register_page);
        body = generate_body(body);
        string res = generate_response("HTTP/1.1", 200, "Success", "text/html", body);
        write(browser_fd, res.c_str(), strlen(res.c_str()));

    } else if (method == "GET" && pageCommand == "/changepassword") {
        string body = string(navbar) + string(changePassword_page);
        body = generate_body(body);
        string res = generate_response("HTTP/1.1", 200, "Success", "text/html", body);
        write(browser_fd, res.c_str(), strlen(res.c_str()));

    } else if (method == "POST" && pageCommand == "/login") {
        int usernamePos = req.find("username=");
        int password1Pos = req.find("password=");
        string username = req.substr(usernamePos+9, password1Pos-1-usernamePos-9);
        string password1 = req.substr(password1Pos+9);

        // ask the backend master for which server to go to
        if (backendAddr == "\0") {  // not yet known the backend address
            string query = "QUERY " + username + "\r\n";
            write(master_fd, (char*)query.c_str(), query.size());
            readPrim(master_fd, backendAddr);
            // create socket for backend server
            if ((backend_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
                fprintf(stderr, "Cannot open socket (%s)\n", strerror(errno));
                exit(1);
            }
            sockaddr_in backAddr = to_sockaddr(backendAddr);
            if (connect(backend_fd, (struct sockaddr*)&backAddr, sizeof(backAddr)) == -1){
                perror("Failed to connect to the backend node");
                exit(1);
            }
        }

        // write LOGIN request to backend
        string write_msg = "LOGIN " + username + " " + password1 +"\r\n";
        clientInfo[clientNo]["username"] = username;
        write(backend_fd , write_msg.c_str() , write_msg.size());

    } else if (method == "POST" && pageCommand.substr(0,9) == "/register") {
        int usernamePos = req.find("username=");
        int password1Pos = req.find("password1=");
        int password2Pos = req.find("password2=");
        string username = req.substr(usernamePos+9, password1Pos-1-usernamePos-9);
        string password1 = req.substr(password1Pos+10, password2Pos-1-password1Pos-10);
        string password2 = req.substr(password2Pos+10);
        if (password1 == password2) {
            // ask the backend master for which server to go to
            string query = "QUERY " + username + "\r\n";
            write(master_fd, (char*)query.c_str(), query.size());
            readPrim(master_fd, backendAddr);
            // create socket for backend server
            if ((backend_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
                fprintf(stderr, "Cannot open socket (%s)\n", strerror(errno));
                exit(1);
            }
            sockaddr_in backAddr = to_sockaddr(backendAddr);
            if (connect(backend_fd, (struct sockaddr*)&backAddr, sizeof(backAddr)) == -1){
                perror("Failed to connect to the backend node");
                exit(1);
            }

            // write REGISTER request to backend
            string write_msg = "REGISTER " + username + " " + password1 +"\r\n";
            clientInfo[clientNo]["username"] = username;
            write(backend_fd , write_msg.c_str() , write_msg.size());

        } else {
            string errorMessage = "<p>Please enter the same password.\n";
            string body = string(navbar) + string(register_page) + errorMessage;
            body = generate_body(body);
            string res = generate_response("HTTP/1.1", 200, "Success", "text/html", body);
            write(browser_fd, res.c_str(), strlen(res.c_str()));
        }
    } else if (method == "POST" && pageCommand.substr(0,15) == "/changepassword") {
        string username;
        if (is_session_valid(req, username)) {
            int oriPasswordPos = req.find("oriPassword=");
            int newPasswordPos = req.find("newPassword=");
            string oriPassword = req.substr(oriPasswordPos + 12, newPasswordPos - 1 - oriPasswordPos - 12);
            string newPassword = req.substr(newPasswordPos + 12);
            // write CPUT request to backend
            string write_msg = "CPUT " + username + + " password " + oriPassword + " " + newPassword + "\r\n";
            cout << "Change password: " + write_msg;
            write(backend_fd, write_msg.c_str(), write_msg.size());

        } else {
            string errorMessage = "<p>PLease Login first.\n";
            string body = string(navbar) + string(changePassword_page) + errorMessage;
            body = generate_body(body);
            string res = generate_response("HTTP/1.1", 200, "Success", "text/html", body);
            write(browser_fd, res.c_str(), strlen(res.c_str()));

        }
    } else if (method == "GET" && pageCommand == "/") {
        string body = string(navbar) + string(login_page);
        body = generate_body(body);
        string res = generate_response("HTTP/1.1", 200, "Success", "text/html", body);
        write(browser_fd, res.c_str(), strlen(res.c_str()));
    }
    else if (method == "POST" && pageCommand == "/sendmail") {
        //cout << req << endl;
        int recipient_pos = req.find("recipient=");
        int subject_pos = req.find("subject=");
        int mailcontent_pos = req.find("mailcontent=");

        string recipient = req.substr(recipient_pos + 10, subject_pos-1-recipient_pos-10);
        string subject = req.substr(subject_pos + 8, mailcontent_pos-1-subject_pos-8 );
        string mailcontent = req.substr(mailcontent_pos+12);
        cout << recipient << endl;
        cout << subject << endl;
        cout << mailcontent << endl;

        // generate current time
        auto time1 = std::chrono::system_clock::now();
        time_t time2 = std::chrono::system_clock::to_time_t(time1);
        char* current_time = std::ctime(&time2);
        string current_time_string = current_time;
        // remove the "/n" at the end
        current_time_string = current_time_string.substr(0, current_time_string.size()-1);

        cout << current_time_string << endl;

        // TODO replace username
        string username;
        generate_username(req, username);
        string write_msg = "SENDMAIL " + username + "|"
                           + recipient + "|"
                           + subject + "|"
                           + current_time_string + "|"
                           + mailcontent + "\r\n";
        cout<< "write_msg = " << write_msg <<endl;
        write(backend_fd , write_msg.c_str() , write_msg.size());
    }
    else if (method == "GET" && pageCommand.substr(0,13)  == "/email/delete")
    {
        string username;
        generate_username(req, username);
        // TODO it only suppports 1 digit index now
        string write_msg = "DELETEMAIL " + pageCommand.substr(14,1) + " " + username +"\r\n";
        cout << "write_msg = " << write_msg << endl;
        write(backend_fd , write_msg.c_str() , write_msg.size());
    }
    else if (method == "GET" && pageCommand.substr(0,12)  == "/email/reply") {
        string username;
        generate_username(req, username);
        string write_msg = "REPLYMAIL " + pageCommand.substr(13,1) +" " + username + " ";
        int reply_content_pos = req.find("reply=");
        int http_pos = req.find("HTTP/1.1");

        string reply_content = req.substr(reply_content_pos + 6, http_pos -1 - reply_content_pos - 6);
        write_msg += (reply_content + "\r\n");
        write(backend_fd , write_msg.c_str() , write_msg.size());

        cout << "request = " << req << endl;
        cout << "reply index = " << pageCommand.substr(13,1) << endl;
        cout << "reply_content = " << reply_content << endl;
        cout << "write_msg = " << write_msg << endl;
    }
    else if (method == "GET" && pageCommand.substr(0,14)  == "/email/forward") {
        /*
         * FORWARD 0 username
         * user1
         */

        string username;
        generate_username(req, username);
        string write_msg = "FORWARDMAIL " + pageCommand.substr(15,1) + " " + username + " ";
        int forward_address_pos = req.find("forward=");
        int http_pos = req.find("HTTP/1.1");

        string forward_address = req.substr(forward_address_pos + 8, http_pos -1 - forward_address_pos - 8);
        write_msg += (forward_address + "\r\n");
        write(backend_fd , write_msg.c_str() , write_msg.size());

        cout << "request = " << req << endl;
        cout << "forward index = " << pageCommand.substr(15,1) << endl;
        cout << "forward_address = " << forward_address << endl;
        cout << "write_msg = " << write_msg << endl;
    }  else if (method == "GET" && pageCommand.substr(0,6)  == "/admin") {
        string username;
        generate_username(req, username);
        string write_msg = "STORAGE\r\n";
        cout << "write_msg = " << write_msg << endl;
        write(backend_fd , write_msg.c_str() , write_msg.size());
    }
}

string generate_inbox_row(string sender, string date, string title, string content, int index) {
    string res;
    res = "<tr><th>" + sender + "</th><th>"+title+"</th><th>" +date+"</th><th>"+content+"</th>";
    // delete button
    res += "<th><form action='/email/delete/" + to_string(index) + "' method='GET'><input type='submit' value='Delete'></form></th>";
    // reply textarea + button
    res += "<th><form action='/email/reply/" + to_string(index) + "' method='GET'>" +
           "<input type='text' name='reply' size='40'></input><input type='submit' value='Reply'></form></th>";
    // forward button
    res += "<th><form action='/email/forward/" + to_string(index) + "' method='GET'>" +
           "<input type='text' name='forward' size='40'></input><input type='submit' value='Forward'></form></th>";
    res += "</tr>";
    return res;
}

string generate_backend_row(string address, string health) {
    string row = "<tr><th>" + address + "</th><th>"+health+"</th></tr>";
    return row;
}

string generate_backend_table() {
    string backend_table = "<table><tr><th>Address</th> <th>Status</th>";
    for (auto it : address_and_health_map) {
        string row = generate_backend_row(it.first, it.second);
        backend_table += row;
    }

    backend_table += "/<table>";

    backend_table += "<p>Kill</p><form method='post' action='/admin/kill'><input type='text' name='kill' size='40'>"
                     "</input><input type='submit' value='Kill'></input></form>";
    backend_table += "<p>Activate</p><form method='post' action='/admin/activate'><input type='text' name='activate' size='40'></input>"
                     "<input type='submit' value='Activate'></input></form>";

    return backend_table;
}
string generate_storage_row(string row, string col, string content) {
    string res;
    if (content.size()>1000) {
        content = "file";
    }
    res = "<tr><th>"+row+"</th><th>"+col+"</th><th>"+content+"</th></tr>";
    return res;
}

string generate_storage_table(vector<string> vec) {
    string res = "<table><tr><th>Row</th> <th>Col</th><th>Content</th></tr>";
    for (int i =0; i < (vec.size() /3); i++) {
        string a = vec.at(i*3);
        string b = vec.at(i*3+1);
        string c = vec.at(i*3+2);
        cout << "a,b,c=" << a << " " << b << " " << c << endl;
        res += generate_storage_row(a,b,c);
    }
    res += "</table>";
    cout << "table=" <<  res << endl;
    return res;
}
/*
 *  <table>
      <tr>
        <th>Company</th>
        <th>Contact</th>
      </tr>
      <tr>
        <td>Centro comercial Moctezuma</td>
        <td>Francisco Chang</td>
        <td>Mexico</td>
      </tr>
    </table>
 */


/**
 *  From <benjamin.franklin@localhost> Mon Oct 17 07:07:41 2022
    From: Benjamin Franklin <benjamin.franklin@localhost>
    To: Linh Thi Xuan Phan <linhphan@localhost>
    Date: Fri, 21 Oct 2016 18:29:11 -0400
    Subject: Testing my new email account
    hello
 */

string generate_inbox_table(string inbox_content) {
    string table = "<table><tr> <th>From</th> <th>Subject</th> <th>Date</th> <th>Content</th><th>Delete</th><th>Reply</th><th>Forward</th></tr>";
    int from_pos = inbox_content.find("From ");
    int from_colon_pos = inbox_content.find("From: ");
    int to_pos = inbox_content.find("To: ");
    int date_pos = inbox_content.find("Date: ");
    int subject_pos = inbox_content.find("Subject: ");
    int content_start_pos = inbox_content.find("\n", subject_pos+1) + 1;
    int index = 0;

    while (from_pos != std::string::npos) {
        // all need to -1 because of /n at the end
        string sender = inbox_content.substr(from_colon_pos + 6, to_pos - from_colon_pos - 6 -1 );
        string date = inbox_content.substr(date_pos + 6, subject_pos - date_pos -6 -1);
        string subject = inbox_content.substr(subject_pos + 9, content_start_pos - subject_pos - 9 - 1 );
        from_pos = inbox_content.find("From ", from_pos + 1);
        string content = inbox_content.substr(content_start_pos, from_pos - content_start_pos);

        cout << "sender = " << sender << endl;
        cout << "date = " << date << endl;
        cout << "subject = " << subject << endl;
        cout << "content = " << content << endl;

        table += generate_inbox_row(sender, date, subject, content, index);

        // find the next iteration
        from_colon_pos = inbox_content.find("From: ", from_colon_pos + 1);
        to_pos = inbox_content.find("To: ", to_pos + 1);
        date_pos = inbox_content.find("Date: ", date_pos + 1);
        subject_pos = inbox_content.find("Subject: ", subject_pos + 1);
        content_start_pos = inbox_content.find("\n", subject_pos+1) + 1;
        index += 1;
    }

    table += "</table>";
    return table;
}


string generate_frontend_row(string address, string health) {

}

string generate_admin_page(string big_table_content) {
    string backend_table = "<table><tr><th>Address</th> <th>Status</th>";
    for (auto it : address_and_health_map) {
        string row = generate_backend_row(it.first, it.second);
        backend_table += row;
    }

    backend_table += "/<table>";

    backend_table += "<p>Kill</p><form method='post' action='/admin/kill'><input type='text' name='kill' size='40'>"
                     "</input><input type='submit' value='Kill'></input></form>";
    backend_table += "<p>Activate</p><form method='post' action='/admin/activate'><input type='text' name='activate' size='40'></input>"
                     "<input type='submit' value='Activate'></input></form>";

    return backend_table;
}

void * thread_job(void * fd){
    int browser_fd = *(int *) fd;
    cout << "browser_fd in thread =" << browser_fd;
    char buffer[10000];
    char resp[10000];
    int read_len;
    char full_line[10000];

    if (debug) {
        fprintf(stderr, "[%d] connected", browser_fd);
    }

    // create fd for backend master
    int master_fd;
    if ((master_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        fprintf(stderr, "Cannot open socket (%s)\n", strerror(errno));
        exit(1);
    }
    sockaddr_in masterAddr = to_sockaddr(MASTER_ADDR);
    if (connect(master_fd, (struct sockaddr*)&masterAddr, sizeof(masterAddr)) == -1){
        perror("Failed to connect to the master node");
        exit(1);
    }

    // create fd for backend server
    int backend_fd = -1;
    string backendAddr = "\0";

    while(1)
    {
        // prepare the fd sets for select
        fd_set r, w;
        FD_ZERO(&r);
        FD_ZERO(&w);
        FD_SET(browser_fd, &r);
        FD_SET(master_fd, &r);
        if (backend_fd != -1) {
            FD_SET(backend_fd, &r);
        }

        int max_fd = browser_fd;
        if (backend_fd > max_fd){
            max_fd = backend_fd;
        }
        if (master_fd > max_fd) {
            max_fd = master_fd;
        }

        int ret = select(max_fd + 1, &r, &w, NULL, NULL);
        if (ret == -1){
            perror("In select");
            exit(EXIT_FAILURE);
        }

        // handle user input in the browser
        if (FD_ISSET(browser_fd, &r)){
//            printf("\n+++++++ Waiting for new connection ++++++++\n\n");

            char buffer[30000] = {0};
            int valread = read( browser_fd , buffer, 30000);

//            printf("%s\n",buffer );
            if (valread <= 0){
                continue;
            }

            string http_request = buffer;

            process_http_request(http_request, browser_fd, master_fd, backend_fd, backendAddr);

            // TODO do we need the following line (write)?
            // write(new_socket , http_response.c_str() , strlen(http_response.c_str()));

            // TODO do we need to close?
            //close(browser_fd);


        } else if (FD_ISSET(master_fd, &r)) {
            //readPrim(master_fd, backendAddr);
            char buffer[1000] = {'\0'};
            char* curr = buffer;

            char prevChar = '\0';
            char curChar = '\0';
            while (!(prevChar == '\r' && curChar == '\n')) {
                read(master_fd, curr, 1);
                prevChar = curChar;
                curChar = *curr;
                curr += 1;
            }
            fprintf(stderr, "Command received: %s", &buffer[0]);
            char* command = strtok(&buffer[0], " ");

            if (strcmp(command, "SERVER") == 0) {
                char* addr = strtok(NULL, " ");
                string tmp(addr);
                backendAddr = tmp;

                close(backend_fd);
                if ((backend_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
                    fprintf(stderr, "Cannot open socket (%s)\n", strerror(errno));
                    exit(1);
                }
                sockaddr_in backAddr = to_sockaddr(backendAddr);
                if (connect(backend_fd, (struct sockaddr*)&backAddr, sizeof(backAddr)) == -1){
                    perror("Failed to connect to the backend node");
                    exit(1);
                }
            } else if (strcmp(command, "STATUS") == 0) {
//                string status_info = buffer;
//                vector<string> address_and_health;
//                address_and_health = split(buffer, " ");
//                string admin_page = generate_admin_page(address_and_health);
            }



        } else if (FD_ISSET(backend_fd, &r)){   // handle input from the backend server

//            printf("\n+++++++ Waiting for new backend connection ++++++++\n\n");

            // read from backend server
            char back_buffer[30000] = {0};
            int valread = read( backend_fd , back_buffer, 30000);

            // TODO: This is using sample file contents. Replace
            // with actual file contents
            string bb = back_buffer;
//            cout << bb;
            if (bb.substr(0,3) == "202"){ // reading the value to be stored...
                cout << "202 was received" << endl;
                //string binary_msg = fileContents;
                string binary_msg = clientInfo[clientNo]["fileContents"];
                int binary_msg_len = strlen(binary_msg.c_str());
                write(backend_fd , binary_msg.c_str() , binary_msg_len);

            } else if (bb.substr(0,3) == "201") {     // registration succeed.
                string body = string(navbar) + string(login_page);
                body = generate_body(body);
                string res = generate_response("HTTP/1.1", 200, "Success", "text/html", body);
                write(browser_fd, res.c_str(), strlen(res.c_str()));

            } else if (bb.substr(0,3) == "401") {    // User already existed
                string errorMessage = "<p>Username already existed. PLease try again.\n";
                string body = string(navbar) + string(register_page) + errorMessage;
                body = generate_body(body);
                string res = generate_response("HTTP/1.1", 200, "Success", "text/html", body);
                write(browser_fd, res.c_str(), strlen(res.c_str()));

            } else if (bb.substr(0,3) == "200") {   // login succeed
                string sucMessage = "<p>Login succeed.\n";
                string body = string(navbar) + string(login_page) + sucMessage;

                // parse username from response
                size_t pos = bb.find("username:");
                string username = bb.substr(pos + 9);
                username.pop_back();
                username.pop_back();    // get rid of /r/n

                loggedInUsers.emplace_back(username);
                body = generate_body(body);
                string res = generate_response("HTTP/1.1", 200, "Success", "text/html", body, username);
                write(browser_fd, res.c_str(), strlen(res.c_str()));

            } else if (bb.substr(0,3) == "402") {     // User not exist
                string errorMessage = "<p>User does not exist. Please try again.\n";
                string body = string(navbar) + string(login_page) + errorMessage;
                body = generate_body(body);
                string res = generate_response("HTTP/1.1", 200, "Success", "text/html", body);
                write(browser_fd, res.c_str(), strlen(res.c_str()));

            } else if (bb.substr(0,3) == "403") {    // Invalid password
                string errorMessage = "<p>Invalid password. Please try again.\n";
                string body = string(navbar) + string(login_page) + errorMessage;
                body = generate_body(body);
                string res = generate_response("HTTP/1.1", 200, "Success", "text/html", body);
                write(browser_fd, res.c_str(), strlen(res.c_str()));

                // TODO: CHANGE HERE
            } else if (bb.substr(0,3) == "213") {
                // FIND_INBOX_SUCCESS = "210 Successfully found the inbox content for user.\r\n";
                int new_line_position = bb.find("\r\n");
                string all_inbox_content = bb.substr(new_line_position + 2);
                cout << "frontend receives inbox content = " << all_inbox_content << endl;
                string body = string(navbar) + generate_inbox_table(all_inbox_content);
                body = generate_body(body);
                string res = generate_response("HTTP/1.1", 200, "Success", "text/html", body);
                write(browser_fd, res.c_str(), strlen(res.c_str()));
            } else if (bb.substr(0,16) == "-ERR get inbox for email\r\n") {

            } else if (bb.substr(0,3) == "210") { // got all files for drive
                string dataStr = bb.substr(28);
                clientInfo[clientNo]["allFiles"] = dataStr;
                vector<pair<string, string>> data = filesStrToVec(dataStr, "\n");
                string body = drivePage(data, backend_fd);
                body = navbar + body;
                string res = generate_response("HTTP/1.1", 200, "Success", "text/html", body);
                write(browser_fd, res.c_str(), strlen(res.c_str()));
            } else if (bb.substr(0, 3) == "204"){ // successfully erased the value
                vector<pair<string, string>> data;
                string body = drivePage(data, backend_fd);
                body = navbar + body;
                string res = generate_response("HTTP/1.1", 200, "Success", "text/html", body);
                write(browser_fd, res.c_str(), strlen(res.c_str()));

            } else if (bb.substr(0, 3) == "211"){ // successfully moved a file
                cout << "GOT HERE AFTER MOVE" << endl;
                vector<pair<string, string>> data;
                string body = drivePage(data, backend_fd);
                body = navbar + body;
                string res = generate_response("HTTP/1.1", 200, "Success", "text/html", body);
                write(browser_fd, res.c_str(), strlen(res.c_str()));

            } else if (bb.substr(0, 3) == "212"){ // successfully moved a folder
                cout << "GOT HERE AFTER MOVE DIR" << endl;
                vector<pair<string, string>> data;
                string body = drivePage(data, backend_fd);
                body = navbar + body;
                string res = generate_response("HTTP/1.1", 200, "Success", "text/html", body);
                write(browser_fd, res.c_str(), strlen(res.c_str()));

            } else if (bb.substr(0, 3) == "410"){ // invalid folder during move
                cout << "GOT INVALID AFTER MOVE" << endl;
                vector<pair<string, string>> data;
                string body = drivePage(data, backend_fd);
                body = navbar + body;
                string res = generate_response("HTTP/1.1", 200, "Success", "text/html", body);
                write(browser_fd, res.c_str(), strlen(res.c_str()));

            } else if (bb.substr(0,3) == "208") {
                string fc = bb.substr(17);
                cout << "fc: " << fc << endl;
                //string downloadPath = "/home/cis5050/Downloads/" + getFilename;
                string downloadPath = "/home/cis5050/Downloads/";
                string fpath = clientInfo[clientNo]["getFilename"];
                if (fpath.find("\/") != string::npos){
                    fpath = fpath.substr(fpath.find_last_of("\/"));
                }

                downloadPath += fpath;

                //downloadPath = getFilename.substr(newFolder.find_last_of("\/"));
                cout << "download path: " << downloadPath << endl;

                ofstream f;
                f.open (downloadPath);
                f << fc;
                f.close();

                // int fdFile = open(downloadPath.c_str(), O_WRONLY);
                // cout << "fdfile " << fdFile << endl;
                // write(fdFile, fc.c_str(), sizeof(char)*strlen(fc.c_str()));
                // close(fdFile);

                vector<pair<string, string>> data;
                string body = drivePage(data, backend_fd);
                body = navbar + body;
                string res = generate_response("HTTP/1.1", 200, "Success", "text/html", body);
                write(browser_fd, res.c_str(), strlen(res.c_str()));

            } else if (bb.substr(0, 3) == "209") { // successfully erased folder and sub folders and files
                vector<pair<string, string>> data;
                string body = drivePage(data, backend_fd);
                body = navbar + body;
                string res = generate_response("HTTP/1.1", 200, "Success", "text/html", body);
                write(browser_fd, res.c_str(), strlen(res.c_str()));

            } else if (bb.substr(0,3) == "214") {
                // FIND_INBOX_SUCCESS = "214 Successfully update the inbox content for user.\r\n";
                string body = string(navbar) + string(compose_page);
                body = generate_body(body);
                string res = generate_response("HTTP/1.1", 200, "Success", "text/html", body);
                write(browser_fd, res.c_str(), strlen(res.c_str()));
            } else if (bb.substr(0,3) == "215") {
                //DELETE_EMAIL_SUCCESS = "215 Successfully delete the email.\r\n";
                int new_line_position = bb.find("\r\n");
                string all_inbox_content = bb.substr(new_line_position + 2);
                cout << "frontend receives new inbox content = " << all_inbox_content << endl;
                string body = string(navbar) + generate_inbox_table(all_inbox_content);
                body = generate_body(body);
                string res = generate_response("HTTP/1.1", 200, "Success", "text/html", body);
                write(browser_fd, res.c_str(), strlen(res.c_str()));

            } else if (bb.substr(0,3) == "218") {
                string sucMessage = "<p>Change value succeed.\n";
                string body = string(navbar) + string(changePassword_page) + sucMessage;
                body = generate_body(body);
                string res = generate_response("HTTP/1.1", 200, "Success", "text/html", body);
                write(browser_fd, res.c_str(), strlen(res.c_str()));
            } else if (bb.substr(0,3) == "408") {
                string errorMessage = "<p>Value does not match please try again.\n";
                string body = string(navbar) + string(changePassword_page) + errorMessage;
                body = generate_body(body);
                string res = generate_response("HTTP/1.1", 200, "Success", "text/html", body);
                write(browser_fd, res.c_str(), strlen(res.c_str()));
            } else if (bb.substr(0,3) == "216") {
                //REPLY_EMAIL_SUCCESS = "216 Successfully reply the email.\r\n";                int new_line_position = bb.find("\r\n");
                int new_line_position = bb.find("\r\n");
                string all_inbox_content = bb.substr(new_line_position + 2);
                cout << "frontend receives new inbox content = " << all_inbox_content << endl;
                string body = string(navbar) + generate_inbox_table(all_inbox_content);
                body = generate_body(body);
                string res = generate_response("HTTP/1.1", 200, "Success", "text/html", body);
                write(browser_fd, res.c_str(), strlen(res.c_str()));
            } else if (bb.substr(0, 3) == "217") {
                int new_line_position = bb.find("\r\n");
                string all_inbox_content = bb.substr(new_line_position + 2);
                cout << "frontend receives new inbox content = " << all_inbox_content << endl;
                string body = string(navbar) + generate_inbox_table(all_inbox_content);
                body = generate_body(body);
                string res = generate_response("HTTP/1.1", 200, "Success", "text/html", body);
                write(browser_fd, res.c_str(), strlen(res.c_str()));
            } else if (bb.substr(0,3) == "299") {
                cout << "299 got here" << endl;
                int stick_position = bb.find("|");
                string storage = bb.substr(stick_position + 1);
                cout << "storage = " << storage << endl;
                //storage = storage.substr(0, storage.size()-1);
                vector<string> all_cells = split(storage, "|");
                all_cells.pop_back();
                cout << "all_cells.size() = " << all_cells.size() << endl;
                string res;
                res =  string(navbar)  + generate_storage_table(all_cells);
                string body = generate_body(res);
                cout << "body = " << body << endl;
                res = generate_response("HTTP/1.1", 200, "Success", "text/html", body);
                cout << "res = " << res << endl;
                write(browser_fd, res.c_str(), strlen(res.c_str()));
            }
        }
    }

    if (debug) {
        fprintf(stderr, "[%d] S: Connection closed\r\n", browser_fd);
    }

    close(browser_fd);
    pthread_exit(NULL);
}


void initialize_server(int port) {
    hostfd = socket(AF_INET, SOCK_STREAM, 0);

    if (hostfd < 0 ){
        fprintf(stderr, "Cannot open socket\n");
        exit(1);
    }

    struct sockaddr_in servaddr;
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(port);
    int bind_res = bind(hostfd, (struct sockaddr*)&servaddr, sizeof(servaddr));
    cout << "bind_res = " << bind_res << endl;

    int listen_res = listen(hostfd, 4096);
    cout << "listen_res = " << listen_res << endl;
    cout << "init done" << endl;

}

int main(int argc, char *argv[])
{
    portNo = 8080;
    bool debug = false;

    handleArguments(argc, argv, portNo, debug);

    // initialize server
    initialize_server(portNo);

    // init client fd array
    int clientFds[1000];

    // TODO multithread
    while (true) {
        cout << "hello" << endl;
        struct sockaddr_in clientaddr;
        socklen_t clientaddrlen = sizeof(clientaddr);

        int browser_fd;

        cout << "hostfd = " << hostfd << endl;
        browser_fd = accept(hostfd, (struct sockaddr*)&clientaddr, &clientaddrlen);
        cout << "New connection! browser_fd = " << browser_fd << endl;

        if (browser_fd >= 0){
            cout << "clientNo = " << clientNo << endl;
            unordered_map<string, string> tmp;
            clientInfo[clientNo] = tmp;
            clientFds[clientNo] = browser_fd;
            pthread_t thread;
            if (pthread_create(&thread, NULL, thread_job, (void*)&browser_fd) !=0){
                fprintf(stderr, "Failed to create thread\n");
                exit(1);
            };
            clientNo ++;
        }

//        all_threads.push_back(thread);
//        all_fd.push_back(*comm_fd);
    }

}

