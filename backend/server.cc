#include <iostream>
#include <fstream>
#include <vector>
#include <unordered_map>
#include <netinet/in.h>
#include <cstdlib>
#include <unistd.h>
#include <string.h>
#include <algorithm>
#include <arpa/inet.h>
#include <map>
#include <sstream>
#include <dirent.h>
#include <chrono>
#include <openssl/md5.h>

using namespace std;

struct info_for_each_email {
	char *username;
	string first_line;
	string content;
	string hex_string_uid;
	bool delete_status = false;
};

struct Server {
	string ip;
	string port;
	bool awake;
	//Server(string ip, string port): ip(ip), port(port) {awake = true;};
};

struct Request {
	string row;
	string column;
	string response;
	string serverPort;
	vector<char> originalValue;
	int clientFD;
	int serverFD;
};

const string LOGIN_SUCCESS = "200 Login successfully, username:";
const string REGISTER_SUCCESS = "201 Registration succeed.\r\n";
const string ACCEPTED_MORE_ACTION = "202 Request accepted, more action to be done.\r\n";
const string SUCCESS_INSERTION = "203 Inserted successfully into the bigtable.\r\n";
const string DELETION_SUCCESS = "204 Successfully erased the value.\r\n";
const string FIRST_COMPARISON_SUCCESS = "205 First value matched, now receiving the second value.\r\n";
const string REPLACE_SUCCESS = "206 Replaced the value successfully.\r\n";
const string GET_FILE_CONTENTS_SUCCESS = "208 File fetched ";
const string DELETE_ALL_SUCCESS = "209 Deleted all\r\n";
const string GOT_ALL_FILES = "210 Got all files for drive";
const string MOVED_A_FILE_SUCCESS = "211 Moved a file for drive \r\n";
const string MOVED_A_FOLDER_SUCCESS = "212 Moved a folder for drive \r\n";
const string FIND_INBOX_SUCCESS = "213 Successfully found the inbox content for user.\r\n";
const string UPDATE_INBOX_SUCCESS = "214 Successfully update the inbox content for user.\r\n";
const string DELETE_EMAIL_SUCCESS = "215 Successfully delete the email.\r\n";
const string REPLY_EMAIL_SUCCESS = "216 Successfully reply the email.\r\n";
const string FORWARD_EMAIL_SUCCESS = "217 Successfully forward the email.\r\n";
const string CPUT_SUCCESS = "218 CPUT success.\r\n";

const string POP3_USER_SUCCESS = "+OK POP3 Successfully found the username.\r\n";
const string POP3_PASS_SUCCESS = "+OK POP3 Correct password.\r\n";
const string POP3_STAT_SUCCESS = "+OK POP3 Successfully found the username.\r\n";
const string POP3_UIDL_SUCCESS = "+OK POP3 Unique-id listing follows.\r\n";
const string POP3_RETR_SUCCESS = "+OK POP3 Message follows.\r\n";

const string BAD_SYNTAX_ERR = "400 Bad Syntax\r\n";
const string USER_EXISTED_ERR = "401 User already existed.\r\n";
const string USER_NOT_EXIST_ERR = "402 User does not exist.\r\n";
const string INVALID_PASSWORD_ERR = "403 Invalid password.\r\n";
const string UNKNOWN_COMMAND_ERR = "404 Syntax error, command unrecognized.\r\n";
const string ROW_NOT_FOUND = "405 Row not found.\r\n";
const string COLUMN_NOT_FOUND = "406 Column not found.\r\n";
const string INVALID_FILESIZE_ERR = "407 Error with PUT, the size of the value should not be 0.\r\n";
const string FIRST_COMPARISON_FAIL = "408 Error with CPUT, the first value did not match.\r\n";
const string READ_FAIL = "409 Error reading from the client.\r\n";
const string INVALID_FOLDER = "410 Invalid Folder.\r\n";

const string POP3_CANNOT_FIND_USER = "-ERR POP3 Cannot find user.\r\n";
const string POP3_PASS_FAILURE = "-ERR POP3 Incorrect password.\r\n";
const string POP3_UIDL_NO_SUCH_MESSAGE = "-ERR UIDL no such message.\r\n";
const string POP3_RETR_NO_SUCH_MESSAGE = "-ERR RETR no such message.\r\n";
const string POP3_LIST_NO_SUCH_MESSAGE = "-ERR LIST no such message.\r\n";

// global variable
unordered_map<std::string, unordered_map<std::string, vector<char>>> bigTable;
vector<int*> sockets;
vector<int> backendFDs;
vector<int> OKAYED_FDs; // keeps track of which replica responded with an OK message
vector<pthread_t> threads;
vector<Server> serverList;              // stores a list of server ip and port
map<string, int> port_to_FD;     // maps port number to FD
map<string, Request> uniqueID_to_response; // maps command unique ID to message response
int myIndex = 1;               // index of the current server in the server list
bool debug = true;
bool alive = true;
Server masterServer;
Server primaryServer;
int primaryFD;
string file1;
string file2;
bool needRevert = false; // whether we need to revert the write operation or not
int portForward; // the port number other servers talking to
int port;   // the port number this server is listening on
ofstream fs;    // log file
unordered_map<int, string> fd_username_map;
unordered_map<string, unordered_map<int, info_for_each_email>> user_email_info_map; // to store all emails for each user

bool do_write(int fd, char *buf, int len);
vector<string> split(string fullcommand, string delimiter);
bool checkKeyExist(int fd, string &row, string &column);
//void getOrDelete(int fd, bool isGet, string full_command);
void getAllFiles(int fd, string &full_command);
void deleteAllFiles(int fd, string &full_command);
void moveAFile(int fd, string &full_command);
void moveAFolder(int fd, string &full_command);
void registration(int fd, string &full_command);
void login(int fd, string &full_command);

#define A 54059 /* a prime */
#define B 76963 /* another prime */
#define C 86969 /* yet another prime */
#define FIRSTH 37 /* also prime */
unsigned hash_str(const char *s) {
	unsigned h = FIRSTH;
	while (*s) {
		h = (h * A) ^ (s[0] * B);
		s++;
	}
	return h; // or return h % C;
}

void processConfig(FILE *file) {
	char *line = NULL;
	size_t len = 0;
	int index = 1;
	// reading line by line
	while ((getline(&line, &len, file)) != -1) {
		string tempLine = line;
		Server s;
		if (index == myIndex) {
			string forward = tempLine.substr(0, tempLine.find(','));
			// set the portForward for myself
			portForward = stoi(
					forward.substr(forward.find(':') + 1,
							forward.length() - forward.find(':') - 1));
		}
		if (tempLine.find(",") != std::string::npos) {
			// we only need to keep track of each server's listening port
			string listening = tempLine.substr(tempLine.find(',') + 1,
					tempLine.length() - tempLine.find(','));
			s.ip = listening.substr(0, listening.find(':'));
			s.port = listening.substr(listening.find(':') + 1,
					listening.length() - listening.find(':') - 2);
			serverList.push_back(s);
		}
		index++;
	}
}

bool im_primary(string ip, string port) {
	if (primaryServer.ip == ip && primaryServer.port == port) {
		return true;
	}
	return false;
}

// from the slides
bool do_write(int fd, char *buf, int len) {
	int sent = 0;
	while (alive && sent < len) {
		int n = write(fd, &buf[sent], len - sent);
		if (n < 0) {
			return false;
		}
		sent += n;
	}
	return true;
}

bool do_read(int fd, char *buf, int len) {
	int rcvd = 0;
	while (rcvd < len) {
		int n = read(fd, &buf[rcvd], len - rcvd);
		if (n < 0) {
			return false;
		}
		rcvd += n;
	}
	string buffer = buf;
	cout << "at do read: " << buffer << endl;
	if (buffer.at(0) != '<') {
		// if it is not a message between backend nodes, add it to the log file
		fs << buffer;
		fs.flush();
	}
	return true;
}

// read until \r\n
bool first_read(int fd, char *buf, int size) {
	int rcvd = 0;
	string buffer = buf;
	while (buffer.find("\r\n") == std::string::npos) {
		int n = read(fd, &buf[rcvd], size);
		if (n < 0) {
			return false;
		}
		rcvd += n;
		buf[rcvd] = '\0';
		buffer = buf;
	}
	if (buffer.at(0) != '<' && buffer.substr(0, 4) != "PRIM") {
		cout << "at first read: " << buffer << endl;
		// if it is not a message between backend nodes, add it to the log file
		fs << buffer;
		fs.flush();
	}
	return true;
}

// split full command by the delimiter
vector<string> split(string fullcommand, string delimiter) {
	vector < string > result;
	while (fullcommand.size()) {
		int i = fullcommand.find(delimiter);
		if (i != string::npos) {
			result.push_back(fullcommand.substr(0, i));
			fullcommand = fullcommand.substr(i + delimiter.size());
		} else {
			result.push_back(fullcommand);
			break;
		}
	}
	return result;
}

bool checkKeyExist(int fd, string &row, string &column,
		bool should_write_back) {
	if (bigTable.find(row) == bigTable.end()) {
		if (should_write_back) {
			do_write(fd, (char*) ROW_NOT_FOUND.c_str(), ROW_NOT_FOUND.length());
		}
		return false;
	} else {
		if (bigTable[row].find(column) == bigTable[row].end()) {
			if (should_write_back) {
				do_write(fd, (char*) COLUMN_NOT_FOUND.c_str(),
						COLUMN_NOT_FOUND.length());
			}
			return false;
		} else {
			return true;
		}
	}
}

vector<std::string> split_string_with_delimiter(std::string input,
		std::string deli) {
	size_t start = 0;
	size_t end = 0;
	size_t delimiter_length = deli.length();
	std::string part;
	std::vector < std::string > ret;

	while (end = input.find(deli, start) != std::string::npos) {
		part = input.substr(start, end - start);
		start = end + delimiter_length;
		ret.push_back(part);
	}
	ret.push_back(input.substr(start));
	return ret;
}

void addBackendMsgPrefix(string &tempStore) {
	tempStore.append("<");
	tempStore.append(serverList[myIndex - 1].port);
	tempStore.append(" ");
}

