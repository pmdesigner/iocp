
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <WinSock2.h>
#include <MSWSock.h>
#include <iostream>
#include <thread>
#include <vector>
#include <string>

//Only vc
//#pragma comment(lib, "ws2_32.lib")

#define MAX_BUFFER_LEN 8192

LPFN_ACCEPTEX _fn_AcceptEx = NULL;                            //AcceptEx函数指针
LPFN_GETACCEPTEXSOCKADDRS _fn_GetAcceptExSockaddrs = nullptr; //加载GetAcceptExSockaddrs函数指针
GUID _guidAcceptEx = WSAID_ACCEPTEX;
GUID _guidGetAcceptExSockaddrs = WSAID_GETACCEPTEXSOCKADDRS;

HANDLE _hIocp;
SOCKET _serverSocket;

enum OPERATION_TYPE
{
    OP_READ = 0,
    OP_WRITE = 1,
    OP_ACCEPT = 2
};

typedef struct _PER_IO_CONTEXT
{
    OVERLAPPED _Overlapped;       // 每一个重叠I/O网络操作都要有一个
    SOCKET _Socket;               // 这个I/O操作所使用的Socket，每个连接的都是一样的
    WSABUF _WsaBuf;               // 存储数据的缓冲区，用来给重叠操作传递参数的，关于WSABUF后面还会讲
    char _Buffer[MAX_BUFFER_LEN]; // 对应WSABUF里的缓冲区
    OPERATION_TYPE _OpType;       // 标志这个重叠I/O操作是做什么的，例如Accept/Recv等
    DWORD _Bytes;
    DWORD _Flags;
} PER_IO_CONTEXT, *PPER_IO_CONTEXT;

typedef struct _PER_SOCKET_CONTEXT
{
    SOCKET _Socket;        // 每一个客户端连接的Socket
    SOCKADDR_IN _SockAddr; // 这个客户端的地址
} PER_SOCKET_CONTEXT, *PPER_SOCKET_CONTEXT;

bool InitSocket(void)
{
    // 初始化Socket库
    WSADATA data;
    WSAStartup(MAKEWORD(2, 2), &data);
    //初始化Socket
    struct sockaddr_in ServerAddress;
    // 这里需要特别注意，如果要使用重叠I/O的话，这里必须要使用WSASocket来初始化Socket
    // 注意里面有个WSA_FLAG_OVERLAPPED参数
    _serverSocket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (_serverSocket == INVALID_SOCKET)
    {
        std::cout << "Create socket failed." << std::endl;
        return false;
    }

    // 填充地址结构信息
    ZeroMemory((char *)&ServerAddress, sizeof(ServerAddress));
    ServerAddress.sin_family = AF_INET;
    // 这里可以选择绑定任何一个可用的地址，或者是自己指定的一个IP地址
    ServerAddress.sin_addr.s_addr = htonl(INADDR_ANY);
    ServerAddress.sin_port = htons(5510);
    // 绑定端口
    if (bind(_serverSocket, (struct sockaddr *)&ServerAddress, sizeof(ServerAddress)) == SOCKET_ERROR)
    {
        std::cout << "Bind socket failed." << std::endl;
        return false;
    }

    // 开始监听
    if (listen(_serverSocket, SOMAXCONN) == SOCKET_ERROR)
    {
        std::cout << "Listen socket failed." << std::endl;
        return false;
    }

    DWORD dwBytes = 0;

    WSAIoctl(
        _serverSocket,
        SIO_GET_EXTENSION_FUNCTION_POINTER,
        &_guidAcceptEx,
        sizeof(_guidAcceptEx),
        &_fn_AcceptEx,
        sizeof(_fn_AcceptEx),
        &dwBytes,
        NULL,
        NULL);

    WSAIoctl(
        _serverSocket,
        SIO_GET_EXTENSION_FUNCTION_POINTER,
        &_guidGetAcceptExSockaddrs,
        sizeof(_guidGetAcceptExSockaddrs),
        &_fn_GetAcceptExSockaddrs,
        sizeof(_fn_GetAcceptExSockaddrs),
        &dwBytes,
        NULL,
        NULL);
    return true;
}

void UninitSocket(void)
{
    WSACleanup();
}

bool PostAcceptEx(SOCKET _Socket)
{
    SOCKET sock = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED); //socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    PER_IO_CONTEXT *context = new PER_IO_CONTEXT();
    ZeroMemory(&(context->_Overlapped), sizeof(context->_Overlapped));
    context->_OpType = OP_ACCEPT;
    context->_Socket = sock;
    context->_WsaBuf.buf = context->_Buffer;
    context->_WsaBuf.len = MAX_BUFFER_LEN;
    context->_Bytes = 0;
    context->_Flags = 0;

    if (_fn_AcceptEx(_Socket, sock, context->_Buffer, 0, sizeof(sockaddr_in) + 16, sizeof(sockaddr_in) + 16, &(context->_Bytes), &(context->_Overlapped)) == FALSE)
    {
        int error = WSAGetLastError();
        if (error != WSA_IO_PENDING)
        {
            std::cout << "PostAcceptEx failed." << errno << std::endl;
            closesocket(sock);
            delete context;
            return false;
        }
    }
    return true;
}

void DoAcceptEx(PER_IO_CONTEXT *_Context)
{
}

