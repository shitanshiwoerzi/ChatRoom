// Dear ImGui: standalone example application for DirectX 11

// Learn about Dear ImGui:
// - FAQ                  https://dearimgui.com/faq
// - Getting Started      https://dearimgui.com/getting-started
// - Documentation        https://dearimgui.com/docs (same as your local docs/ folder).
// - Introduction, links and more at the top of imgui.cpp

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <mutex>
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <d3d11.h>
#include <tchar.h>
#include <map>

#pragma comment(lib, "ws2_32.lib")

#define DEFAULT_BUFFER_SIZE 1024

std::vector<std::string> user_list;  // local user list
std::map<std::string, std::vector<std::string>> private_chats; // private chat log
std::string current_private_chat;    // current target user in the private chat 
std::map<std::string, int> unread_messages; // unread messages count
bool is_private_chat_open = false;

std::atomic<bool> running(true); // control the state of threads
std::vector<std::string> messages; // chat log
std::mutex data_mutex;         // to protect data

// Data
static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static bool g_SwapChainOccluded = false;
static UINT g_ResizeWidth = 0, g_ResizeHeight = 0;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

// Forward declarations of helper functions
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// update user list
void update_user_list(const std::string& user_list_message) {
	std::lock_guard<std::mutex> lock(data_mutex);
	user_list.clear();

	size_t start = 0, end;
	while ((end = user_list_message.find(',', start)) != std::string::npos) {
		user_list.push_back(user_list_message.substr(start, end - start));
		start = end + 1;
	}
	if (start < user_list_message.size()) {
		user_list.push_back(user_list_message.substr(start));
	}
}

// Receive the sentence from the server
void receive_messages(SOCKET client_socket) {
	std::string recvBuffer;
	while (running) {
		char buffer[DEFAULT_BUFFER_SIZE] = { 0 };
		int bytes_received = recv(client_socket, buffer, DEFAULT_BUFFER_SIZE, 0);
		if (bytes_received > 0) {
			recvBuffer.append(buffer, bytes_received);
			while (true) {
				// find the \n
				size_t pos = recvBuffer.find('\n');
				if (pos == std::string::npos)
				{
					// if not, wait the next \n
					break;
				}

				// get a complete message 
				std::string message = recvBuffer.substr(0, pos);
				recvBuffer.erase(0, pos + 1); // remove the finished part

				if (message.find("USER_LIST:") == 0) {
					// update user list
					std::string user_list_message = message.substr(10); // remove "USER_LIST:" prefix
					update_user_list(user_list_message);
				}
				else if ((message.find("[Private from ") == 0)) {
					// format: [Private from username]: message
					size_t start = 14; // skip "[Private from "
					size_t end = message.find(']', start);
					if (end != std::string::npos) {
						std::string sender = message.substr(start, end - start);
						std::string private_message = message.substr(end + 3); // skip "]: "
						{
							std::lock_guard<std::mutex> lock(data_mutex);
							private_chats[sender].push_back("[Private]: " + private_message);

							unread_messages[sender]++;
						}
					}
				}
				else {
					std::lock_guard<std::mutex> lock(data_mutex);
					messages.push_back(std::string(buffer));
				}
			}
		}
		else if (bytes_received == 0) {
			std::lock_guard<std::mutex> lock(data_mutex);
			messages.push_back("[System]: Connection closed by server.");
			running = false;
			break;
		}
		else {
			std::lock_guard<std::mutex> lock(data_mutex);
			messages.push_back("[Error]: Receive failed.");
			running = false;
			break;
		}
	}

}

SOCKET init_client_socket(const char* host, unsigned int port) {
	std::string sentence;

	// Initialize WinSock
	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
		std::cerr << "WSAStartup failed with error: " << WSAGetLastError() << std::endl;
		return INVALID_SOCKET;
	}

	// Create a socket
	SOCKET client_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (client_socket == INVALID_SOCKET) {
		std::cerr << "Socket creation failed with error: " << WSAGetLastError() << std::endl;
		WSACleanup();
		return INVALID_SOCKET;
	}

	// Resolve the server address and port
	sockaddr_in server_address = {};
	server_address.sin_family = AF_INET;
	//server_address.sin_port = htons(std::stoi(port));
	server_address.sin_port = htons(port);
	if (inet_pton(AF_INET, host, &server_address.sin_addr) <= 0) {
		std::cerr << "Invalid address/ Address not supported" << std::endl;
		closesocket(client_socket);
		WSACleanup();
		return INVALID_SOCKET;
	}

	// Connect to the server
	if (connect(client_socket, reinterpret_cast<sockaddr*>(&server_address), sizeof(server_address)) == SOCKET_ERROR) {
		std::cerr << "Connection failed with error: " << WSAGetLastError() << std::endl;
		closesocket(client_socket);
		WSACleanup();
		return INVALID_SOCKET;
	}

	return client_socket;
}