// process delete or get
bool getOrDelete(int fd, bool isGet, string full_command, string unique_ID,
		bool toBackendNode, bool dont_send) {
	full_command = full_command.substr(full_command.find(' ') + 1);
	vector < string > variables = split(full_command, " ");
	string tempStore;
	if (toBackendNode) {
		addBackendMsgPrefix(tempStore);
	}
	if (variables.size() == 2) {
		string row = variables[0];
		string column = variables[1];
		if (checkKeyExist(fd, row, column, !toBackendNode)) {
			if (isGet) {
				// found the value, send it back
				string data_to_send(bigTable[row][column].begin(),
						bigTable[row][column].end());
				data_to_send.append("\r\n");
				do_write(fd, (char*) data_to_send.c_str(), data_to_send.size());
				return true;
			} else {
				// process delete
				// found the value, delete the value
				uniqueID_to_response[unique_ID].row = row;
				uniqueID_to_response[unique_ID].column = column;
				uniqueID_to_response[unique_ID].originalValue = bigTable[row][column];
				uniqueID_to_response[unique_ID].response = DELETION_SUCCESS;
				bigTable[row].erase(column);
				tempStore.append(DELETION_SUCCESS);
				if (toBackendNode) {
					tempStore.insert(tempStore.find("\r"), " " + unique_ID);
				}
				if (!dont_send) {
					do_write(fd, (char*) tempStore.c_str(), tempStore.length());
				}
				return true;
			}
		}
	} else {
		tempStore.append(BAD_SYNTAX_ERR);
		if (toBackendNode) {
			tempStore.insert(tempStore.find("\r"), " " + unique_ID);
		}
		do_write(fd, (char*) tempStore.c_str(), tempStore.size());
	}
	return false;
}

vector<string> getAllFilesAsVec(string username) {
	vector < string > output;
	for (auto i : bigTable[username]) {
		if (i.first[0] == 'F' || i.first[0] == 'D') {
			string filename = i.first;    // get rid of the prefix "T:" or "F:"
			output.emplace_back(filename);
		}
	}
	return output;
}
// return all files (with their timestamps) of a user
// format: GETALL username
void getAllFiles(int fd, string &full_command) {
	full_command = full_command.substr(full_command.find(' ') + 1);
	vector < string > variables = split(full_command, " ");
	string res;
	if (variables.size() == 1) {
		string username = variables[0];
		if (bigTable.find(username) == bigTable.end()) { // username doesn't exist
			res = BAD_SYNTAX_ERR;
		} else {
			string output = "";
			for (auto i : bigTable[username]) {
				if (i.first[0] == 'F' || i.first[0] == 'D') {    // timestamp
					string filename = i.first; // get rid of the prefix "T:" or "F:"
					output += filename + '\n';
				}
			}
			if (output.length() != 0) {
				output.pop_back();
			}
			output += "\r\n";  // replace the last \n with \r\n
			res = GOT_ALL_FILES + " " + output;
		}
	} else {
		res = BAD_SYNTAX_ERR;
	}
    cout << "result of get all: " <<  res << endl;
	do_write(fd, (char*) res.c_str(), res.size());
}

// move a file from a directory to another
// this also allows for file renaming
bool moveAFile(int fd, string &full_command, bool toBackendNode, bool not_write, string unique_ID) {
	full_command = full_command.substr(full_command.find(' ') + 1);
	vector < string > variables = split(full_command, " ");
	string res;
	if (toBackendNode) {
		addBackendMsgPrefix(res);
	}
	if (variables.size() == 3) {
		string username = variables[0];
		string path1 = variables[1];
		string path2 = variables[2];
		if (bigTable.find(username) == bigTable.end()) { // username doesn't exist
            uniqueID_to_response[unique_ID].response = BAD_SYNTAX_ERR;
			res.append(BAD_SYNTAX_ERR);
		} else {
			vector < string > output = getAllFilesAsVec(username);
			bool path1valid = false;
			bool path2valid = false;
			string columnFrom;
			for (int i = 0; i < output.size(); i++) {
				string existingFilePath = output[i];
				if (existingFilePath == path1) {
					path1valid = true;
					columnFrom = output[i];
				}

				string path2Folder = "D"
						+ path2.substr(1, path2.find_last_of("\/") - 1);
				if (existingFilePath == path2Folder || path2Folder == "D:root") {
					path2valid = true;
				}
			}

			if (path1valid == true && path2valid == true) {
				cout << "VALID PATHS" << endl;
				cout << variables[1] << endl;

				vector<char> data = bigTable[username][path1];
				vector<char> data2;
				for (int i = 0; i < data.size(); i++) {
					data2.emplace_back(data[i]);
				}
                uniqueID_to_response[unique_ID].row = username;
                uniqueID_to_response[unique_ID].column = path1;
                uniqueID_to_response[unique_ID].originalValue = bigTable[username][path1];
                uniqueID_to_response[unique_ID].response = MOVED_A_FILE_SUCCESS;

				bigTable[username][path2] = data2;
				bigTable[username].erase(path1);
				res.append(MOVED_A_FILE_SUCCESS);
				if (!not_write) {
                    cout << "response for move a file: " << res << endl;
                    if (toBackendNode) {
                        res.insert(res.find("\r"), " " + unique_ID);
                    }
					do_write(fd, (char*) res.c_str(), res.length());
				}
				return true;
			} else {
				res.append(INVALID_FOLDER);
			}
		}
	} else {
		res.append(BAD_SYNTAX_ERR);
	}
	if (!not_write) {
        if (toBackendNode) {
            res.insert(res.find("\r"), " " + unique_ID);
        }
		do_write(fd, (char*) res.c_str(), res.size());
        uniqueID_to_response[unique_ID].response = res;
	}
	return false;
}

// move a directory and its subdirectories from a directory to another
// this also allows for directory renaming
bool moveAFolder(int fd, string &full_command, bool toBackendNode, bool not_write, string unique_ID) {
	full_command = full_command.substr(full_command.find(' ') + 1);
	vector < string > variables = split(full_command, " ");
	string res;
	if (toBackendNode) {
		addBackendMsgPrefix(res);
	}
	if (variables.size() == 3) {
		string username = variables[0];
		string path1 = variables[1];
		string path2 = variables[2];
		if (bigTable.find(username) == bigTable.end()) { // username doesn't exist
			res.append(BAD_SYNTAX_ERR);
		} else {
			vector < string > output = getAllFilesAsVec(username);
			bool path1valid = false;
			bool path2valid = false;
			string columnFrom;
			string existingFilePath;
			string path2Folder = path2.substr(0, path2.find_last_of("\/"));
			for (int i = 0; i < output.size(); i++) {
				existingFilePath = output[i];
				cout << existingFilePath << " " << path2Folder << endl;
				if (existingFilePath == path1) {
					path1valid = true;
					columnFrom = output[i];
				}

                if (existingFilePath == path2Folder || path2Folder == "D:root") {
					path2valid = true;
				}
			}
			if (path1valid == true && path2valid == true) {
				cout << variables[1] << endl;

				vector < string > allUserFiles = getAllFilesAsVec(username);
				for (int i = 0; i < allUserFiles.size(); i++) {
					string btFile = allUserFiles[i];
					char type = btFile[0];
					if (type == 'D') {
						if (btFile.find(path1) != string::npos) { // user path is contained in file in bigtable. File is nested
							cout << "dir to transfer found" << endl;
							string folder = btFile.substr(path1.length());
							string newPath2 = path2 + folder;
							cout << "btfile: " << btFile << endl;
							cout << "newpath2: " << newPath2 << endl;
							vector<char> data;
							data.emplace_back('t');
							bigTable[username][newPath2] = data;
							bigTable[username].erase(btFile);
						}
					} else if (type == 'F') {
						string filePath1 = 'F' + path1.substr(1);
						string filePath2 = 'F' + path2.substr(1);
						if (btFile.find(filePath1) != string::npos) { // user path is contained in file in bigtable. File is nested
							cout << "file to transfer found" << endl;
							string filename = btFile.substr(filePath1.length());
							string newPath2 = filePath2 + filename;

							cout << "btfile: " << btFile << endl;
							cout << "newpath2: " << newPath2 << endl;

							vector<char> data = bigTable[username][btFile];
							bigTable[username][newPath2] = data;
							bigTable[username].erase(btFile);
						}
					}
				}
				vector<char> data = bigTable[username][columnFrom];
				vector<char> data2;
				for (int i = 0; i < data.size(); i++) {
					data2.emplace_back(data[i]);
				}
                uniqueID_to_response[unique_ID].row = username;
                uniqueID_to_response[unique_ID].column = columnFrom;
                uniqueID_to_response[unique_ID].originalValue = data2;
                uniqueID_to_response[unique_ID].response = MOVED_A_FOLDER_SUCCESS;
				bigTable[username][variables[1]] = data2;
				bigTable[username].erase(columnFrom);
				res.append(MOVED_A_FOLDER_SUCCESS);
				if (!not_write) {
                    cout << "response for moving a folder: " << res << endl;
                    if (toBackendNode) {
                        res.insert(res.find("\r"), " " + unique_ID);
                    }
					do_write(fd, (char*) res.c_str(),
							res.length());
				}
				return true;
			} else {
				res.append(INVALID_FOLDER);
			}
		}
	} else {
		res.append(BAD_SYNTAX_ERR);
	}
	if (!not_write) {
        if (toBackendNode) {
            res.insert(res.find("\r"), " " + unique_ID);
        }
        uniqueID_to_response[unique_ID].response = res;
		do_write(fd, (char*) res.c_str(), res.size());
	}
	return false;
}

