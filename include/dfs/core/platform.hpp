#pragma once

#ifdef _WIN32
    #define DFS_PLATFORM_WINDOWS
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #define DFS_PLATFORM_LINUX
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
#endif

namespace dfs {

enum class Platform {
    Windows,
    Linux,
    Unknown
};

inline Platform get_platform() {
#ifdef DFS_PLATFORM_WINDOWS
    return Platform::Windows;
#elif defined(DFS_PLATFORM_LINUX)
    return Platform::Linux;
#else
    return Platform::Unknown;
#endif
}

inline const char* platform_name() {
    switch(get_platform()) {
        case Platform::Windows: return "Windows";
        case Platform::Linux: return "Linux";
        default: return "Unknown";
    }
}

} // namespace dfs