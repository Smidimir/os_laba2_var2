#include <os2var2_common.h>

#include <array>
#include <string>
#include <optional>
#include <fstream>
#include <thread>


// #pragma warning (disable : 4996)


struct ClientConfig
{
    bool deserialize(std::string const& configName)
    {
        auto bResult = true;

        auto clientConfigJson = nlohmann::json{};

        auto confgFile = std::ifstream{ configName };

        bResult &= confgFile.is_open();

        if(bResult)
        {
            confgFile >> clientConfigJson;

            if (confgFile.fail())
                return false;

            bResult &= JSON_GET_AND_PARSE(clientConfigJson, server_ip, is_string);
            bResult &= JSON_GET_AND_PARSE(clientConfigJson, server_port, is_string);
            bResult &= JSON_GET_AND_PARSE(clientConfigJson, package_size, is_number_unsigned);
            bResult &= JSON_GET_AND_PARSE(clientConfigJson, timeout, is_array);
            bResult &= JSON_GET_AND_PARSE(clientConfigJson, file_name, is_string);
            bResult &= JSON_GET_AND_PARSE(clientConfigJson, apply_socket_timeout, is_boolean);
            bResult &= JSON_GET_AND_PARSE(clientConfigJson, apply_select_timeout, is_boolean);
            bResult &= JSON_GET_AND_PARSE(clientConfigJson, number_of_tries, is_number_unsigned);
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
                        {"server_ip"            , server_ip           },
                        {"server_port"          , server_port         },
                        {"package_size"         , package_size        },
                        {"timeout"              , timeout             },
                        {"file_name"            , file_name           },
                        {"apply_socket_timeout" , apply_socket_timeout},
                        {"apply_select_timeout" , apply_select_timeout},
                        {"number_of_tries"      , number_of_tries     }
                    };
        }

        return bResult;
    }

    std::string                server_ip;
    std::string                server_port;
    std::uint32_t              package_size;
    std::vector<std::uint32_t> timeout;
    std::string                file_name;
    bool                       apply_socket_timeout;
    bool                       apply_select_timeout;
    std::uint32_t              number_of_tries;
};

int main(int argc, char **argv)
{
    auto const clientConfig = []()
    {
        auto const configName = std::string{"config_client.json"};

        auto config = std::optional<ClientConfig>{ ClientConfig{} };
        if (!config->deserialize(configName)) {
            config->server_ip = "localhost";
            config->server_port = "9999";
            config->package_size = 16;
            config->timeout = { 25, 50, 75 };
            config->file_name = "in.dat";
            config->apply_socket_timeout = true;
            config->apply_select_timeout = true;
            config->number_of_tries = 1;
            config->serialize(configName);

            print_std("Generated default config: ", configName);
            config = std::nullopt;
        }

        return config;
    } ();

    if(!clientConfig.has_value())
        return 0;

    // Initialize Winsock
    auto const wsaData = createWSADataRaii();
    if (!wsaData) {
        print_err("WSAStartup failed");
        return 1;
    }

    print_std("using config:");
    print_std("server_ip:    ", clientConfig->server_ip);
    print_std("server_port:  ", clientConfig->server_port);
    print_std("package_size: ", clientConfig->package_size);

    auto const hints = []()
    {
        auto _hints = addrinfo{};
        ZeroMemory(&_hints, sizeof(_hints));
        _hints.ai_family = AF_UNSPEC;
        _hints.ai_socktype = SOCK_STREAM;
        _hints.ai_protocol = IPPROTO_TCP;

        return _hints;
    } ();

    // Resolve the server address and port

    auto const serverAddrinfo = getaddrinfoRaii(clientConfig->server_ip.c_str(), clientConfig->server_port.c_str(), &hints);
    if (!serverAddrinfo) {
        print_err("getaddrinfo failed: ");
        return 1;
    }

    auto connection = [&serverAddrinfo]()
    {
        auto _connection = Connection{};

        // Attempt to connect to an address until one succeeds
        for (addrinfo* ptr = serverAddrinfo.get(); ptr != nullptr; ptr = ptr->ai_next)
        {

            // Create a SOCKET for connecting to server
            _connection.setSocket(socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol));
            if (!_connection.is_valid()) {
                print_err("createSocket failed with error: ", WSAGetLastError());
                break;
            }

            // Connect to server.
            _connection.connect(*ptr);
            if (_connection.is_socket_error())
            {
                _connection.reset();
                continue;
            }

            return _connection;
        }

        _connection.reset();
        return _connection;
    } ();

    if (!connection.is_valid()) {
        print_err("Unable to connect to server");
        return 1;
    }
    print_std("Connected successfully");

    // Buffer for Recieving/Sending data
    auto buffer = std::vector<char>(clientConfig->package_size);

    {
        auto fileProcessConfig = FileProcessConfig{};
        fileProcessConfig.timeouts = clientConfig->timeout.size();
        fileProcessConfig.package_size = clientConfig->package_size;
        fileProcessConfig.file_name = clientConfig->file_name;

        auto const strFileProcessConfig = nlohmann::json
            {
                {"timeouts"      , fileProcessConfig.timeouts      },
                {"package_size"  , fileProcessConfig.package_size  },
                {"file_name"     , fileProcessConfig.file_name     },
            }.dump();

        // Send number of timeouts to server
        auto const nSize = static_cast<std::uint32_t>(strFileProcessConfig.size());
        connection.send_val(nSize);
        connection.send(strFileProcessConfig.data(), nSize);
        if (connection.is_socket_error()) {
            print_err("Failed to send number of timeouts to server with error: ", WSAGetLastError());
            return 1;
        }
    }