// delete all files and folders of a user
// format: GETALL username
bool deleteAllFiles(int fd, string &full_command, bool toBackendNode, bool not_write, string unique_ID) {
	full_command = full_command.substr(full_command.find(' ') + 1);
	vector < string > variables = split(full_command, " ");
	string res;
	if (toBackendNode) {
		addBackendMsgPrefix(res);
	}
	if (variables.size() == 2) {
		string username = variables[0];
		string path = variables[1].substr(2);
		if (bigTable.find(username) == bigTable.end()) { // username doesn't exist
			res.append(BAD_SYNTAX_ERR);
		} else {
			cout << "user bigtable size: " << bigTable[username].size() << endl;
			unordered_map<std::string, vector<char>> userData = bigTable[username];
            uniqueID_to_response[unique_ID].row = username;
            uniqueID_to_response[unique_ID].response = DELETE_ALL_SUCCESS;
			for (auto i : userData) {
				cout << "i: " << i.first << endl;
				if (i.first[0] == 'F' || i.first[0] == 'D') {
					string origColumn = i.first;
					string column = i.first.substr(2);
					string folder = column.substr(0, column.find_last_of("\/"));
					cout << "path: " << path << " | column: " << column << endl;
					if (path == column || folder.find(path) != string::npos) {
						cout << "deleting " << origColumn << endl;
						bigTable[username].erase(origColumn);
						cout << "deleted" << endl;
					}
				}
			}
			res.append(DELETE_ALL_SUCCESS);
			if (!not_write) {
                if (toBackendNode) {
                    res.insert(res.find("\r"), " " + unique_ID);
                }
				do_write(fd, (char*) res.c_str(), res.length());
			}
			return true;
		}
	} else {
		res.append(BAD_SYNTAX_ERR);
	}
	if (!not_write) {
        uniqueID_to_response[unique_ID].response = res;
		do_write(fd, (char*) res.c_str(), res.size());
	}
	return false;
}

// process registration request
// format: REGISTER username password
bool registration(int fd, string &full_command, bool toBackendNode,
		string uniqueID, bool dont_send) {
    cout << "toBackendNode: " << toBackendNode <<endl;
    cout << "dont_send: " << dont_send <<endl;

    full_command = full_command.substr(full_command.find(' ') + 1);
	vector < string > variables = split(full_command, " ");
	string res;
	if (toBackendNode) {
		addBackendMsgPrefix(res);
	}
	bool ret = false;
	if (variables.size() == 2) {
		string username = variables[0];
		string password = variables[1];
		if (bigTable.find(username) == bigTable.end()) { // user not existed yet
			unordered_map<string, vector<char>> tmp;
			vector<char> passwordVec(password.begin(), password.end());
			bigTable[username] = tmp;
			bigTable[username]["password"] = passwordVec;
			uniqueID_to_response[uniqueID].row = username;
			uniqueID_to_response[uniqueID].column = "password";
			uniqueID_to_response[uniqueID].response = REGISTER_SUCCESS;
			res.append(REGISTER_SUCCESS);
			ret = true;
		} else {
			res.append(USER_EXISTED_ERR);
		}
	} else {
		res.append(BAD_SYNTAX_ERR);
	}
	if (toBackendNode) {
		res.insert(res.find("\r"), " " + uniqueID);
	}
	if (!dont_send) {
        cout << "should be here" << endl;
		do_write(fd, (char*) res.c_str(), res.size());
	}
	return ret;
}

// process login request
// format: LOGIN username password
void login(int fd, string &full_command) {
	full_command = full_command.substr(full_command.find(' ') + 1);
	vector < string > variables = split(full_command, " ");
	string res;
	if (variables.size() == 2) {
		string username = variables[0];
		string passwordInput = variables[1];
		if (bigTable.find(username) != bigTable.end()) {    // user exist
			string passwordReal(bigTable[username]["password"].begin(),
					bigTable[username]["password"].end());
			if (passwordInput == passwordReal) {
				string extend_msg = LOGIN_SUCCESS + username + "\r\n";
				res = extend_msg;
			} else {
				res = INVALID_PASSWORD_ERR;
			}
		} else {
			res = USER_NOT_EXIST_ERR;
		}
	} else {
		res = BAD_SYNTAX_ERR;
	}
	do_write(fd, (char*) res.c_str(), res.size());
}

// process PUT request
bool processPUT(int fd, string &full_command, bool dont_write_yet, string unique_ID, bool toBackendNode, bool secondTime) {
	string temp = full_command.substr(full_command.find(' ') + 1);
	vector < string > variables = split(temp, " ");
    string res;
    string tempStore;
	if (toBackendNode) {
		addBackendMsgPrefix(res);
        addBackendMsgPrefix(tempStore);
	}
	if (variables.size() == 3) {
		string row = variables[0];
		string column = variables[1];
		string size_string = variables[2];
        vector<string> output = getAllFilesAsVec(row);
        bool isValid = false;
        output.emplace_back("D:root");
        if (column[0] == 'D') {
            string matchingFolder = column.substr(0, column.find_last_of("\/"));
            for (int i=0; i<output.size(); i++) {
                if (output[i] == matchingFolder) {
                    isValid = true;
                }
            }
            if (!isValid) {
                res.append(INVALID_FOLDER);
                if (!dont_write_yet) {
                    do_write(fd, (char*) res.c_str(), res.length());
                }
                return false;
            }
        } else {
            string matchingFolder = 'D' + column.substr(1, column.find_last_of("\/")-1);
            for (int i=0; i<output.size(); i++) {
                if (output[i] == matchingFolder) {
                    isValid = true;
                }
            }
            if (!isValid) {
                res.append(INVALID_FOLDER);
                if (!dont_write_yet) {
                    do_write(fd, (char*) res.c_str(), res.length());
                }
                return false;
            }
        }
		try {
			int fileSize = stoi(size_string);
			if (fileSize == 0) {
				string tempStore2 = tempStore;
				tempStore2.append(INVALID_FILESIZE_ERR);
				if (toBackendNode && im_primary(serverList[myIndex - 1].ip,serverList[myIndex - 1].port)) {
					tempStore2.insert(tempStore2.find("\r"), " " + unique_ID);
					do_write(fd, (char*) tempStore2.c_str(),tempStore2.length());
				}
				do_write(fd, (char*) tempStore2.c_str(), tempStore2.length());
			} else {
				string tempStore2 = tempStore;
				tempStore2.append(ACCEPTED_MORE_ACTION);
				if (toBackendNode) {
					tempStore2.insert(tempStore2.find("\r"), " " + unique_ID);
				}
				if (!secondTime) {
					do_write(fd, (char*) tempStore2.c_str(),
							tempStore2.length());
				}
				// read until the size, which is specified by the client
				char *incoming_File = new char[fileSize];
				// if I already have the file, then don't ask for the file, just execute PUT
				// this means I am at the stage where I am the primary and I am executing the command after multicasting
				if (im_primary(serverList[myIndex - 1].ip,
						serverList[myIndex - 1].port) && file1.length() > 0) {
					std::vector<char> charToAdd(file1.begin(), file1.end());
					bigTable[row][column] = charToAdd;
					uniqueID_to_response[unique_ID].row = row;
					uniqueID_to_response[unique_ID].column = column;
					uniqueID_to_response[unique_ID].response =
							SUCCESS_INSERTION;
					// if I am a primary, executing it myself, I don't need to respond to anyone
					return true;
				} else if (do_read(fd, incoming_File, fileSize)) {
					// If I don't have the file yet, it means that either I am asking the client for the file
					// or I am waiting for the other backend node to voluntarily send me the file
					if (dont_write_yet) {
						// Not execute it yet, because I am either receiving it from the client
						// or receiving it from a backend node as the primary node
						file1 = incoming_File;
					} else {
						// I will execute it right away because I am the replica receiving the command from the primary
						std::vector<char> charToAdd(incoming_File,
								incoming_File + fileSize);
						bigTable[row][column] = charToAdd;
						uniqueID_to_response[unique_ID].row = row;
						uniqueID_to_response[unique_ID].column = column;
						uniqueID_to_response[unique_ID].response = SUCCESS_INSERTION;
						tempStore.append(SUCCESS_INSERTION);
						if (toBackendNode) {
							tempStore.insert(tempStore.find("\r"),
									" " + unique_ID);
						}
						do_write(fd, (char*) tempStore.c_str(),
								tempStore.length());
					}
					return true;
				} else {
					tempStore.append(READ_FAIL);
					if (toBackendNode) {
						tempStore.insert(tempStore.find("\r"), " " + unique_ID);
					}
					do_write(fd, (char*) tempStore.c_str(), tempStore.size());
				}
			}
		} catch (exception e) {
			tempStore.append(INVALID_FILESIZE_ERR);
			if (toBackendNode) {
				tempStore.insert(tempStore.find("\r"), " " + unique_ID);
			}
			do_write(fd, (char*) tempStore.c_str(), tempStore.size());
		}
	} else {
		tempStore.append(BAD_SYNTAX_ERR);
		if (toBackendNode) {
			tempStore.insert(tempStore.find("\r"), " " + unique_ID);
		}
		do_write(fd, (char*) tempStore.c_str(), tempStore.size());
	}
	return false;
}

void mail_get_inbox(int fd, string &full_command) {
	cout << "mail_get_inbox = " << full_command << endl;
	full_command = full_command.substr(full_command.find(' ') + 1);
	vector < string > variables = split(full_command, " ");
	string res;
	string inbox;
	if (variables.size() == 1) {
		string username = variables[0];
		if (bigTable.find(username) != bigTable.end()) {    // user exist
			string inbox(bigTable[username]["mailbox"].begin(),
					bigTable[username]["mailbox"].end());
			string extend_msg = FIND_INBOX_SUCCESS + inbox;
			res = extend_msg;
		} else {
			res = USER_NOT_EXIST_ERR;
		}
	} else {
		res = BAD_SYNTAX_ERR;
	}
	cout << "Backend mail_get_mail response = " << res << endl;
	do_write(fd, (char*) res.c_str(), res.size());
}

