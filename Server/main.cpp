#include <os2var2_common.h>
#include <utils.h>


#include <nlohmann/json.hpp>

#include <array>
#include <fstream>
#include <vector>
#include <optional>
#include <string_view>
#include <future>

#define C_BS char{ 0x8 }
#define C_TAB char{ 0x9 }
#define C_LF char{ 0xA }
#define C_VT char{ 0xB }
#define C_FF char{ 0xC }
#define C_CR char{ 0xD }

using namespace std::string_literals;

// experement:		time-out
// Package size:	128 bite
// Time-out:		10-20 ms
// Connection type:	logical channel
// Data size:		16 kbyte
// Output:			server


struct ServerConfig
{
    bool deserialize(std::string const& configName)
    {
        auto bResult = true;

        auto configFile = std::ifstream{ configName };
        bResult &= configFile.is_open();
        if (bResult)
        {
            auto serverConfigJson = nlohmann::json{};
            configFile >> serverConfigJson;

            bResult &= !configFile.fail();

            bResult &= JSON_GET_AND_PARSE(serverConfigJson, server_port, is_string);
        }

        return bResult;
    }

    bool serialize(std::string const& configName)
    {
        auto bResult = true;

        auto confgFile = std::ofstream{ configName };

        bResult &= confgFile.is_open();

        if (bResult)
        {
            confgFile << nlohmann::json
                {
                    {"server_port"       , server_port       },
                };
        }

        return bResult;
    }

    std::string   server_port;
};

struct TimeData
{
    std::uint32_t timeout;
    std::uint32_t recv_time;
};

