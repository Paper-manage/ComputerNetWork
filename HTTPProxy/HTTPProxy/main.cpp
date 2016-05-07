#include <stdio.h>
#include <Windows.h>
#include <process.h>
#include <string.h>
#include <tchar.h>

#pragma comment(lib,"Ws2_32.lib")
#define MAXSIZE 65507 //maximum size of datagram
#define HTTP_PORT 80 //http server port

struct HttpHeader {
	char method[4];  //POST or GET, some are CONNECT, but this Lab does not consider of it
	char url[1024];  //requested url
	char host[1024];  //target host
	char cookie[1024 * 10];  //cookie
	HttpHeader() {
		ZeroMemory(this, sizeof(HttpHeader));  //Fills a block of memory with zeros
	}
};

bool InitSocket();
void ParseHttpHead(char *buffer, HttpHeader *httpHeader);
bool ConnectToServer(SOCKET *serverSocket, char *host);
unsigned int __stdcall ProxyThread(LPVOID lpParameter);

//proxy arguments
SOCKET ProxyServer;
sockaddr_in ProxyServerAddr;
const int ProxyPort = 10240;

//a new connction should be dealt with a new thread, and the frequent creation and destruction of thread can consume a lot of resource
//using thread pool to promote the efficiency
//const int ProxyThreadMaxNum = 20;
//HANDLE ProxyThreadHandle[ProxyThreadMaxNum] = {0};
//DWORD ProxyThreadDW[ProxyThreadMaxNum] = {0};

struct ProxyParam {
	SOCKET clientSocket;
	SOCKET serverSocket;
};

int _tmain(int argc, _TCHAR* argv[]) {
	printf("Proxy server is starting...\n");
	printf("Initialize...\n");
	if (!InitSocket()) {
		printf("socket initialize failed!\n");
		return -1;
	}
	printf("Proxy server is running, listening to port %d\n", ProxyPort);
	SOCKET acceptSocket = INVALID_SOCKET;
	ProxyParam *lpProxyParam;
	HANDLE hThread;
	//DWORD dwThreadID; 
	//Proxy Server keeps listening
	while (true) {
		acceptSocket = accept(ProxyServer, NULL, NULL);  //新创建一个socket与客户端通信
		lpProxyParam = new ProxyParam;
		if (lpProxyParam == NULL) {
			continue;
		}
		lpProxyParam->clientSocket = acceptSocket;
		hThread = (HANDLE)_beginthreadex(NULL, 0, &ProxyThread, (LPVOID)lpProxyParam, 0, 0);
		CloseHandle(hThread);
		Sleep(200);
	}
	closesocket(ProxyServer);
	WSACleanup();
	return 0;
}


//************************************
//Method: InitSocket
//FullName: InitSocket
//Access: public
//Returns: bool
//Qualifier: initiate the socket
//************************************
bool InitSocket() {
	//load the socket library(must be done)
	WORD wVersionRequested;
	WSADATA wsaData;
	//error prompt when loading the socket
	int err;
	//version 2.2
	wVersionRequested = MAKEWORD(2, 2);
	//load the DLL file and Socket Library
	err = WSAStartup(wVersionRequested, &wsaData);   //Initialize DLL
	if (err != 0) {
		//cannot find winsock.dll
		printf("load winsock failed, error code is:%d\n", WSAGetLastError());
		return false;
	}
	if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2) {
		printf("Cannot find the correct winsock version\n");
		WSACleanup();   //Release DLL
		return false;
	}
	ProxyServer = socket(AF_INET, SOCK_STREAM, 0);  //socket descriptor  AF_INET refers to the Internet Protocol version 4 (IPv4) address family.
	if (INVALID_SOCKET == ProxyServer) {
		printf("create socket failed, error code is: %d\n", WSAGetLastError());
		return false;
	}
	ProxyServerAddr.sin_family = AF_INET;
	ProxyServerAddr.sin_port = htons(ProxyPort);  //本地字节顺序->网络字节顺序
	ProxyServerAddr.sin_addr.S_un.S_addr = INADDR_ANY;   //在服务器运行的这个主机上的任何一个有效地址都是可以的
	if (bind(ProxyServer, (SOCKADDR*)&ProxyServerAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
		printf("binding socket failed\n");
		return false;
	}
	if (listen(ProxyServer, SOMAXCONN) == SOCKET_ERROR) {  //SOMAXCONN为连接请求队列的大小 The listen function places a socket in a state in which it is listening for an incoming connection.
		printf("listening to port %d failed", ProxyPort);
		return false;
	}
	return true;
}

//*************************
//Method: ProxyThread
//FullName: ProxyThread
//Access: public
//Returns: unsigned int __stdcall
//Qualifier: thread execution function
//Parameter: LPVOID lpParameter
//*************************