bool mail_send_mail(int fd, string &full_command, bool toBackendNode, bool not_write, string unique_ID) {
	full_command = full_command.substr(full_command.find(' ') + 1);

	vector < string > variables = split(full_command, "|");
	string sender = variables[0];
	string recipient = variables[1];
	string subject = variables[2];
	string send_time = variables[3];
	string content = variables[4];

	cout << "sender = " << sender << endl;
	cout << "recipient = " << recipient << endl;
	cout << "subject = " << subject << endl;
	cout << "send_time = " << send_time << endl;
	cout << "content = " << content << endl;
	//TODO: verify the user

	// update the recipient mailbox
	string recipient_inbox(bigTable[recipient]["mailbox"].begin(),
			bigTable[recipient]["mailbox"].end());

	string added_content = "From <" + sender + "> " + send_time + "\n";
	added_content += "From: " + sender + " <" + sender + "> " + "\n";
	added_content += "To: " + recipient + " <" + recipient + ">\n";
	added_content += "Date: " + send_time + "\n";
	added_content += "Subject: " + subject + "\n";
	added_content += "\n";
	added_content += content + "\n";
	cout << "added_content = " << added_content << endl;

	recipient_inbox += added_content;

	// update cell
	std::vector<char> v(recipient_inbox.begin(), recipient_inbox.end());
	bigTable[recipient]["mailbox"] = v;
    uniqueID_to_response[unique_ID].row = recipient;
    uniqueID_to_response[unique_ID].column = "mailbox";
    uniqueID_to_response[unique_ID].response = UPDATE_INBOX_SUCCESS;
	cout << "finish bigtable inbox cell update" << endl;

	string res;
	if (toBackendNode) {
		addBackendMsgPrefix(res);
	}

	if (!not_write) {
        res.append(UPDATE_INBOX_SUCCESS);
        if (toBackendNode) {
            res.insert(res.find("\r"), " " + unique_ID);
        }
        cout << "sending out from mail send: " << res << endl;
        do_write(fd, (char*) res.c_str(), res.size());
	}
	return true;
}

void computeDigest(char *data, int dataLengthBytes,
		unsigned char *digestBuffer) {
	/* The digest will be written to digestBuffer, which must be at least MD5_DIGEST_LENGTH bytes long */

	MD5_CTX c;
	MD5_Init(&c);
	MD5_Update(&c, data, dataLengthBytes);
	MD5_Final(digestBuffer, &c);
}

std::string covert_md5_hash_to_hex_string(unsigned char *hashed_char) {
	std::string ret;
	for (size_t i = 0; i != 16; ++i) {
		ret += "0123456789ABCDEF"[hashed_char[i] / 16];
		ret += "0123456789ABCDEF"[hashed_char[i] % 16];
	}
	return ret;
}

void read_mbox(string &username) {
	string inbox_content(bigTable[username]["mailbox"].begin(),
			bigTable[username]["mailbox"].end());
	cout << "inbo_content = " << inbox_content << endl;

	int from_pos = inbox_content.find("From ");
	int return_pos;
	int begin, end;
	std::string first_line;
	int id = 1;
	unordered_map<int, info_for_each_email> id_email_info_map;

	/**
	 *  From <benjamin.franklin@localhost> Mon Oct 17 07:07:41 2022
	 From: Benjamin Franklin <benjamin.franklin@localhost>
	 To: Linh Thi Xuan Phan <linhphan@localhost>
	 Date: Fri, 21 Oct 2016 18:29:11 -0400
	 Subject: Testing my new email account
	 From <benjamin.franklin@localhost> Mon Oct 17 07:07:41 2022
	 From: Benjamin Franklin <benjamin.franklin@localhost>
	 To: Linh Thi Xuan Phan <linhphan@localhost>
	 Date: Fri, 21 Oct 2016 18:29:11 -0400
	 Subject: Testing my new email account
	 */

	while (from_pos != std::string::npos) {
		begin = from_pos;
		return_pos = inbox_content.find("\n", begin);
		first_line = inbox_content.substr(from_pos, return_pos - begin + 1);
		from_pos = inbox_content.find("From ", return_pos); // find the next From, starting from "\n"

		begin = return_pos + 1;
		end = from_pos - 1;

		std::string content = inbox_content.substr(begin, end - begin + 1);
		std::cout << content << std::endl;

		info_for_each_email info_each_email;

		strcpy(info_each_email.username, username.c_str());
		info_each_email.first_line = first_line;
		cout << "first_line = " << first_line << endl;
		info_each_email.content = content;
		cout << "content = " << content << endl;

		std::string entire_message = first_line + content;
		char *entire_message_char = (char*) entire_message.c_str();
		cout << "entire_message_char = " << entire_message_char << endl;

		unsigned char digestBuffer[MD5_DIGEST_LENGTH];
		computeDigest(entire_message_char, strlen(entire_message_char),
				digestBuffer);
		std::string hex_string_uid = covert_md5_hash_to_hex_string(
				digestBuffer);

		info_each_email.hex_string_uid = hex_string_uid;
		cout << "id = " << id << " UID = " << hex_string_uid << endl;
		id_email_info_map.insert(
				std::pair<int, info_for_each_email>(id, info_each_email));
		id++;
	}

	user_email_info_map.insert(
			std::pair<string, unordered_map<int, info_for_each_email>>(username,
					id_email_info_map));
}

void find_user_email_info_with_fd(int fd,
		unordered_map<int, info_for_each_email> &this_user_email_info) {
	string username;
	string res;

	auto itor = fd_username_map.find(fd);
	if (itor != fd_username_map.end()) {
		username = itor->second;
	} else {
		res = POP3_CANNOT_FIND_USER;
		do_write(fd, (char*) res.c_str(), res.size());
		return;
	}

	auto itor2 = user_email_info_map.find(username);
	if (itor2 != user_email_info_map.end()) {
		this_user_email_info = itor2->second;
	} else {
		res = POP3_CANNOT_FIND_USER;
		do_write(fd, (char*) res.c_str(), res.size());
		return;
	}
}

// TODO delete mail by index!
bool mail_delete_mail(int fd, string &full_command, bool toBackendNode, bool dont_write, string unique_ID) {
	vector < string > variables = split(full_command, " ");
	int index = stoi(variables[0]);
	// TODO: update username
	string username = variables[1];
	string res;
	if (toBackendNode) {
		addBackendMsgPrefix(res);
	}
	string inbox_content(bigTable[username]["mailbox"].begin(),
			bigTable[username]["mailbox"].end());

	// parse inbox_content
	int from_pos = inbox_content.find("From ");
	int return_pos;
	int begin, end;
	string first_line;
	string new_inbox_content;
	int id = 0;

	while (from_pos != std::string::npos) {
		begin = from_pos;
		return_pos = inbox_content.find("\n", begin);
		first_line = inbox_content.substr(from_pos, return_pos - begin + 1);
		from_pos = inbox_content.find("From ", return_pos); // find the next From, starting from "\n"

		begin = return_pos + 1;
		end = from_pos - 1;

		std::string content = inbox_content.substr(begin, end - begin + 1);
		std::string entire_message = first_line + content;
		if (id != index) {
			new_inbox_content += entire_message;
		}
		id++;
	}

    uniqueID_to_response[unique_ID].row = username;
    uniqueID_to_response[unique_ID].column = "mailbox";
    uniqueID_to_response[unique_ID].response = DELETE_EMAIL_SUCCESS;
	// update cell
	std::vector<char> v(new_inbox_content.begin(), new_inbox_content.end());
	bigTable[username]["mailbox"] = v;
	cout << "finish bigtable inbox cell update" << endl;

	res.append(DELETE_EMAIL_SUCCESS + new_inbox_content);
	if (!dont_write) {
        if (toBackendNode) {
            res.insert(res.find("\r"), " " + unique_ID);
        }
		do_write(fd, (char*) res.c_str(), res.size());
	}
	return true;
}

bool mail_reply_mail(int fd, string &buffer, bool toBackendNode, bool dont_write, string unique_ID) {
	/*
	 * REPLYMAIL 0 username
	 hello
	 */

	// TODO find the user who is viewing the inbox page
	string res;
	if (toBackendNode) {
		addBackendMsgPrefix(res);
	}
	cout << "full_command = " << buffer << endl;
	int pos1 = buffer.find(" ");
	int pos2 = buffer.find(" ", pos1 + 1);
	int pos3 = buffer.find(" ", pos2 + 1);

	vector < string > variables = split(buffer.substr(0, pos3), " ");
	int index = stoi(variables[1]);
	string username = variables[2];
	string reply_content = buffer.substr(pos3 + 1);
	cout << "index = " << index << endl;
	cout << "username = " << username << endl;
	cout << "reply_content = " << reply_content << endl;
	int id = 0;
	string inbox_content(bigTable[username]["mailbox"].begin(),
			bigTable[username]["mailbox"].end());
	// parse inbox_content
	int from_pos = inbox_content.find("From ");
	int begin, end;
	int subject_pos;
	string first_line;
	int return_pos;

	while (from_pos != std::string::npos) {
		begin = from_pos;
		return_pos = inbox_content.find("\n", begin);
		first_line = inbox_content.substr(from_pos, return_pos - begin + 1);
		from_pos = inbox_content.find("From ", return_pos); // find the next From, starting from "\n"

		begin = return_pos + 1;
		end = from_pos - 1;

		if (index == id) {
			cout << "first_line = " << first_line << endl;

			// parse sender from the first line
			int left_bracket_pos = first_line.find("<");
			int right_bracket_pos = first_line.find(">");
			string reply_to = first_line.substr(left_bracket_pos + 1,
					right_bracket_pos - left_bracket_pos - 1);
			cout << "reply_to = " << reply_to << endl;
			string sender_inbox_content(bigTable[reply_to]["mailbox"].begin(),
					bigTable[reply_to]["mailbox"].end());

			// TODO change subject
			string subject = "Reply";

			// generate current time
			auto time1 = std::chrono::system_clock::now();
			time_t time2 = std::chrono::system_clock::to_time_t(time1);
			char *current_time = std::ctime(&time2);
			string current_time_string = current_time;
			// remove the "/n" at the end
			current_time_string = current_time_string.substr(0,
					current_time_string.size() - 1);

			string added_content = "From <" + username + "> "
					+ current_time_string + "\n";
			added_content += "From: " + username + " <" + username + "> "
					+ "\n";
			added_content += "To: " + reply_to + " <" + reply_to + ">\n";
			added_content += "Date: " + current_time_string + "\n";
			added_content += "Subject: " + subject + "\n";
			added_content += reply_content + "\n";
			cout << "added_content = " << added_content << endl;

			sender_inbox_content += added_content;

            uniqueID_to_response[unique_ID].row = reply_to;
            uniqueID_to_response[unique_ID].column = "mailbox";
            uniqueID_to_response[unique_ID].response = REPLY_EMAIL_SUCCESS;

			// update cell
			std::vector<char> v(sender_inbox_content.begin(),
					sender_inbox_content.end());
			bigTable[reply_to]["mailbox"] = v;
			cout << "finish bigtable inbox cell update" << endl;

			string new_inbox_content(bigTable[username]["mailbox"].begin(),
					bigTable[username]["mailbox"].end());
			res.append(REPLY_EMAIL_SUCCESS + new_inbox_content);
			if (!dont_write) {
                if (toBackendNode) {
                    res.insert(res.find("\r"), " " + unique_ID);
                }
				do_write(fd, (char*) res.c_str(), res.size());
			}
			break;
		}
		id++;
	}
	return true;

}