int main(int argc, char** argv)
{
    auto const serverConfig = []()
    {
        auto const configName = std::string{"config_server.json"};

        auto config = std::optional<ServerConfig>{ ServerConfig{} };
        if (!config->deserialize(configName)) {
            config->server_port = "9999";
            config->serialize(configName);

            print_std("Generated default config: ", configName);
            config = std::nullopt;
        }

        return config;
    } ();

    if(!serverConfig.has_value())
        return 0;

    // Initialize Winsock
    auto const wsaData = createWSADataRaii();
    if (!wsaData) {
        print_err("WSAStartup failed");
        return 1;
    }

    print_std("using config:");
    print_std("server_port:        ", serverConfig->server_port);

    auto const hints = []()
    {
        auto _hints = addrinfo{};
        ZeroMemory(&_hints, sizeof(_hints));
        _hints.ai_family = AF_INET;
        _hints.ai_socktype = SOCK_STREAM;
        _hints.ai_protocol = IPPROTO_TCP;
        _hints.ai_flags = AI_PASSIVE;

        return _hints;
    } ();

    // Resolve the server address and port
    auto const clientAddrinfo = getaddrinfoRaii(nullptr, serverConfig->server_port.c_str(), &hints);
    if (!clientAddrinfo) {
        print_err("getaddrinfo failed");
        return 1;
    }

    // Create a SOCKET for connecting to server
    auto ListenSocket = createSocketRaii(clientAddrinfo->ai_family, clientAddrinfo->ai_socktype, clientAddrinfo->ai_protocol);
    if (!ListenSocket) {
        print_err("Failed to create listen socket: ", WSAGetLastError());
        return 1;
    }

    {
        // Setup the TCP listening socket
        auto const iResult = bind(*ListenSocket, clientAddrinfo->ai_addr, (int)clientAddrinfo->ai_addrlen);
        if (iResult == SOCKET_ERROR) {
            print_err("Failed to bind listen socket with error: ", WSAGetLastError());
            return 1;
        }
    }

    {
        auto const iResult = listen(*ListenSocket, SOMAXCONN);
        if (iResult == SOCKET_ERROR) {
            print_err("listen failed with error: ", WSAGetLastError());
            return 1;
        }
    }

    // Waiting for client and Accept socket
    auto connection = Connection{};
    connection.setSocket(accept(*ListenSocket, nullptr, nullptr));
    if (connection.is_socket_error()) {
        print_err("accept failed with error: ", WSAGetLastError());
        return 1;
    }

    // No longer need server socket
    ListenSocket.reset();

    // Buffer for Recieving/Sending data
    auto constexpr nBufferSize{ 1024u };
    auto buffer = std::vector<char>(nBufferSize);

    auto nRecvCounter = std::uint32_t{ 0 };
    auto nSendCounter = std::uint32_t{ 0 };

    auto const fileProcessConfig = [&]()
    {
        auto _fileProcessConfig = FileProcessConfig{};

        auto const nSize = connection.recv_val<std::uint32_t>();
        connection.recv(buffer.data(), nSize);

        auto jsonFileProcessConfig = nlohmann::json::parse(buffer.begin(), std::next(buffer.begin(), nSize));
        JSON_GET_AND_PARSE_MEMBER(jsonFileProcessConfig, _fileProcessConfig, timeouts, is_number_unsigned);
        JSON_GET_AND_PARSE_MEMBER(jsonFileProcessConfig, _fileProcessConfig, package_size, is_number_unsigned);
        JSON_GET_AND_PARSE_MEMBER(jsonFileProcessConfig, _fileProcessConfig, file_name, is_string);
        JSON_GET_AND_PARSE_MEMBER(jsonFileProcessConfig, _fileProcessConfig, maximum_errors, is_number_unsigned);

        return _fileProcessConfig;
    } ();

    if(buffer.size() < fileProcessConfig.package_size)
    {
        buffer.resize(fileProcessConfig.package_size);
    }

    auto timeData = std::vector<TimeData>{};
    timeData.reserve(fileProcessConfig.timeouts);

    for (std::size_t i{ 0 }; i < fileProcessConfig.timeouts; ++i)
    {
        timeData.emplace_back();

        auto const nTimeout = connection.recv_val<std::uint32_t>();
        auto const nFileSize = connection.recv_val<std::int64_t>();

        timeData.back().timeout = nTimeout;

        auto const strOutFileName = "out_"s + std::to_string(i) + "_"s + fileProcessConfig.file_name;
        auto fout = std::ofstream{ strOutFileName, std::ios::binary | std::ios::out };
        if (!fout) {
            print_err("Failed to open file ", strOutFileName);
            return 1;
        }

        print_std("---------------");
        print_std("--   START   --");
        print_std("---------------");
        print_std("Receiving file ", std::to_string(i));
        print_std("File name: ", strOutFileName);
        print_std("File size: ", nFileSize, " bytes");
        print_std("Timeout: ", nTimeout);
        print_std("---------------");

        if(fileProcessConfig.maximum_errors > 0)
        {
            auto tv = [&]()
            {
                auto _tv = timeval{};
                _tv.tv_sec = 0;
                _tv.tv_usec = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::milliseconds{nTimeout}).count();
                return _tv;
            } ();

            auto nErrorCounter = std::uint32_t{0};
            auto nErrorSeqCounter = std::uint32_t{0};
            auto nErrorMaxSeqCounter = std::uint32_t{0};

            auto nCurFileSize = std::int64_t{ 0 };

            timeData.back().recv_time = (std::uint32_t)exec_duration<std::chrono::microseconds>(
                    [&]()
                    {
                        while (nCurFileSize < nFileSize && nErrorSeqCounter < fileProcessConfig.maximum_errors)
                        {
                            fd_set fdRead;
                            FD_ZERO(&fdRead);
                            FD_SET(connection.getSocket(), &fdRead);
                            auto const iRet = select(0, &fdRead, nullptr, nullptr, &tv);
                            auto const bFdIsSet = FD_ISSET(connection.getSocket(), &fdRead);

                            if(iRet > 0 && bFdIsSet)
                            {
                                auto const nPackageSize = std::min<std::int64_t>(fileProcessConfig.package_size, nFileSize - nCurFileSize);

                                auto const iResult = connection.recv(buffer.data(), nPackageSize);

                                nErrorMaxSeqCounter = std::max(nErrorMaxSeqCounter, nErrorSeqCounter);
                                nErrorSeqCounter = 0;

                                fout.write(buffer.data(), iResult);
                                nCurFileSize += nPackageSize;
                            }
                            else
                            {
                                ++nErrorCounter;
                                ++nErrorSeqCounter;
                            }
                        }
                    }).count();

            print_std("---------------");

            if(nErrorMaxSeqCounter != fileProcessConfig.maximum_errors)
            {
                print_std("!! File received successfully");
            }
            else
            {
                print_err("?? File not received");
            }

            print_std("-- Recieved: ", nCurFileSize, " bytes");
            print_std("-- Errors: ", nErrorCounter);
            print_std("-- Max errors in sequence: ", nErrorMaxSeqCounter, "/", fileProcessConfig.maximum_errors);
            print_std("---------------");
            print_std();
        }
        else
        {
            connection.setsockopt(SOL_SOCKET, SO_RCVTIMEO, static_cast<int>(nTimeout));

            auto nCurFileSize = std::int64_t{ 0 };

            timeData.back().recv_time = (std::uint32_t)exec_duration<std::chrono::microseconds>(
                    [&]()
                    {
                        while (nCurFileSize < nFileSize)
                        {
                            auto const nPackageSize = std::min<std::int64_t>(fileProcessConfig.package_size, nFileSize - nCurFileSize);

                            connection.recv(buffer.data(), nPackageSize);

                            if(connection.is_socket_error())
                            {
                                break;
                            }
                            else
                            {
                                fout.write(buffer.data(), connection.getResult());
                                nCurFileSize += nPackageSize;
                            }
                        }
                    }).count();


            if(connection.is_socket_error())
                print_err("?? File not received");
            else
                print_std("!! File received successfully");

            print_std("-- Recieved: ", nCurFileSize, " bytes");
            print_std("---------------");
            print_std();

            if(connection.is_socket_error())
                return 1;
        }
    }


    {
        auto fout = std::ofstream{fileProcessConfig.file_name + ".csv"s};

        std::for_each(timeData.cbegin(), std::prev(timeData.cend()), [&](auto const& td)
        {
            fout << td.timeout << ",";
        });
        fout << timeData.back().timeout << std::endl;

        std::for_each(timeData.cbegin(), std::prev(timeData.cend()), [&](auto const& td)
        {
            fout << td.recv_time << ",";
        });
        fout << timeData.back().recv_time << std::endl;

    }


    {
        // shutdown the connection since we're done
        connection.shutdown(SD_SEND);
        if (connection.is_socket_error()) {
            print_err("shutdown failed with error: ", WSAGetLastError());
            return 1;
        }
    }

    return 0;
}