bool PostRecv(PER_IO_CONTEXT *_Context)
{
    int errno;
    ZeroMemory(&(_Context->_Overlapped), sizeof(OVERLAPPED));
    _Context->_OpType = OP_READ;
    _Context->_WsaBuf.buf = _Context->_Buffer;
    _Context->_WsaBuf.len = MAX_BUFFER_LEN;
    _Context->_Flags = 0;
    _Context->_Bytes = 0;
    memset(_Context->_Buffer, 0, MAX_BUFFER_LEN);

    if (WSARecv(_Context->_Socket, &(_Context->_WsaBuf), 1, &(_Context->_Bytes), &(_Context->_Flags), &(_Context->_Overlapped), NULL) == SOCKET_ERROR)
    {
        errno = WSAGetLastError();
        if (errno != ERROR_IO_PENDING)
        {
            std::cout << "PostRecv failed." << errno << std::endl;
            closesocket(_Context->_Socket);
            delete _Context;
            return false;
        }
        return true;
    }
    return true;
}

void DoWsaRecv(void)
{
}

int main()
{
    std::vector<std::string> msg;
    msg.clear();

    InitSocket();

    _hIocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    if (_hIocp == NULL)
    {
        std::cout << "CreateIoCompletionPort failed." << std::endl;
        return 0;
    }

    PER_SOCKET_CONTEXT *context = new PER_SOCKET_CONTEXT();
    context->_Socket = _serverSocket;

    if (CreateIoCompletionPort((HANDLE)_serverSocket, _hIocp, (ULONG_PTR)context, 0) == nullptr)
    {
        std::cout << "Bind listen socket to iocp failed." << std::endl;
        CloseHandle(_hIocp);
        closesocket(_serverSocket);
        delete context;
        return 0;
    }

    PostAcceptEx(_serverSocket);

    //遍历队列
    while (true)
    {
        int errno;
        DWORD dwBytesTranfered = 0;
        ULONG_PTR uKey;
        PER_IO_CONTEXT *context = NULL;
        PER_SOCKET_CONTEXT *io = NULL;
        struct sockaddr_in *remoteaddr, *localaddr;
        int remotelen = sizeof(sockaddr_in), locallen = sizeof(sockaddr_in);
        if (GetQueuedCompletionStatus(_hIocp, &dwBytesTranfered, &uKey, (LPOVERLAPPED *)&context, INFINITE) == 0)
        {
            if (context)
                errno = WSAGetLastError();
            std::cout << "GetQueuedCompletionStatus failed." << errno << std::endl;
            break;
        }

        switch (context->_OpType)
        {
            //接收新的连接
        case OP_ACCEPT:
            //连接完成后，再次投递一个连接的请求
            // PER_IO_CONTEXT *io = new PER_IO_CONTEXT();
            // io->_WsaBuf.buf = io->_Buffer;
            // io->_WsaBuf.len = MAX_BUFFER_LEN;
            // io->_Socket = context->_Socket;
            _fn_GetAcceptExSockaddrs(context->_Buffer, 0 /*MAX_BUFFER_LEN - ((sizeof(SOCKADDR_IN)+16)*2)*/, sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16, (LPSOCKADDR *)&localaddr, &locallen, (LPSOCKADDR *)&remoteaddr, &remotelen);
            std::cout << "Accept client, remote: " << inet_ntoa(remoteaddr->sin_addr) << ":" << ntohs(remoteaddr->sin_port);
            std::cout << " local: " << inet_ntoa(localaddr->sin_addr) << ":" << ntohs(localaddr->sin_port) << std::endl;
            io = new PER_SOCKET_CONTEXT();
            io->_Socket = context->_Socket;
            CreateIoCompletionPort((HANDLE)context->_Socket, _hIocp, (ULONG_PTR)io, 0);
            PostRecv(context);
            PostAcceptEx(_serverSocket);
            break;
        case OP_READ:
            //投递一个接收数据的请求
            printf("Receive data: %s\r\n", context->_Buffer);
            PostRecv(context);
            break;
        default:
            break;
        }
    }

    CloseHandle(_hIocp);
    closesocket(_serverSocket);
    WSACleanup();

    std::cout << "Hello, This is first vscode program." << std::endl;
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    int m_nProcessors = si.dwNumberOfProcessors;
    std::cout << "Number of processors: " << m_nProcessors << std::endl;
    std::cout << "C++11 Hardware: " << std::thread::hardware_concurrency() << std::endl;
    //INADDR_ANY
    return 0;
}

//获取CPU核心个数
// #if !defined (_WIN32) && !defined (_WIN64)
// #define LINUX
// #include <sysconf.h>
// #else
// #define WINDOWS
// #include <windows.h>
// #endif
// unsigned core_count()
// {
//   unsigned count = 1; // 至少一个
//   #if defined (LINUX)
//   count = sysconf(_SC_NPROCESSORS_CONF);
//   #elif defined (WINDOWS)
//   SYSTEM_INFO si;
//   GetSystemInfo(&si);
//   count = si.dwNumberOfProcessors;
//   #endif
//   return count;
// }
// #include <iostream>
// int main()
// {
//   unsigned sz = core_count();
//   std::cout << sz << (1 == sz ? "core" : "cores") << '/n';
// }