bool mail_forward_mail(int fd, string &buffer, bool toBackendNode, bool dont_write, string unique_ID) {
	// 1) find the entire message based on index
	// 2) change the sender
	// 2) attach the entire message to the recipient mbox

	/*
	 * FORWARDMAIL 0 username username_to_forward_to
	 */

	cout << "full_command = " << buffer << endl;
	vector < string > variables = split(buffer, " ");
	int index = stoi(variables[1]);
	string username = variables[2];
	string forward_address = variables[3];
	cout << "index = " << index << endl;
	cout << "username = " << username << endl;
	cout << "forward_address = " << forward_address << endl;
	int id = 0;
	string res;
	if (toBackendNode) {
		addBackendMsgPrefix(res);
	}
	string inbox_content(bigTable[username]["mailbox"].begin(),
			bigTable[username]["mailbox"].end());
	// parse inbox_content
	int from_pos = inbox_content.find("From ");
	int begin, end;
	int subject_pos;
	int return_pos;
	string first_line;
	string mail_to_forward;

	/**
	 * From <benjamin.franklin@localhost> Mon Oct 17 07:07:41 2022
	 From: Benjamin Franklin <benjamin.franklin@localhost>
	 To: Linh Thi Xuan Phan <linhphan@localhost>
	 Date: Fri, 21 Oct 2016 18:29:11 -0400
	 Subject: Testing my new email account
	 */

	while (from_pos != std::string::npos) {
		begin = from_pos;
		return_pos = inbox_content.find("\n", begin);
		first_line = inbox_content.substr(from_pos, return_pos - begin + 1);
		from_pos = inbox_content.find("From ", return_pos); // find the next From, starting from "\n"

		begin = return_pos + 1;
		end = from_pos - 1;

		std::string content = inbox_content.substr(begin, end - begin + 1);
		std::string entire_message = first_line + content;
		if (id == index) {
			mail_to_forward = entire_message;
			break;
		}
		id++;
	}

	string recipient_inbox_content(bigTable[forward_address]["mailbox"].begin(),
			bigTable[forward_address]["mailbox"].end());
	recipient_inbox_content += mail_to_forward;

    uniqueID_to_response[unique_ID].row = forward_address;
    uniqueID_to_response[unique_ID].column = "mailbox";
    uniqueID_to_response[unique_ID].response = FORWARD_EMAIL_SUCCESS;
	// update cell
	std::vector<char> v(recipient_inbox_content.begin(),
			recipient_inbox_content.end());
	bigTable[forward_address]["mailbox"] = v;
	cout << "finish bigtable inbox cell update" << endl;

	// return Success message + new inbox
	string new_inbox_content(bigTable[username]["mailbox"].begin(),
			bigTable[username]["mailbox"].end());
	res.append(FORWARD_EMAIL_SUCCESS + new_inbox_content);
	if (!dont_write) {
        cout << "forward email response: " << res << endl;
        if (toBackendNode) {
            res.insert(res.find("\r"), " " + unique_ID);
        }
		do_write(fd, (char*) res.c_str(), res.size());
	}
	return true;
}

bool pop3_dele(int fd, string &full_command) {
	cout << "pop3_dele = " << full_command << endl;

	// RETR, which retrieves a particular message;

	string username;
	bool ret;
	string res;

	unordered_map<int, info_for_each_email> this_user_email_info;

	if (full_command == "DELE\r\n" || full_command == "DELE \r\n") {
		res = UNKNOWN_COMMAND_ERR;
		do_write(fd, (char*) res.c_str(), res.size());
		return false;
	}

	find_user_email_info_with_fd(fd, this_user_email_info);

	string arguments_substring = full_command.substr(5);
	vector < std::string > all_arguments = split_string_with_delimiter(
			arguments_substring, " ");
	int id_to_find = std::stoi(all_arguments.at(0));

	// validate delete status
	auto itor = this_user_email_info.find(id_to_find);
	// if we can find the id
	if (itor != this_user_email_info.end()) {
		if (itor->second.delete_status == false) { // email not deleted yet
			itor->second.delete_status = true;

			//std::stringstream resp_to_build;
			res = res + "+OK message " + to_string(id_to_find) + " deleted\r\n";

			ret = true;
		} else {
            res = res + "-ERR message " + to_string(id_to_find) + " is already deleted\r\n";

			ret = false;
		}
	}

    do_write(fd, (char*)res.c_str(), res.size());

	return ret;
}

void pop3_user(int fd, string &full_command) {

	full_command = full_command.substr(full_command.find(' ') + 1);
	vector < string > variables = split(full_command, " ");
	string res;
	if (variables.size() == 1) {
		string username = variables[0];
		if (bigTable.find(username) != bigTable.end()) {    // user exist
				// store ip:port info of this client
			if (fd_username_map.find(fd) == fd_username_map.end()) {
				fd_username_map.insert(std::pair<int, string>(fd, username));
			}
			read_mbox(username);
			string extend_msg = POP3_USER_SUCCESS;
			res = extend_msg;
		} else {
			res = POP3_CANNOT_FIND_USER;
		}
	} else {
		res = BAD_SYNTAX_ERR;
	}
	do_write(fd, (char*) res.c_str(), res.size());
}

vector<string> get_row_col_content() {
    vector < string > output;
    for (auto &i : bigTable) {
        string row = i.first;
        cout << "row: " << row << endl;
        for (auto &j : i.second) {
            string col = j.first;
            string content(j.second.begin(), j.second.end());
            output.push_back(row);
            output.push_back(col);
            output.push_back(content);
        }
    }
    return output;
}

void admin_storage(int fd, string &full_command) {
    vector<string> vec = get_row_col_content();
    string res = "299 |";
    for (int i = 0; i < vec.size(); i++) {
        res += vec.at(i);
        res += "|";
    }
    res += "\r\n";
    cout << "admin_storage = " << res << endl;
    do_write(fd, (char*) res.c_str(), res.size());
}

bool processCPUT(int fd, string &full_command, bool dont_send, string uniqueID,
		bool toBackendNode) {
	full_command = full_command.substr(full_command.find(' ') + 1);
	vector < string > variables = split(full_command, " ");
	string tempStore;
	if (toBackendNode) {
		addBackendMsgPrefix(tempStore);
	}
	if (variables.size() == 4) {
		string row = variables[0];
		string column = variables[1];
		string val1 = variables[2];
		string val2 = variables[3];
		if (checkKeyExist(fd, row, column, !toBackendNode)) {
			string valReal(bigTable[row][column].begin(),
					bigTable[row][column].end());
			if (val1 == valReal) {
				uniqueID_to_response[uniqueID].row = row;
				uniqueID_to_response[uniqueID].column = column;
				uniqueID_to_response[uniqueID].response = REPLACE_SUCCESS;
				uniqueID_to_response[uniqueID].originalValue =
						bigTable[row][column];
				vector<char> vec(val2.begin(), val2.end());
				bigTable[row][column] = vec;
				tempStore.append(CPUT_SUCCESS);
				if (toBackendNode) {
					tempStore.insert(tempStore.find("\r"), " " + uniqueID);
				}
				if (!dont_send) {
                    cout << "CPUT response: " << tempStore << endl;
					do_write(fd, (char*) tempStore.c_str(), tempStore.length());
				}
				return true;
			} else {
				tempStore.append(FIRST_COMPARISON_FAIL);
				if (toBackendNode) {
					tempStore.insert(tempStore.find("\r"), " " + uniqueID);
				}
				do_write(fd, (char*) tempStore.c_str(), tempStore.length());
				return false;
			}
		} else {
            return false;
        }
	} else {
		tempStore.append(BAD_SYNTAX_ERR);
		if (toBackendNode) {
			tempStore.insert(tempStore.find("\r"), " " + uniqueID);
		}
		do_write(fd, (char*) tempStore.c_str(), tempStore.length());
		return false;
	}
}

bool processWriteOperations(int fd, string &command, string &full_command, string uniqueID, bool toBackendNode, bool secondTime) {
	if (command == "PUT") {
		cout << "processWriteOperations PUT" << endl;
		return processPUT(fd, full_command, false, uniqueID, toBackendNode, secondTime);
	} else if (command == "CPUT") {
		return processCPUT(fd, full_command, secondTime, uniqueID, toBackendNode);
	} else if (command == "DELETE") {
		return getOrDelete(fd, false, full_command, uniqueID, toBackendNode, secondTime);
	} else if (command == "REGISTER") {
		return registration(fd, full_command, toBackendNode, uniqueID, secondTime);
	} else if (command == "MOVE") {
		return moveAFile(fd, full_command, toBackendNode, secondTime, uniqueID);
	} else if (command == "MOVEDIR") {
		return moveAFolder(fd, full_command, toBackendNode, secondTime, uniqueID);
	} else if (command == "DELETEALL") {
		return deleteAllFiles(fd, full_command, toBackendNode, secondTime, uniqueID);
	} else if (command == "SENDMAIL") {
		return mail_send_mail(fd, full_command, toBackendNode, secondTime, uniqueID);
	} else if (command == "DELETEMAIL") {
		return mail_delete_mail(fd, full_command, toBackendNode, secondTime, uniqueID);
	} else if (command == "REPLYMAIL") {
		return mail_reply_mail(fd, full_command, toBackendNode, secondTime, uniqueID);
	} else if (command == "FORWARDMAIL") {
		return mail_forward_mail(fd, full_command, toBackendNode, secondTime, uniqueID);
	}
}

