#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <mutex>
#include <map>
#include <random>
#include <set>

#pragma comment(lib, "Ws2_32.lib")

std::vector<SOCKET> clients; // all clients
std::map<SOCKET, std::string> user_map;  // clients' nickname
std::mutex clients_mutex;    // to protect the list of client sockets

std::vector<std::string> username_pool = { "Ryan", "Fox", "Jimmy", "Liam", "Emma", "Noah", "Olivia" }; // 用户名池
std::set<std::string> assigned_usernames;

std::string assign_random_username() {
	std::random_device rd;
	std::mt19937 gen(rd());
	std::uniform_int_distribution<> dist(0, username_pool.size() - 1);

	// ensure the name is unique
	std::string username;
	do {
		username = username_pool[dist(gen)];
	} while (assigned_usernames.find(username) != assigned_usernames.end());

	assigned_usernames.insert(username);
	return username;
}

// send private messages
void send_private_message(const std::string& target_user, const std::string& message) {
	std::lock_guard<std::mutex> lock(clients_mutex);
	for (auto it = user_map.begin(); it != user_map.end(); ++it) {
		if (it->second == target_user) {
			send(it->first, message.c_str(), static_cast<int>(message.size()), 0);
			return;
		}
	}
}

void broadcast_message(const std::string& message, SOCKET sender_socket) {
	std::lock_guard<std::mutex> lock(clients_mutex);
	for (SOCKET client : clients) {
		if (client != sender_socket) {
			send(client, message.c_str(), static_cast<int>(message.size()), 0);
		}
	}
}

// broadcast userList to all clients
void broadcast_user_list() {
	std::lock_guard<std::mutex> lock(clients_mutex);

	//userList
	std::string user_list_message = "USER_LIST:";
	for (const auto& pair : user_map) {
		user_list_message += pair.second + ",";
	}
	//remove the last ","
	if (!user_map.empty()) {
		user_list_message.pop_back();
	}

	// send
	for (SOCKET client : clients) {
		send(client, user_list_message.c_str(), static_cast<int>(user_list_message.size()), 0);
	}
}

void client_connection(SOCKET client_socket, int id) {
	char buffer[1024] = { 0 };
	std::string username = assign_random_username(); // default username

	{
		std::lock_guard<std::mutex> lock(clients_mutex);
		user_map[client_socket] = username; // store the username
	}

	// broadcast the new user join
	broadcast_message(username + " has joined the chat.", client_socket);
	broadcast_user_list();

	bool stop = false;
	while (!stop) {
		// Step 6: Communicate with the client
		int bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
		if (bytes_received > 0) {
			buffer[bytes_received] = '\0';
			std::string response(buffer);

			if (response.find("/msg ") == 0) {
				size_t colon_pos = response.find(':', 5);
				if (colon_pos != std::string::npos) {
					std::string target_user = response.substr(5, colon_pos - 5);

					// find whether the user exists now in the chat room 
					bool user_found = false;
					for (const auto& pair : user_map) {
						if (pair.second == target_user) {
							user_found = true;
							std::string private_message = response.substr(colon_pos + 1);
							std::cout << target_user << "[Private from " + username + "]: " << private_message << std::endl;
							send_private_message(target_user, "[Private from " + username + "]: " + private_message);
							break;
						}
					}

					if (!user_found) {
						std::string notFoundMsg = "Sorry, the user is not in the chat room or you spell the wrong username.";
						send(client_socket, notFoundMsg.c_str(), static_cast<int>(notFoundMsg.size()), 0);
					}
				}
				else {
					std::string formatErrorMsg = "private message format error, the correct format is: '/msg username:'";
					send(client_socket, formatErrorMsg.c_str(), static_cast<int>(formatErrorMsg.size()), 0);
				}
			}
			else if (response == "!bye") {
				stop = true;
				response = username + " has left the chat.";
				std::cout << response << std::endl;
				broadcast_message(response, client_socket);
			}
			else {
				std::cout << username << ": " << response << std::endl;
				// send to ohter clients except the sender
				broadcast_message(username + ": " + response, client_socket);
			}
		}
		else {
			stop = true;
		}
	}

	// disconnect
	{
		std::lock_guard<std::mutex> lock(clients_mutex);
		clients.erase(std::remove(clients.begin(), clients.end(), client_socket), clients.end());
		user_map.erase(client_socket);
	}

	// notify other clients to update the userlist
	broadcast_user_list();

	closesocket(client_socket);
}

int server() {
	// Step 1: Initialize WinSock
	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
		std::cerr << "WSAStartup failed with error: " << WSAGetLastError() << std::endl;
		return 1;
	}

	// Step 2: Create a socket
	SOCKET server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (server_socket == INVALID_SOCKET) {
		std::cerr << "Socket creation failed with error: " << WSAGetLastError() << std::endl;
		WSACleanup();
		return 1;
	}

	// Step 3: Bind the socket
	sockaddr_in server_address = {};
	server_address.sin_family = AF_INET;
	server_address.sin_port = htons(65432);  // Server port
	server_address.sin_addr.s_addr = INADDR_ANY; // Accept connections on any IP address

	if (bind(server_socket, (sockaddr*)&server_address, sizeof(server_address)) == SOCKET_ERROR) {
		std::cerr << "Bind failed with error: " << WSAGetLastError() << std::endl;
		closesocket(server_socket);
		WSACleanup();
		return 1;
	}

	// Step 4: Listen for incoming connections
	if (listen(server_socket, SOMAXCONN) == SOCKET_ERROR) {
		std::cerr << "Listen failed with error: " << WSAGetLastError() << std::endl;
		closesocket(server_socket);
		WSACleanup();
		return 1;
	}

	std::cout << "Server is listening on port 65432..." << std::endl;

	int connections = 0;
	while (true) {
		// Step 5: Accept a connection
		sockaddr_in client_address = {};
		int client_address_len = sizeof(client_address);

		SOCKET client_socket = accept(server_socket, (sockaddr*)&client_address, &client_address_len);
		if (client_socket == INVALID_SOCKET) {
			std::cerr << "Accept failed with error: " << WSAGetLastError() << std::endl;
			closesocket(server_socket);
			WSACleanup();
			return 1;
		}

		char client_ip[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &client_address.sin_addr, client_ip, INET_ADDRSTRLEN);
		std::cout << "Accepted connection from " << client_ip << ":" << ntohs(client_address.sin_port) << std::endl;

		{
			std::lock_guard<std::mutex> lock(clients_mutex);
			clients.push_back(client_socket);
		}

		std::thread* t = new std::thread(client_connection, client_socket, ++connections);
		t->detach();
	}

	// Step 7: Clean up
	closesocket(server_socket);
	WSACleanup();

	return 0;
}

int main() {
	server();
}