// render user list
void render_user_list() {
	ImGui::BeginChild("UserList", ImVec2(150, -30), true, ImGuiWindowFlags_AlwaysVerticalScrollbar);
	ImGui::Text("Users");
	ImGui::Separator();

	{
		std::lock_guard<std::mutex> lock(data_mutex);
		for (const auto& user : user_list) {
			int unread_count = unread_messages[user];
			if (unread_count > 0) {
				// red font style to show the unread messages
				ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "%s (New)", user.c_str());
			}
			else {
				ImGui::Text("%s", user.c_str());
			}

			if (ImGui::IsItemClicked()) {
				current_private_chat = user; // set current private chat
				is_private_chat_open = true;
				unread_messages[user] = 0;    // remove all
			}
		}
	}

	ImGui::EndChild();
}

// render public chat winodw
void render_public_chat(SOCKET client_socket) {
	static char input_buffer[256] = { 0 };
	// public chat area
	ImGui::BeginChild("ChatHistory", ImVec2(0, -30), true);
	{
		std::lock_guard<std::mutex> lock(data_mutex);
		for (const auto& message : messages) {
			ImGui::TextWrapped("%s", message.c_str());
		}
	}
	ImGui::EndChild();

	// input text box
	ImGui::PushItemWidth(-80);
	if (ImGui::InputText("##Input", input_buffer, sizeof(input_buffer), ImGuiInputTextFlags_EnterReturnsTrue)) {
		std::string message(input_buffer);
		if (!message.empty()) {
			send(client_socket, message.c_str(), static_cast<int>(message.size()), 0);
			{
				std::lock_guard<std::mutex> lock(data_mutex);
				messages.push_back("[You]: " + message);
			}
			if (message == "!bye") {
				running = false;
			}
			input_buffer[0] = '\0';
		}
	}
	ImGui::SameLine();

	// send button
	if (ImGui::Button("Send")) {
		std::string message(input_buffer);
		if (!message.empty()) {
			send(client_socket, message.c_str(), static_cast<int>(message.size()), 0);
			{
				std::lock_guard<std::mutex> lock(data_mutex);
				messages.push_back("[You]: " + message);
			}
			if (message == "!bye") {
				running = false;
			}
			input_buffer[0] = '\0';
		}
	}
}

// render private chat window
void render_private_chat(SOCKET client_socket) {
	// haven't chose
	if (current_private_chat.empty() || !is_private_chat_open) {
		return;
	}

	std::string window_title = "Private Chat with " + current_private_chat;
	ImGui::Begin(window_title.c_str(), &is_private_chat_open, ImGuiWindowFlags_AlwaysAutoResize);

	// show private messages history
	ImGui::BeginChild("PrivateChatHistory", ImVec2(400, 300), true);
	{
		std::lock_guard<std::mutex> lock(data_mutex);
		for (const auto& message : private_chats[current_private_chat]) {
			ImGui::TextWrapped("%s", message.c_str());
		}
	}
	ImGui::EndChild();

	static char input_buffer[256] = { 0 };

	// enter
	if (ImGui::InputText("##PrivateInput", input_buffer, sizeof(input_buffer), ImGuiInputTextFlags_EnterReturnsTrue)) {
		std::string message(input_buffer);
		if (!message.empty()) {
			{
				std::lock_guard<std::mutex> lock(data_mutex);
				private_chats[current_private_chat].push_back("[You]: " + message);
			}
			// transfer format to: /msg username:message
			std::string formatted_message = "/msg " + current_private_chat + ":" + message;
			send(client_socket, formatted_message.c_str(), static_cast<int>(formatted_message.size()), 0);

			input_buffer[0] = '\0'; // clear input box
		}
	}

	ImGui::SameLine();

	// send button
	if (ImGui::Button("Send")) {
		std::string message(input_buffer);
		if (!message.empty()) {
			{
				std::lock_guard<std::mutex> lock(data_mutex);
				private_chats[current_private_chat].push_back("[You]: " + message);
			}
			// transfer format to: /msg username:message
			std::string formatted_message = "/msg " + current_private_chat + ":" + message;
			send(client_socket, formatted_message.c_str(), static_cast<int>(formatted_message.size()), 0);

			input_buffer[0] = '\0'; // clear input box
		}
	}
	ImGui::End();
}

void render_chat_interface(SOCKET client_socket) {
	// set the size of window and default location
	ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowPos(ImVec2(100, 100), ImGuiCond_FirstUseEver);

	ImGui::Begin("Chat Room");

	render_user_list();

	ImGui::SameLine();

	render_public_chat(client_socket);

	render_private_chat(client_socket);

	ImGui::End();
}

