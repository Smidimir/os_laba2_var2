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

            bResult &= JSON_GET_AND_PARSE(serverConfigJson, server_port         , is_string);
            bResult &= JSON_GET_AND_PARSE(serverConfigJson, apply_socket_timeout, is_boolean);
            bResult &= JSON_GET_AND_PARSE(serverConfigJson, apply_select_timeout, is_boolean);
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
                    {"server_port"          , server_port         },
                    {"apply_socket_timeout" , apply_socket_timeout},
                    {"apply_select_timeout" , apply_select_timeout}
                };
        }

        return bResult;
    }

    std::string server_port;
    bool        apply_socket_timeout;
    bool        apply_select_timeout;
};

struct TimeData
{
    std::uint32_t timeout;
    std::int64_t recv_time;
};

int main(int argc, char** argv)
{
    auto const serverConfig = []()
    {
        auto const configName = std::string{"config_server.json"};

        auto config = std::optional<ServerConfig>{ ServerConfig{} };
        if (!config->deserialize(configName)) {
            config->server_port = "9999";
            config->apply_socket_timeout = true;
            config->apply_select_timeout = true;
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

        return _fileProcessConfig;
    } ();

    auto timeData = std::vector<TimeData>{};
    timeData.resize(fileProcessConfig.timeouts);

    auto const defaultRecvTime = static_cast<int>( std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::seconds{30}).count() );

//    connection.setsockopt(SOL_SOCKET, SO_RCVBUF, static_cast<int>(fileProcessConfig.package_size));

    auto const nTries = connection.recv_val<std::uint32_t>();
    for(int nTry = 0; nTry < nTries; ++nTry)
    {
        auto itTimeData = timeData.begin();

        for (std::size_t i{ 0 }; i < fileProcessConfig.timeouts; ++i)
        {
            auto const nTimeout = connection.recv_val<std::uint32_t>();

            auto tv = [&]()
            {
                auto _tv = timeval{};
                _tv.tv_sec = 0;
                _tv.tv_usec = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::milliseconds{nTimeout}).count();
                return _tv;
            } ();

            auto const nFileSize = connection.recv_val<std::int64_t>();
            if(connection.is_socket_error())
            {
                break;
            }

            buffer.resize(nFileSize);

            itTimeData->timeout = nTimeout;

            auto const strOutFileName = "out_"s + std::to_string(i) + "_"s + fileProcessConfig.file_name;

            print_std(":: try: ", nTry, ", file: ", std::to_string(i), " - ", strOutFileName, ", file size: ",  nFileSize, " bytes", ", timeout: ", nTimeout);


            {
                if(serverConfig->apply_socket_timeout)
                {
                    connection.setsockopt(SOL_SOCKET, SO_RCVTIMEO, static_cast<int>(nTimeout));
                }

                auto nCurFileSize = std::int64_t{ 0 };

                itTimeData->recv_time += exec_duration_windows<std::chrono::microseconds>(
                        [&]()
                        {
                            while (nCurFileSize < nFileSize)
                            {
                                auto const iRet = [&]()
                                {
                                    if(serverConfig->apply_select_timeout)
                                    {
                                        fd_set fdRead;
                                        FD_ZERO(&fdRead);
                                        FD_SET(connection.getSocket(), &fdRead);
                                        return select(0, &fdRead, nullptr, nullptr, &tv);
                                    }
                                    else
                                    {
                                        return 1;
                                    }
                                } ();


                                if(iRet > 0)
                                {
                                    auto const nPackageSize = std::min<std::int64_t>(fileProcessConfig.package_size, nFileSize - nCurFileSize);

                                    connection.recv(buffer.data() + nCurFileSize, nPackageSize);

                                    if(!connection.is_socket_error())
                                    {
//                                    print_std(":: !! recv: ", connection.getResult(), ", size: ", nCurFileSize);
                                        nCurFileSize += connection.getResult();
                                    }
                                    else
                                    {
//                                    print_std(":: ?? error");
                                        print_err(":: connection.getResult(): ", connection.getResult(), " : ", WSAGetLastError());
                                        break;
                                    }
                                }
                                else
                                {
                                    print_std(":: skip package", iRet);
                                }
                            }
                        }).count();

                if(serverConfig->apply_socket_timeout && !connection.is_socket_error())
                {
                    connection.setsockopt(SOL_SOCKET, SO_RCVTIMEO, defaultRecvTime);
                }

                if(connection.is_socket_error())
                {
                    print_err("?? File not received");
                    break;
                }
                else
                {
                    print_std("!! File received successfully");
                }

                print_std("-- Recieved: ", nCurFileSize, " bytes");
                print_std("---------------");
                print_std();
            }

            {
                auto fout = std::ofstream{ strOutFileName, std::ios::binary | std::ios::out };
                if (!fout) {
                    print_err("Failed to open file ", strOutFileName);
                    return 1;
                }

                fout.write(buffer.data(), nFileSize);
            }

            ++itTimeData;
        }
    }


    {
        auto fout = std::ofstream{fileProcessConfig.file_name + ".csv"s};

        std::for_each(timeData.cbegin(), std::prev(timeData.cend()), [&](auto const& td)
        {
            fout << td.timeout << ",";
        });
        fout << timeData.back().timeout << std::endl;

        std::for_each(timeData.begin(), std::prev(timeData.end()), [&](auto& td)
        {
            td.recv_time /= nTries;
            fout << td.recv_time << ",";
        });
        timeData.back().recv_time /= nTries;
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