unsigned int __stdcall ProxyThread(LPVOID lpParameter) {
	char Buffer[MAXSIZE];
	char *CacheBuffer;
	ZeroMemory(Buffer, MAXSIZE);
	//SOCKADDR_IN clientAddr;
	//int length = sizeof(SOCKADDR_IN);
	int recvSize;
	int ret;
	recvSize = recv(((ProxyParam*)lpParameter)->clientSocket, Buffer, MAXSIZE, 0);
	if (recvSize <= 0) {
		goto error;
	}
	HttpHeader* httpHeader = new HttpHeader();
	CacheBuffer = new char[recvSize + 1];
	ZeroMemory(CacheBuffer, recvSize + 1);
	memcpy(CacheBuffer, Buffer, recvSize);
	printf("*******************************\n");
	printf(Buffer);
	printf("*******************************\n");
	ParseHttpHead(CacheBuffer, httpHeader);
	delete CacheBuffer;
	if (!ConnectToServer(&((ProxyParam*)lpParameter)->serverSocket, httpHeader->host)) {
		goto error;
	}
	printf("proxy connects to host %s success!\n", httpHeader->host);
	//将客户端发送的HTTP数据报文直接转发给目标服务器
	ret = send(((ProxyParam *)lpParameter)->serverSocket, Buffer, strlen(Buffer) + 1, 0);  //已经有连接了，直接send即可
	printf("&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&\n");
	printf(Buffer);
	printf("&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&\n");
	//printf("ret = %d\n", ret);
	//等待目标服务器返回数据
	recvSize = recv(((ProxyParam *)lpParameter)->serverSocket, Buffer, MAXSIZE, 0);
	printf("###############################\n");
	printf(Buffer);
	printf("###############################\n");
	//printf("recvSize = %d\n", recvSize);
	if (recvSize <= 0) {
		goto error;
	}
	//将目标服务器返回的数据直接转发给客户端
	ret = send(((ProxyParam*)lpParameter)->clientSocket, Buffer, sizeof(Buffer), 0);
	//printf("ret = %d\n", ret);
	//error handling
error:
	printf("close the socket\n");
	Sleep(200);
	closesocket(((ProxyParam*)lpParameter)->clientSocket);
	closesocket(((ProxyParam*)lpParameter)->serverSocket);
	delete lpParameter;
	_endthreadex(0);
	return 0;
}

//*************************
//Method: ParseHttpHead
//FullName: ParseHttpHead
//Access: public
//Returns: void
//Qualifier: parse the HTTP Header in the TCP message
//Parameter: char *buffer
//Parameter: HttpHeader *httpHeader
//*************************

void ParseHttpHead(char *buffer, HttpHeader *httpHeader) {
	char *p;
	char *ptr;
	const char *delim = "\r\n";  //delimiter
	p = strtok_s(buffer, delim, &ptr);  //parse the first line

	printf("------------------------------------------\n");
	printf("%s\n", p);
	printf("------------------------------------------\n");
	if (p[0] == 'G') {	//GET
		memcpy(httpHeader->method, "GET", 3);
		memcpy(httpHeader->url, &p[4], strlen(p) - 13);
	}
	else if (p[0] == 'P') {	//POST
		memcpy(httpHeader->method, "POST", 4);
		memcpy(httpHeader->url, &p[5], strlen(p) - 14);
	}
	printf("%s\n", httpHeader->url);  //url
	p = strtok_s(NULL, delim, &ptr);  

	printf("------------------------------------------\n");
	printf("%s\n", p);
	printf("------------------------------------------\n");

	while (p) {
		switch (p[0]) {
		case 'H'://HOST
			memcpy(httpHeader->host, &p[6], strlen(p) - 6);
			break;
		case 'C'://Cookie
			if (strlen(p) > 8) {
				char header[8];
				ZeroMemory(header, sizeof(header));
				memcpy(header, p, 6);
				if (!strcmp(header, "Cookie")) {
					memcpy(httpHeader->cookie, &p[8], strlen(p) - 8);
				}
			}
			break;
		default:
			break;
		}
		p = strtok_s(NULL, delim, &ptr);  //使用换行符分割，提取当前行

		printf("------------------------------------------\n");
		printf("%s\n", p);
		printf("------------------------------------------\n");
	}
}

//**************************************
//Method:			ConnectToServer
//FullName:			ConnectToServer
//Access:			public
//Returns:			bool
//Qualifier:		根据主机创建目标服务器套接字，并连接
//Parameter:		SOCKET *serverSocket
//Parameter:		char *host
//**************************************
bool ConnectToServer(SOCKET *serverSocket, char *host) {//代理访问server
	sockaddr_in serverAddr;
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(HTTP_PORT);  ////本地字节顺序->网络字节顺序
	HOSTENT *hostent = gethostbyname(host);  //实现域名到32位IP地址的转换，返回
	if (!hostent) {
		return false;
	}
	in_addr Inaddr = *((in_addr*)*hostent->h_addr_list);
	serverAddr.sin_addr.s_addr = inet_addr(inet_ntoa(Inaddr));   //实现点分十进制IP地址到32位IP地址的转换
	*serverSocket = socket(AF_INET, SOCK_STREAM, 0);
	if (*serverSocket == INVALID_SOCKET) {
		return false;
	}
	if (connect(*serverSocket, (SOCKADDR*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {//尝试连接
		closesocket(*serverSocket);
		return false;
	}
	return true;
}