//    connection.setsockopt(SOL_SOCKET, SO_SNDBUF, static_cast<int>(clientConfig->package_size));

    auto const defaultSendTime = static_cast<int>( std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::seconds{30}).count() );

    connection.send_val(clientConfig->number_of_tries);
    auto const nTries = clientConfig->number_of_tries;
    for(int nTry = 0; nTry < nTries; ++nTry)
    {
        auto nFileCounter = std::uint32_t{ 0 };
        for (auto const& nTimeout : clientConfig->timeout)
        {
            auto tv = [&]()
            {
                auto _tv = timeval{};
                _tv.tv_sec = 0;
                _tv.tv_usec = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::milliseconds{nTimeout}).count();
                return _tv;
            } ();

            {
                // Send timeout to server
                connection.send_val(nTimeout);
                if (connection.is_socket_error()) {
                    print_err("Failed to send current timeout to server with error: ", WSAGetLastError());
                    return 1;
                }
            }

            auto const nFileSize = [&]()
            {
                auto fin = std::ifstream{ clientConfig->file_name, std::ios::binary };
                if (!fin) {
                    print_err("Failed to open file: ", clientConfig->file_name);
                    return std::int64_t{-1};
                }

                fin.seekg(0, std::ios::end);
                auto const _nFileSize = std::int64_t{ fin.tellg() };
                fin.seekg(0, std::ios::beg);

                buffer.resize(_nFileSize);
                fin.read(buffer.data(), _nFileSize);

                // Send timeout to server
                connection.send_val(_nFileSize);
                return _nFileSize;
            } ();

            if(nFileSize == -1)
                return 1;

            print_std(":: try: ", nTry, ", file: ", std::to_string(nFileCounter), " - ", clientConfig->file_name, ", file size: ",  nFileSize, " bytes", ", timeout: ", nTimeout);


            {
                if(clientConfig->apply_socket_timeout)
                {
                    connection.setsockopt(SOL_SOCKET, SO_SNDTIMEO, static_cast<int>(nTimeout));
                }

                auto nCurFileSize = std::int64_t{ 0 };

                while (nCurFileSize < nFileSize)
                {
                    auto const iRet = [&]()
                    {
                        if(clientConfig->apply_select_timeout)
                        {
                            fd_set fdWrite;
                            FD_ZERO(&fdWrite);
                            FD_SET(connection.getSocket(), &fdWrite);
                            return select(0, nullptr, &fdWrite, nullptr, &tv);
                        }
                        else
                        {
                            return 1;
                        }
                    } ();

                    if(iRet > 0)
                    {
                        auto const nBytesReed = std::min<std::int64_t>(clientConfig->package_size, nFileSize - nCurFileSize);

                        connection.send(buffer.data(), nBytesReed);

                        if(!connection.is_socket_error())
                        {
                            nCurFileSize += connection.getResult();
                        }
                        else
                        {
                            break;
                        }
                    }
                }

                if(clientConfig->apply_socket_timeout && !connection.is_socket_error())
                {
                    connection.setsockopt(SOL_SOCKET, SO_SNDTIMEO, defaultSendTime);
                }

                print_std("-- Sent: ", nCurFileSize, " bytes");
                print_std("---------------");
                print_std();

                if(!connection.is_socket_error())
                {
                    print_std("!! File sent successfully");
                }
                else
                {
                    break;
                }
            }

            ++nFileCounter;
        }
    }



    {
        // shutdown the connection since no more data will be sent
        connection.shutdown(SD_SEND);
        if (connection.is_socket_error()) {
            print_err("shutdown failed with error: ", WSAGetLastError());
            return 1;
        }
        print_std("Connection shutdown");
    }

    // Receive until the peer closes the connection
    do
    {
        connection.recv(buffer.data(), buffer.size());

        if (connection.getResult() > 0)
            print_std("Bytes received: ", connection.getResult());
        else if(connection.is_socket_error())
            print_err("recv failed with error: ", WSAGetLastError());
        else
            print_std("Connection closed");
    } while(connection.getResult() > 0);

    return 0;
}
