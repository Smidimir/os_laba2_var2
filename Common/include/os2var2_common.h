#pragma once

#include <winsock2.h>
#include <ws2tcpip.h>

#include <windows.h>

#undef min
#undef max

#include "utils.h"

#include <nlohmann/json.hpp>

#include <tuple>

//// Need to link with Ws2_32.lib
//#pragma comment (lib, "Ws2_32.lib")
//// #pragma comment (lib, "Mswsock.lib")




auto createWSADataRaii()
{
    return createRaiiObject<WSAData>(
            [](auto pWsData) -> bool
            {
                auto nResult = WSAStartup(MAKEWORD(2, 2), pWsData);
                return nResult == 0;
            }
            ,	[](auto pWsData)
            {
                WSACleanup();
            });
}

// using WSADataRaii = decltype(createWSADataRaii());

auto createSocketRaii( _In_ int af
        , _In_ int type
        , _In_ int protocol)
{
    return createRaiiObject<SOCKET>(
            [&](auto pSocket) -> bool
            {
//                *pSocket = WSASocketA(af, type, protocol, nullptr, 0u, WSA_FLAG_OVERLAPPED);
                *pSocket = socket(af, type, protocol);
                return *pSocket != INVALID_SOCKET;
            }
            , [](auto pSocket)
            {
                closesocket(*pSocket);
            }
    );
}

using SocketRaii = decltype(createSocketRaii(int{}, int{}, int{}));

// using SocketRaii = decltype(createSocketRaii(int{}, int{}, int{}));

auto acceptSocketRaii( SOCKET s
                     , struct sockaddr *addr
                     , int *addrlen )
{
    return createRaiiObject<SOCKET>(
            [&](auto pSocket) -> bool
            {
                *pSocket = accept(s, addr, addrlen);
                return *pSocket != INVALID_SOCKET;
            }
            , [](SOCKET* pSocket)
            {
                closesocket(*pSocket);
            });
}

auto getaddrinfoRaii(
        _In_opt_        PCSTR               pNodeName,
        _In_opt_        PCSTR               pServiceName,
        _In_opt_        const ADDRINFOA *   pHints)
{
    return std::unique_ptr<addrinfo, std::function<void(addrinfo*)>>
            {	[&]() -> addrinfo*
                 {
                     addrinfo* pAddrinfo = nullptr;
                     if (getaddrinfo(pNodeName, pServiceName, pHints, &pAddrinfo) == 0)
                         return pAddrinfo;
                     else
                         return nullptr;
                 } ()
                    , [](auto pAddrinfo)
                 {
                     if (pAddrinfo)
                         freeaddrinfo(pAddrinfo);
                 }
            };
}

template<typename T>
bool json_deserialize(nlohmann::json const& json, T& var)
{
    auto bResult{ true };

    if (bResult &= json.is_string()) {
        var = json.get<T>();
    }

    return true;
}

class FileProcessConfig
{
public:
    std::uint32_t timeouts;
    std::uint32_t package_size;
    std::string   file_name;
};

class Connection
{
public:
    ~Connection() noexcept
    {
        reset();
    }

    inline int connect(addrinfo const& addr) noexcept
    {
        m_nResult = ::connect(m_socket, addr.ai_addr, (int)addr.ai_addrlen);
    }

    inline int recv(char* buf, int len, int flags = 0) noexcept
    {
        m_nResult = ::recv(m_socket, buf, len, flags);
        return m_nResult;
    }

    template<typename T>
    int recv(T& val, int flags = 0) noexcept
    {
        return this->recv(reinterpret_cast<char*>(&val), sizeof(T), flags);
    }

    template<typename T>
    T recv_val(int flags = 0) noexcept
    {
        auto val = T{};
        this->recv(val, flags);
        return val;
    }

    inline int send(char const* buf, int len, int flags = 0) noexcept
    {
        m_nResult = ::send(m_socket, buf, len, flags);
        return m_nResult;
    }

    template<typename T>
    int send_val(T const& val, int flags = 0) noexcept
    {
        return this->send(reinterpret_cast<char const*>(&val), sizeof(T), flags);
    }

    int shutdown(int how)
    {
        m_nResult = ::shutdown(m_socket, how);
        return m_nResult;
    }

    int setsockopt(int level,int optname,char const* optval,int optlen)
    {
        m_nResult = ::setsockopt(m_socket, level, optname, optval, optlen);
        return m_nResult;
    }

    template<typename T>
    int setsockopt(int level,int optname,T const& optval)
    {
        return this->setsockopt(level, optname, reinterpret_cast<char const*>(&optval), sizeof(T));
    }

    int getsockopt(int level, int optname, char* optval, int* optlen)
    {
        m_nResult = ::getsockopt(m_socket, level, optname, optval, optlen);
        return m_nResult;
    }

    template<typename T>
    int getsockopt(int level, int optname, T& optval, int& optlen)
    {
        return this->getsockopt(level, optname, reinterpret_cast<char*>(&optval), &optlen);
    }

    inline void setSocket(SOCKET socket) noexcept
    {
        if(socket != m_socket)
        {
            reset();
            m_socket = socket;
        }
    }

    inline SOCKET const& getSocket() const noexcept
    {
        return m_socket;
    }

    inline int getResult() const
    {
        return m_nResult;
    }

    inline bool is_valid() const noexcept
    {
        return m_socket != INVALID_SOCKET;
    }

    inline bool is_socket_error() const noexcept
    {
        return m_nResult == SOCKET_ERROR;
    }

    void reset() noexcept
    {
        closesocket(m_socket);
        m_nResult = 0;
    }
private:
    SOCKET m_socket;
    int m_nResult = 0;
};

#define JSON_PARSE(JSON, VAR, TYPE_CHECK) \
    [](nlohmann::json const& json, auto& var) -> bool \
    { \
        auto bResult = json.TYPE_CHECK(); \
        if(bResult) \
            var = json.get<std::decay_t<decltype(var)>>(); \
        return bResult; \
    } (JSON, VAR)

#define JSON_GET_AND_PARSE_NAME(JSON, NAME, VAR, TYPE_CHECK) \
    [](nlohmann::json const& json, std::string const& key, auto& var) \
    { \
         auto bResult = json.is_object() && json.contains(key); \
         if(bResult) \
            bResult &= JSON_PARSE(json[key], var, TYPE_CHECK); \
         return bResult; \
    } (JSON, NAME, VAR)

#define JSON_GET_AND_PARSE_KEY(JSON, KEY, VAR, TYPE_CHECK) \
    JSON_GET_AND_PARSE_NAME(JSON, #KEY, VAR, TYPE_CHECK)

#define JSON_GET_AND_PARSE(JSON, VAR, TYPE_CHECK) \
    JSON_GET_AND_PARSE_KEY(JSON, VAR, VAR, TYPE_CHECK)

#define JSON_GET_AND_PARSE_MEMBER(JSON, OBJ, VAR, TYPE_CHECK) \
    JSON_GET_AND_PARSE_KEY(JSON, VAR, OBJ.VAR, TYPE_CHECK)

#if 0
    json.is_array();
    json.is_boolean();
    json.is_discarded();
    json.is_null();
    json.is_number();
    json.is_number_float();
    json.is_number_integer();
    json.is_number_unsigned();
    json.is_primitive();
    json.is_string();
    json.is_structured();

#endif