void pop3_pass(int fd, string &full_command) {
	cout << "pop3_pass = " << full_command << endl;
	string username;
	string res;

	auto itor = fd_username_map.find(fd);
	if (itor != fd_username_map.end()) {
		username = itor->second;
	} else {
		res = POP3_CANNOT_FIND_USER;
		do_write(fd, (char*) res.c_str(), res.size());
		return;
	}

	full_command = full_command.substr(full_command.find(' ') + 1);
	vector < string > variables = split(full_command, " ");
	string password_in_big_table;
	if (variables.size() == 1) {
		string password = variables[0];
		if (bigTable.find(username) != bigTable.end()) {    // user exist

			string password_in_big_table(bigTable[username]["password"].begin(),
					bigTable[username]["password"].end());
			if (password == password_in_big_table) {
				string extend_msg = POP3_PASS_SUCCESS;
				res = extend_msg;
			} else {
				res = POP3_PASS_FAILURE;
			}
		} else {
			res = POP3_CANNOT_FIND_USER;
		}
	} else {
		res = BAD_SYNTAX_ERR;
	}
	do_write(fd, (char*) res.c_str(), res.size());
}

void pop3_stat(int fd, string &full_command) {
	cout << "pop3_stat = " << full_command << endl;

	// full_command = full_command.substr(full_command.find(' ') + 1);
	// vector <string> variables = split(full_command, " ");
	string username;
	string res;
	unordered_map<int, info_for_each_email> this_user_email_info;

	find_user_email_info_with_fd(fd, this_user_email_info);

	int num_of_email = 0;
	int all_email_size = 0;

	for (int j = 0; j < this_user_email_info.size(); j++) {
		auto itor = this_user_email_info.find(j + 1);
		if (itor->second.delete_status == false) {
			all_email_size += itor->second.content.size();
			num_of_email++;
		}
	}
	//std::sprintf(resp_to_build, "+OK %d %d\r\n", num_of_email, all_email_size);
	res = "+OK " + to_string(num_of_email) + " " + to_string(all_email_size)
			+ "\r\n";
	do_write(fd, (char*) res.c_str(), res.size());
	cout << "finish writing stat" << endl;
}

void pop3_uidl(int fd, string &full_command) {
	cout << "pop3_uidl = " << full_command << endl;

	// UIDL, which shows a list of messages, along with a unique ID for each message;
	string username;
	string res;
	char *resp;
	unordered_map<int, info_for_each_email> this_user_email_info;

	find_user_email_info_with_fd(fd, this_user_email_info);

	// UIDL all emails
	if (full_command.substr(0, 4) == "UIDL" && full_command.size() < 5) {
		res = "+OK\r\n";
		cout << "res to write = " << res << endl;
		write(fd, (char*) res.c_str(), res.size());

		for (int j = 0; j < this_user_email_info.size(); j++) {
			// char* resp_to_build;
			auto itor = this_user_email_info.find(j + 1);

			if (itor != this_user_email_info.end()
					&& itor->second.delete_status == false) {
				std::string hashcode = itor->second.hex_string_uid;
				res = to_string(j + 1) + " " + hashcode.c_str() + "\r\n";
				cout << "res to write = " << res << endl;
				write(fd, (char*) res.c_str(), res.size());

			}
		}
		res = ".\r\n";
		cout << "res to write = " << res << endl;
		write(fd, (char*) res.c_str(), res.size());
		return;
	}

	// the id is specified. first detect the number of arguments
	std::string arguments_substring = full_command.substr(5);
	std::vector < std::string > all_arguments = split_string_with_delimiter(
			arguments_substring, " ");
	if (all_arguments.size() == 1) {

		// extract the id to be found
		int id_to_find = std::stoi(all_arguments.at(0));

		auto itor = this_user_email_info.find(id_to_find);
		// if we can find the id
		if (itor != this_user_email_info.end()) {
//            res ="+OK\r\n";
//            do_write(fd, (char*) res.c_str(), res.size());

			std::string hashcode = itor->second.hex_string_uid;
			//std::sprintf(resp_to_build, "%d %s\r\n", id_to_find, hashcode.c_str());

			res = "+OK " + to_string(id_to_find) + " " + hashcode.c_str()
					+ "\r\n";
		} else { // if we can't find the id
			res = POP3_RETR_NO_SUCH_MESSAGE;
		}

	} else { // more than one argument
		res = BAD_SYNTAX_ERR;
	}
	do_write(fd, (char*) res.c_str(), res.size());
}

void pop3_retr(int fd, string &full_command) {
	cout << "pop3_retr = " << full_command << endl;

	// RETR, which retrieves a particular message;
	string username;
	string res;
	unordered_map<int, info_for_each_email> this_user_email_info;
	find_user_email_info_with_fd(fd, this_user_email_info);

	// first detect the number of arguments
	std::string arguments_substring = full_command.substr(5);
	std::vector < std::string > all_arguments = split_string_with_delimiter(
			arguments_substring, " ");
	if (all_arguments.size() == 1) {

		// extract the id to be found
		int id_to_find = std::stoi(all_arguments.at(0));

		// validate delete status
		auto itor = this_user_email_info.find(id_to_find);
		// if we can find the id
		if (itor != this_user_email_info.end()) {

			if (itor->second.delete_status == false) {

				// write +OK response first
				res = "+OK\r\n";
				do_write(fd, (char*) res.c_str(), res.size());

				std::string message = itor->second.content;
				res = message + ".\r\n";
				cout << "res = " << res << endl;

				do_write(fd, (char*) res.c_str(), res.size());
				return;
			} else { // the email's delete_status is true
				res = "-ERR message has been deleted.\r\n";
			}
		} else { // if we can't find the id
			res = POP3_RETR_NO_SUCH_MESSAGE;
		}
	} else {
		res = UNKNOWN_COMMAND_ERR;
	}
	do_write(fd, (char*) res.c_str(), res.size());
}

void revert(string uniqueID, string command) {
	Request r = uniqueID_to_response[uniqueID];
	if (command == "PUT") {
		bigTable[r.row].erase(r.column);
	} else if (command == "CPUT" || command == "DELETE") {
		bigTable[r.row][r.column] = r.originalValue;
	} else {
		// reverting register operation should remove the entire row
		bigTable.erase(r.row);
	}
}

// This is what primary node will do after receiving a write operation
void primaryNodeAction(int fd, string command, string full_command,
		string uniqueID, bool toBackendNode, bool secondTime) {
	cout << "full_command at the beginning: " << full_command << endl;
	string originalCommand = full_command;

	string prefix = "<";
	prefix.append(serverList[myIndex - 1].port);
	prefix.append(" ");
	full_command.insert(0, prefix);
	full_command.append(uniqueID);
	full_command.append("\r\n");
	// multicast to all the replicas
	for (int i = 0; i < backendFDs.size(); i++) {
		int back_fd = backendFDs[i];
		cout << "backend fd: " << back_fd << endl;
		cout << "full_command multicasting from primary: " << full_command
				<< endl;
		do_write(back_fd, (char*) full_command.c_str(), full_command.length());
	}

	// execute the command myself, without sending messages through socket
	bool write_status = processWriteOperations(fd, command, originalCommand,
			uniqueID, toBackendNode, secondTime);
	needRevert = !write_status;
//	if (needRevert) {
//		string revertMsg = "<";
//		revertMsg.append(serverList[myIndex - 1].port);
//		revertMsg.append(" REVERT ");
//		revertMsg.append(uniqueID);
//		revertMsg.append("\r\n");
//		// tell every replica who has done it to revert the operation
//		for (int i = 0; i < OKAYED_FDs.size(); i++) {
//			do_write(OKAYED_FDs[i], (char*) revertMsg.c_str(),
//					revertMsg.length());
//		}
//	}
}

void pop3_quit(int fd, string &full_command) {
	cout << "pop3_quit = " << full_command << endl;

	string username;
	string res;
	unordered_map<int, info_for_each_email> this_user_email_info;

	// find username
	find_user_email_info_with_fd(fd, this_user_email_info);

	std::stringstream new_inbox_stream;
	for (int j = 0; j < this_user_email_info.size(); j++) {
		auto itor = this_user_email_info.find(j + 1);
		if (itor->second.delete_status == false) {
			new_inbox_stream << itor->second.first_line;
			new_inbox_stream << itor->second.content;
		}
	}
}

// when the message is from a backend node
void backendNodeMessage(int fd, string full_command, string buffer,
		string port) {
	string uniqueID = full_command.substr(full_command.rfind(" ") + 1);
	cout << "uniqueID parsed from message: " << uniqueID << endl;
    cout << "object server port: " << uniqueID_to_response[uniqueID].serverPort << endl;
	string command = full_command.substr(0, full_command.find(' '));
	if (isdigit(uniqueID.at(0))) {
		full_command = full_command.substr(0,
				full_command.length() - uniqueID.length());
	}
	cout << "full command in backendNodeMessage: " << full_command << endl;
	if (command.substr(0, 3) == "202") {
		// send the file
		cout << "sending the file: " << file1 << endl;
		do_write(fd, (char*) file1.c_str(), file1.length());
	} else if (command.at(0) == '2') {
		// success messages
		// if I receive success messages from other nodes, then I should be the primary node
		if (im_primary(serverList[myIndex - 1].ip,serverList[myIndex - 1].port)) {
			OKAYED_FDs.push_back(fd);
			// check if all replicas have replied
			cout << "need revert: " << needRevert << endl;
			if (!needRevert && OKAYED_FDs.size() == backendFDs.size()) {
                cout << "got all the OKAYED FDs, time to respond" << endl;
				// either I reply to the original client if I was contacted
                cout << "unique iD: " << uniqueID << endl;
                cout << "uniqueID_to_response[uniqueID].serverPort: " << uniqueID_to_response[uniqueID].serverPort << endl;
				if (uniqueID_to_response[uniqueID].serverPort == serverList[myIndex - 1].port) {
					Request tempR = uniqueID_to_response[uniqueID];
					cout << "stored response: " << tempR.response << endl;
                    cout << "client FD: " << tempR.clientFD << endl;
					do_write(tempR.clientFD, (char*) full_command.c_str(),
                             full_command.length());
					file1 = "";
					file2 = "";
					OKAYED_FDs.clear();
				} else {
					// Or I tell the replica who the client contacted to reply
					string response = "<";
					response.append(serverList[myIndex - 1].port);
					response.append(" RESPOND ");
					response.append(uniqueID + "\r\n");
					do_write(uniqueID_to_response[uniqueID].serverFD,
							(char*) response.c_str(), response.length());
					// clear the file1 variable
					file1 = "";
					file2 = "";
					OKAYED_FDs.clear();
				}
			}
		} else {
			cout << "Problem: I should be the primary node because I am receiving success msgs from other nodes" << endl;
		}
	} else if (command == "file1") {
		// store file in file1 global variable
		string temp_store = full_command.substr(full_command.find(" ") + 1);
		file1 = temp_store;
	} else if (command != "RESPOND") {
		// if I am the primary node
		if (im_primary(serverList[myIndex - 1].ip,
				serverList[myIndex - 1].port)) {
			// keep track of which replica the client sent request to
			uniqueID_to_response[uniqueID].serverFD = fd;
            //TODO: might need to change
			uniqueID_to_response[uniqueID].serverPort = serverList[myIndex-1].port;
			// I should first get the necessary data, and then propagate
			if (command == "PUT") {
				// get the file for PUT command
				processPUT(fd, full_command, true, uniqueID, true, false);
			}
			primaryNodeAction(fd, command, full_command, uniqueID, true, true);
		} else {
			// if I am a replica node
			// I will just execute the command and send back the status of the operation
			processWriteOperations(fd, command, full_command, uniqueID, true,
					false);
		}
	} else {
		if (command == "RESPOND") {
			// command is respond, we should respond to the client now
			Request tempR = uniqueID_to_response[uniqueID];
			do_write(tempR.clientFD, (char*) tempR.response.c_str(),
					tempR.response.length());
			file1 = "";
			file2 = "";
			OKAYED_FDs.clear();
		}
	}
}

