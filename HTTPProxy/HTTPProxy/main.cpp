#include <stdio.h>
#include <Windows.h>
#include <process.h>
#include <string.h>

#pragma comment(lib,"Ws2_32.lib")
#define MAXSIZE 65507 //发送数据报文的最大长度
#define HTTP_PORT 80 //http server port

struct HttpHeader {
	char method[4];  //POST或者GET，注意有些为CONNECT，本实验暂不考虑
	char url[1024];  //请求的url
	char host[1024];  //target host
	char cookie[1024 * 10];  //cookie
	HttpHeader() {
		ZeroMemory(this, sizeof(HttpHeader));   /***************///内存置零????
	}
};

bool InitSocket();
void ParseHttpHead(char *buffer, HttpHeader *httpHeader);
bool ConnectToServer(SOCKET *serverSocket, char *host);
unsigned int __stdcall ProxyThread(LPVOID lpParameter);   /*********/

														  //proxy arguments
SOCKET ProxyServer;
sockaddr_in ProxyServerAddr;
const int ProxyPort = 10240;  /*********************/

							  //a new connction should be dealt with a new thread, and the frequent creation and destruction of thread can consume a lot of resource
							  //using thread pool to promote the efficiency
const int ProxyThreadMaxNum = 20;
//HANDLE ProxyThreadHandle[ProxyThreadMaxNum] = {0};
//DWORD ProxyThreadDW[ProxyThreadMaxNum] = {0};

struct ProxyParam {
	SOCKET clientSocket;
	SOCKET serverSocket;
};

int main(int argc, char* argv[]) {  /*****************/
	printf("代理服务器正在启动\n");
	printf("初始化...\n");
	if (!InitSocket()) {
		printf("socket初始化失败\n");
		return -1;
	}
	printf("代理服务器正在运行，监听端口 %d\n", ProxyPort);
	SOCKET acceptSocket = INVALID_SOCKET;
	ProxyParam *lpProxyParam;
	HANDLE hThread;
	DWORD dwThreadID;
	//Proxy Server keeps listening
	while (true) {
		acceptSocket = accept(ProxyServer, NULL, NULL);  /**********/
		lpProxyParam = new ProxyParam;
		if (lpProxyParam == NULL) {
			continue;
		}
		lpProxyParam->clientSocket = acceptSocket;
		hThread = (HANDLE)_beginthreadex(NULL, 0, &ProxyThread, (LPVOID)lpProxyParam, 0, 0); //**************/
		CloseHandle(hThread);
		Sleep(200);
	}
	closesocket(ProxyServer);
	WSACleanup(); //***************/
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
	err = WSAStartup(wVersionRequested, &wsaData);
	if (err != 0) {
		//cannot find winsock.dll
		printf("load winsock failed, error code is:%d\n", WSAGetLastError());
		return false;
	}
	if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2) {  /*************/
		printf("Cannot find the correct winsock version\n");
		WSACleanup();    /*****/
		return false;
	}
	ProxyServer = socket(AF_INET, SOCK_STREAM, 0);
	if (INVALID_SOCKET == ProxyServer) {
		printf("create socket failed, error code is: %d\n", WSAGetLastError());
		return false;
	}
	ProxyServerAddr.sin_family = AF_INET;
	ProxyServerAddr.sin_port = htons(ProxyPort);
	ProxyServerAddr.sin_addr.S_un.S_addr = INADDR_ANY;
	if (bind(ProxyServer, (SOCKADDR*)&ProxyServerAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
		printf("binding socket failed\n");
		return false;
	}
	if (listen(ProxyServer, SOMAXCONN) == SOCKET_ERROR) {
		printf("listening port %d failed", ProxyPort);
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
	SOCKADDR_IN clientAddr;
	int length = sizeof(SOCKADDR_IN);
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
	ParseHttpHead(CacheBuffer, httpHeader);
	delete CacheBuffer;
	if (!ConnectToServer(&((ProxyParam*)lpParameter)->serverSocket, httpHeader->host)) {
		goto error;
	}
	printf("proxy connect host %s success!\n", httpHeader->host);
	//send the HTTP datagram form the client to the target server directly
	ret = send(((ProxyParam *)lpParameter)->serverSocket, Buffer, strlen(Buffer) + 1, 0);
	//waiting for the target server to return data
	recvSize = recv(((ProxyParam *)lpParameter)->serverSocket, Buffer, MAXSIZE, 0);
	if (recvSize <= 0) {
		goto error;
	}
	//send the data from the target server to client directly
	ret = send(((ProxyParam*)lpParameter)->clientSocket, Buffer, sizeof(Buffer), 0);
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
	const char *delim = "\r\n";//换行符
	p = strtok_s(buffer, delim, &ptr);//提取第一行
	printf("%s\n", p);
	if (p[0] == 'G') {	//GET
		memcpy(httpHeader->method, "GET", 3);
		memcpy(httpHeader->url, &p[4], strlen(p) - 13);
	}
	else if (p[0] == 'P') {	//POST
		memcpy(httpHeader->method, "POST", 4);
		memcpy(httpHeader->url, &p[5], strlen(p) - 14);
	}
	printf("%s\n", httpHeader->url);//url
	p = strtok_s(NULL, delim, &ptr);//使用换行符分割，提取当前行
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
		p = strtok_s(NULL, delim, &ptr);//使用换行符分割，提取当前行
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
	serverAddr.sin_port = htons(HTTP_PORT);
	HOSTENT *hostent = gethostbyname(host);//获取主机名字、地址信息
	if (!hostent) {
		return false;
	}
	in_addr Inaddr = *((in_addr*)*hostent->h_addr_list);
	serverAddr.sin_addr.s_addr = inet_addr(inet_ntoa(Inaddr));
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