// Main code
int main(int, char**)
{
	// Create application window
	//ImGui_ImplWin32_EnableDpiAwareness();
	WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"ImGui Example", nullptr };
	::RegisterClassExW(&wc);
	HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"Dear ImGui DirectX11 Example", WS_OVERLAPPEDWINDOW, 100, 100, 1280, 800, nullptr, nullptr, wc.hInstance, nullptr);

	// Initialize Direct3D
	if (!CreateDeviceD3D(hwnd))
	{
		CleanupDeviceD3D();
		::UnregisterClassW(wc.lpszClassName, wc.hInstance);
		return 1;
	}

	// Show the window
	::ShowWindow(hwnd, SW_SHOWDEFAULT);
	::UpdateWindow(hwnd);

	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

	// Setup Dear ImGui style
	ImGui::StyleColorsDark();
	//ImGui::StyleColorsLight();

	// Setup Platform/Renderer backends
	ImGui_ImplWin32_Init(hwnd);
	ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

	const char* host = "127.0.0.1";
	unsigned int port = 65432;
	SOCKET client_socket = init_client_socket(host, port);
	if (client_socket == INVALID_SOCKET) return -1;
	std::thread* t = new std::thread(receive_messages, client_socket);
	t->detach();

	// Our state
	ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

	// Main loop
	bool done = false;
	while (!done)
	{
		// Poll and handle messages (inputs, window resize, etc.)
		// See the WndProc() function below for our to dispatch events to the Win32 backend.
		MSG msg;
		while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
		{
			::TranslateMessage(&msg);
			::DispatchMessage(&msg);
			if (msg.message == WM_QUIT)
				done = true;
		}
		if (done)
			break;

		// Handle window being minimized or screen locked
		if (g_SwapChainOccluded && g_pSwapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED)
		{
			::Sleep(10);
			continue;
		}
		g_SwapChainOccluded = false;

		// Handle window resize (we don't resize directly in the WM_SIZE handler)
		if (g_ResizeWidth != 0 && g_ResizeHeight != 0)
		{
			CleanupRenderTarget();
			g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
			g_ResizeWidth = g_ResizeHeight = 0;
			CreateRenderTarget();
		}

		// Start the Dear ImGui frame
		ImGui_ImplDX11_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();

		render_chat_interface(client_socket);

		// Rendering
		ImGui::Render();
		const float clear_color_with_alpha[4] = { clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w };
		g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
		g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

		// Present
		HRESULT hr = g_pSwapChain->Present(1, 0);   // Present with vsync
		//HRESULT hr = g_pSwapChain->Present(0, 0); // Present without vsync
		g_SwapChainOccluded = (hr == DXGI_STATUS_OCCLUDED);
	}

	running = false;
	closesocket(client_socket);

	// Cleanup
	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();

	CleanupDeviceD3D();
	::DestroyWindow(hwnd);
	::UnregisterClassW(wc.lpszClassName, wc.hInstance);

	return 0;
}

// Helper functions

bool CreateDeviceD3D(HWND hWnd)
{
	// Setup swap chain
	DXGI_SWAP_CHAIN_DESC sd;
	ZeroMemory(&sd, sizeof(sd));
	sd.BufferCount = 2;
	sd.BufferDesc.Width = 0;
	sd.BufferDesc.Height = 0;
	sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	sd.BufferDesc.RefreshRate.Numerator = 60;
	sd.BufferDesc.RefreshRate.Denominator = 1;
	sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.OutputWindow = hWnd;
	sd.SampleDesc.Count = 1;
	sd.SampleDesc.Quality = 0;
	sd.Windowed = TRUE;
	sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

	UINT createDeviceFlags = 0;
	//createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
	D3D_FEATURE_LEVEL featureLevel;
	const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
	HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
	if (res == DXGI_ERROR_UNSUPPORTED) // Try high-performance WARP software driver if hardware is not available.
		res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
	if (res != S_OK)
		return false;

	CreateRenderTarget();
	return true;
}

void CleanupDeviceD3D()
{
	CleanupRenderTarget();
	if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
	if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
	if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void CreateRenderTarget()
{
	ID3D11Texture2D* pBackBuffer;
	g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
	g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
	pBackBuffer->Release();
}

void CleanupRenderTarget()
{
	if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Win32 message handler
// You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
// - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
// - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
// Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
		return true;

	switch (msg)
	{
	case WM_SIZE:
		if (wParam == SIZE_MINIMIZED)
			return 0;
		g_ResizeWidth = (UINT)LOWORD(lParam); // Queue resize
		g_ResizeHeight = (UINT)HIWORD(lParam);
		return 0;
	case WM_SYSCOMMAND:
		if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
			return 0;
		break;
	case WM_DESTROY:
		::PostQuitMessage(0);
		return 0;
	}
	return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