void pop3_list(int fd, string &full_command) {
	cout << "pop3_list = " << full_command << endl;

	string username;
	string res;
	unordered_map<int, info_for_each_email> this_user_email_info;

	// find username
	find_user_email_info_with_fd(fd, this_user_email_info);
	cout << "pop3_list this_user_email_info.size = "
			<< this_user_email_info.size() << endl;

	// LIST ALL
	if (full_command.substr(0, 4) == "LIST" && full_command.size() < 7) { // list all message size
		int num_messages = 0;
		for (int j = 0; j < this_user_email_info.size(); j++) {
			//char* resp_to_build;
			auto itor = this_user_email_info.find(j + 1);
			if (itor != this_user_email_info.end()) {
				if (itor->second.delete_status == false) {
					num_messages += 1;
				}
			}
		}

		res = "+OK " + to_string(num_messages) + " messages\r\n";
		do_write(fd, (char*) res.c_str(), res.size());

		num_messages = 0;
		for (int j = 0; j < this_user_email_info.size(); j++) {
			auto itor = this_user_email_info.find(j + 1);
			if (itor != this_user_email_info.end()) {
				if (itor->second.delete_status == false) {
					num_messages += 1;
					int size_of_one_message = itor->second.content.size();
					res = to_string(j + 1) + " "
							+ to_string(size_of_one_message) + "\r\n";
					do_write(fd, (char*) res.c_str(), res.size());
				}
			}
		}

		res = ".\r\n";
		do_write(fd, (char*) res.c_str(), res.size());
		return;
	}

	// LIST ONE
	// first detect the number of arguments
	std::string arguments_substring = full_command.substr(5);
	std::vector < std::string > all_arguments = split_string_with_delimiter(
			arguments_substring, " ");
	if (all_arguments.size() == 1) {
		// extract the id to be found
		int id_to_find = std::stoi(all_arguments.at(0));

		//char* resp_to_build;
		auto itor = this_user_email_info.find(id_to_find);
		// if we can find the id
		if (itor != this_user_email_info.end()) {
			if (itor->second.delete_status == false) {
				int size_of_each_message = itor->second.content.size();
				res = "+OK " + to_string(id_to_find) + " "
						+ to_string(size_of_each_message) + "\r\n";
				do_write(fd, (char*) res.c_str(), res.size());
			} else {
				res = POP3_LIST_NO_SUCH_MESSAGE;
				do_write(fd, (char*) res.c_str(), res.size());

			}
		} else { // if we can't find the id
			res = POP3_LIST_NO_SUCH_MESSAGE;
			do_write(fd, (char*) res.c_str(), res.size());

		}
	} else {
		res = UNKNOWN_COMMAND_ERR;
		do_write(fd, (char*) res.c_str(), res.size());

	}

}

void pop3_rset(int fd, string &full_command) {
	cout << "pop3_rset = " << full_command << endl;

	// RSET, which undeletes all the messages that have been deleted with DELE;
	string username;
	string res;
	unordered_map<int, info_for_each_email> this_user_email_info;

	// find username
	find_user_email_info_with_fd(fd, this_user_email_info);

	for (int j = 0; j < this_user_email_info.size(); j++) {
		auto itor = this_user_email_info.find(j + 1);
		itor->second.delete_status = false;
	}
	res = "+OK maildrop has " + to_string(this_user_email_info.size())
			+ " messages\r\n";
	do_write(fd, (char*) res.c_str(), res.size());
}

// thread function for connection from clients
void* threadFunction(void *newSocket) {
	int fd = *((int*) newSocket);
	int initial_size = 4096;
	char *buf = new char[initial_size];
	std::string full_command;
	std::string command;

	while (true) {
		bool read_status = first_read(fd, buf, initial_size);
        if (!alive) {
            continue;
        }
		if (read_status) {
			cout << "this is connection with fd: " << fd << endl;
			string buffer = buf;
			strcpy(buf, "");
			cout << "overall buffer received from: " << buffer << endl;
			if (buffer.at(0) == '<') {
				// connection from other node
				string localhost = "127.0.0.1";
				full_command = buffer.substr(0, buffer.find("\r\n"));
				full_command = full_command.substr(1);
				string port = full_command.substr(0, full_command.find(" "));
                port_to_FD[port] = fd;
				full_command = full_command.substr(full_command.find(" ") + 1);
				if (buffer.length() < 10) {
					// initial message sent so that every node knows every node in the group
					backendFDs.push_back(fd);
					Server s;
					s.ip = localhost;
					s.port = port;
					if (im_primary(localhost, port)) {
						cout << "new primary fd is: " << fd << endl;
						primaryFD = fd;
					}
				} else {
					// actual message, needs processing
					backendNodeMessage(fd, full_command, buffer, port);
				}
			} else {
				// connection from the client frontend node
				string fullcommand_without_uniqueID = buffer.substr(0, buffer.find("\r\n"));
				fullcommand_without_uniqueID.append(" ");
				// generate a unique ID for each message
				unsigned int ID = hash_str((const char*) buffer.c_str());
				string uniqueID = to_string(ID);
				cout << "unique id generated: " << uniqueID << endl;
				cout << "buffer before inserting the uniqueID: " << buffer << endl;
				buffer.insert(buffer.find("\r\n"), " " + uniqueID);
//				Request r;
//				r.clientFD = fd;
				uniqueID_to_response[uniqueID].clientFD = fd;
                uniqueID_to_response[uniqueID].serverPort = serverList[myIndex - 1].port;
                cout << "setting the server port: " << uniqueID_to_response[uniqueID].serverPort <<endl;
                if (debug) {
					cout << "buffer after inserting the uniqueID: " << buffer
							<< endl;
				}
				strcpy(buf, "");
				full_command = buffer.substr(0, buffer.find("\r\n"));
				if (debug) {
					cout << "full command received from client is: "
							<< full_command << endl;
				}
				command = full_command.substr(0, full_command.find(' '));

				if (command == "PUT" || command == "CPUT" || command == "DELETE"
						|| command == "REGISTER" || command == "DELETEALL"
						|| command == "MOVE" || command == "MOVEDIR"
						|| command == "SENDMAIL" || command == "DELETEMAIL"
						|| command == "REPLYMAIL" || command == "FORWARDMAIL") {
					// get the potential data we need to send the write operation to other nodes
					if (command == "PUT") {
						if (backendFDs.size() > 0) {
							// only need to store the file if I need to multicast it (when I am not the only node)
							processPUT(fd, fullcommand_without_uniqueID, true,
									uniqueID, false, false);
						}
					}
					if (im_primary(serverList[myIndex - 1].ip, serverList[myIndex - 1].port)) {
						// if I am the primary, I will multicast the messages
						if (backendFDs.size() > 0) {
							primaryNodeAction(fd, command,
									fullcommand_without_uniqueID, uniqueID,
									false, true);
						} else {
							// if I am the only node alive, then I can just respond to the client directly
							primaryNodeAction(fd, command, fullcommand_without_uniqueID, uniqueID, false, false);
						}
					} else {
						string prefix = "< ";
						prefix.insert(1, serverList[myIndex - 1].port);
						buffer.insert(0, prefix);
						if (debug) {
							cout << "Sending to primary node: " << buffer
									<< endl;
						}
						// if I am a replica node, I will send it to the primary node
						do_write(primaryFD, (char*) buffer.c_str(),
								buffer.length());
					}
				} else if (command == "GET") {
					getOrDelete(fd, true, fullcommand_without_uniqueID,
							uniqueID, false, false);
				} else if (command == "GETALL") {
					getAllFiles(fd, fullcommand_without_uniqueID);
				} else if (command == "LOGIN") {
					login(fd, fullcommand_without_uniqueID);
				} else if (command == "GETINBOX") {
					mail_get_inbox(fd, fullcommand_without_uniqueID);
				} else if (command == "USER") {
					pop3_user(fd, fullcommand_without_uniqueID);
				} else if (command == "PASS") {
					pop3_pass(fd, fullcommand_without_uniqueID);
				} else if (command == "STAT") {
					pop3_stat(fd, fullcommand_without_uniqueID);
				} else if (command == "UIDL") {
					pop3_uidl(fd, fullcommand_without_uniqueID);
				} else if (command == "RETR") {
					pop3_retr(fd, fullcommand_without_uniqueID);
				} else if (command == "QUIT") {
					pop3_quit(fd, fullcommand_without_uniqueID);
				} else if (command == "LIST") {
					pop3_list(fd, fullcommand_without_uniqueID);
				} else if (command == "RSET") {
					pop3_rset(fd, fullcommand_without_uniqueID);
				} else if (command == "DELE") {
                    pop3_dele(fd, fullcommand_without_uniqueID);
                } else if (command == "STORAGE") {
                    admin_storage(fd, fullcommand_without_uniqueID);
                } else {
                        //					do_write(fd, (char*) UNKNOWN_COMMAND_ERR.c_str(),
                        //							UNKNOWN_COMMAND_ERR.size());

                }
			}
		} else {
			do_write(fd, (char*) READ_FAIL.c_str(), READ_FAIL.length());
		}
	}
}

void connectToNode(void* (*threadFunction)(void*), string address, string port, bool connectToMaster) {
	int *fd = new int;
	if ((*fd = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
		fprintf(stderr, "Cannot open socket (%s) \n", strerror(errno));
		exit(1);
	}
	struct sockaddr_in sockAddr;
	bzero(&sockAddr, sizeof(sockAddr));
	sockAddr.sin_family = AF_INET;
	sockAddr.sin_port = htons(stoi(port));
	inet_pton(AF_INET, (const char*) address.c_str(), &(sockAddr.sin_addr));
	if (connect(*fd, (struct sockaddr*) &sockAddr, sizeof(sockAddr)) == -1) {
		perror(
				"Failed to connect to the node (could be master, could be other backend nodes)\n");
	} else {
		if (!connectToMaster) {
			cout << "connecting to backend node: " << *fd << endl;
			backendFDs.push_back(*fd);
            port_to_FD[port] = *fd;
			Server s;
			s.port = port;
			s.ip = address;
			if (im_primary(address, port)) {
				primaryFD = *fd;
				cout << "new primary fd is: " << primaryFD << endl;
			}
			string initialMsg = "<";
			initialMsg.append(serverList[myIndex - 1].port);
			initialMsg.append(" \r\n");
			cout << "should be sending a greeting message:" << initialMsg
					<< endl;
			do_write(*fd, (char*) initialMsg.c_str(), initialMsg.length());
		}
		pthread_t ptid;
		// spawn a thread to take care of the command
		if (pthread_create(&ptid, NULL, threadFunction, (void*) fd) != 0) {
			fprintf(stderr, "Failed to create thread\n");
			exit(1);
		}
		threads.push_back(ptid);
	}
}

void* masterPostThreadFunction(void *masterFD) {
    int fd = *((int*) masterFD);
    string portMsg = "PORT " + to_string(port) + "\r\n";
    write(fd, (char*) portMsg.c_str(), portMsg.size());
    if (debug) {
        cout << portMsg << endl;
    }
}

void* masterHeartThreadFunction(void *masterFD) {
	int fd = *((int*) masterFD);
//	// send the port message
//	string portMsg = "PORT " + to_string(port) + "\r\n";
//	write(fd, (char*) portMsg.c_str(), portMsg.size());
//	if (debug) {
//		cout << portMsg << endl;
//	}

	int sequenceNumber = 0;
	while (true) {
		string heartbeat = "HEART ";
		heartbeat += to_string(sequenceNumber++) + "\r\n";
		int status = do_write(fd, (char*) heartbeat.c_str(),
				heartbeat.length());
		sleep(2);
	}
}

void* masterPrimThreadFunction(void *masterFD) {
	int fd = *((int*) masterFD);
	int initial_size = 4096;
	char *buf = new char[initial_size];
	std::string full_command;
	std::string command;
	cout << "New thread!" << endl;

	while (true) {
		bool read_status = first_read(fd, buf, initial_size);
		if (read_status) {
			string buffer = buf;
			strcpy(buf, "");
			full_command = buffer.substr(0, buffer.find("\r\n"));
			if (debug) {
				cout << "full command received is: " << full_command << endl;
			}
			command = full_command.substr(0, full_command.find(' '));
			if (command == "PRIM") {
				// setting the primary node based on the master node's message
				string primAddr = full_command.substr(
						full_command.find(' ') + 1);
				primAddr = primAddr.substr(0, primAddr.find("\r\n"));
				primaryServer.ip = primAddr.substr(0, primAddr.find(":"));
				primaryServer.port = primAddr.substr(primAddr.find(":") + 1);
			} else if (command == "DOWN") {
                string downServer = full_command.substr(full_command.find(' ') + 1);
                downServer = downServer.substr(0, downServer.find("\r\n"));
                // remove the corresponding backend FD
                string downPort = downServer.substr(downServer.find(":") + 1);
                cout << "backendFD size: " << backendFDs.size() << endl;
                cout << "port_to_FD[downPort]: " << port_to_FD[downPort] << endl;
                backendFDs.erase(std::remove(backendFDs.begin(), backendFDs.end(), port_to_FD[downPort]), backendFDs.end());
                cout << "backendFD size: " << backendFDs.size() << endl;
            } else if (command == "SHUTDOWN") {
                string fd_response = full_command.substr(full_command.find(' ')+1);
                alive = false;
                string response = "SUCCESS " + fd_response + "\r\n";
                do_write(fd, (char *)response.c_str(), response.size());
            }
		}
	}
}

int main(int argc, char *argv[]) {
	int opt;
	while ((opt = getopt(argc, argv, "v")) != -1) {
		switch (opt) {
		case 'v':
			// print out debug output
			debug = true;
			break;
		case ':':
			fprintf(stderr, "Error: An option is missing an argument\n");
			exit(1);
		case '?':
			fprintf(stderr, "Error: Invalid option\n");
			exit(1);
		default: /* '?' */
			exit(1);
		}
	}

	char *config;

	std::vector<char*> parameters;
	if (optind < argc) {
		do {
			parameters.push_back(argv[optind]);
		} while (++optind < argc);
	} else {
		fprintf(stderr, "Error: no arguments given\n");
		exit(1);
	}

	for (int i = 0; i < parameters.size(); i++) {
		if (atoi(parameters[i]) == 0) {
			// treat it as the config file name
			config = parameters[i];
		} else {
			// treat it as the index of the current server instance
			myIndex = atoi(parameters[i]);
			if (myIndex < 1 || myIndex > 3) {
				fprintf(stderr, "Error: index can only be between 1 and 3\n");
				exit(1);
			}
		}
	}

	FILE *configFile = fopen(config, "r");
	// checking if the config file exists
	if (configFile == NULL) {
		fprintf(stderr, "Error: Config file doesn't exist (%d)\n", errno);
		exit(1);
	}
	// parsing the server list file into the vector
	processConfig(configFile);
	// set my listening port
	port = stoi(serverList[myIndex - 1].port);

	// master port is constant, never changes
	masterServer.port = "5000";
	masterServer.ip = "127.0.0.1";
	// set the primary node to 8000 initially, but master node can change it anytime
	primaryServer.ip = "127.0.0.1";
	primaryServer.port = "8000";

	// create a log file for myself
	fs.open(serverList[myIndex - 1].port + "logfile.txt");

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
	my_addr.sin_port = htons(stoi(serverList[myIndex - 1].port));
	cout << "server port binding first: " << serverList[myIndex - 1].port
			<< endl;
	if (bind(server_fd, (struct sockaddr*) &my_addr, sizeof(my_addr)) < 0) {
		fprintf(stderr, "Binding the port to the socket failed: (%s)\n",
				strerror(errno));
		exit(1);
	}

	// listening
	if (listen(server_fd, SOMAXCONN)) {
		fprintf(stderr, "Listening failed\n");
		exit(1);
	}
	if (debug) {
		fprintf(stderr, "Server waiting for connection at port %d...\n",
				stoi(serverList[myIndex - 1].port));
	}

	// try to connect to all the other backend nodes within the same group
	for (int i = 0; i < serverList.size(); i++) {
		if (i != myIndex - 1) {
			connectToNode(threadFunction, serverList[i].ip, serverList[i].port,
					false);
		}
	}

	// connect to the master node
	int *master_fd = new int;
	if ((*master_fd = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
		fprintf(stderr, "Cannot open socket (%s) \n", strerror(errno));
		exit(1);
	}
	struct sockaddr_in masterAddr;
	bzero(&masterAddr, sizeof(masterAddr));
	masterAddr.sin_family = AF_INET;
	masterAddr.sin_port = htons(stoi(masterServer.port));
	inet_pton(AF_INET, (const char*) masterServer.ip.c_str(),
			&(masterAddr.sin_addr));

	struct sockaddr_in new_addr;
	new_addr.sin_family = AF_INET;
	new_addr.sin_addr.s_addr = INADDR_ANY;
	new_addr.sin_port = htons(portForward);

	op = 1;
	res = setsockopt(*master_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT,
			&op, sizeof(op));
	cout << "master port binding second: " << masterServer.port << endl;
	if (bind(*master_fd, (struct sockaddr*) &new_addr, sizeof(new_addr)) < 0) {
		fprintf(stderr, "Binding the port to the socket failed: (%s)\n",
				strerror(errno));
		exit(1);
	}

	if (connect(*master_fd, (struct sockaddr*) &masterAddr, sizeof(masterAddr))
			== -1) {
		perror("Failed to connect to the master node");
		exit(1);
	}

    pthread_t ptid0;
    if (pthread_create(&ptid0, NULL, masterPostThreadFunction,
                       (void*) master_fd) != 0) {
        fprintf(stderr, "Failed to create masterPostThread\n");
        exit(1);
    }

    pthread_join(ptid0, NULL);

	pthread_t ptid;
	// spawn a thread to take care of the command
	if (pthread_create(&ptid, NULL, masterHeartThreadFunction,
			(void*) master_fd) != 0) {
		fprintf(stderr, "Failed to create masterHeartThread\n");
		exit(1);
	}

	pthread_t ptid2;
	// spawn a thread to take care of the command
	if (pthread_create(&ptid2, NULL, masterPrimThreadFunction,
			(void*) master_fd) != 0) {
		fprintf(stderr, "Failed to create masterPrimThread\n");
		exit(1);
	}

	int i = 0;	// thread index
	while (true) {
		int *new_socket = new int;
		// wait for a connection
		struct sockaddr_in clientaddr;
		socklen_t clientaddrlen = sizeof(clientaddr);
		if ((*new_socket = accept(server_fd, (sockaddr*) &clientaddr,
				(socklen_t*) &clientaddrlen)) < 0) {
			fprintf(stderr, "Error with accepting new connections\n");
			exit(1);
		}
		sockets.push_back(new_socket);
		if (debug) {
			fprintf(stderr, "[%d] New connection\n", *new_socket);
		}

		pthread_t ptid;

		if (pthread_create(&ptid, NULL, threadFunction, (void*) new_socket)
				!= 0) {
			fprintf(stderr, "Failed to create thread\n");
			exit(1);
		}

		threads.push_back(ptid);
	}
}

