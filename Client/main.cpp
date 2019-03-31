#include <os2var2_common.h>

#include <array>
#include <string>
#include <optional>
#include <fstream>


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
            bResult &= JSON_GET_AND_PARSE(clientConfigJson, maximum_errors, is_number_unsigned);
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
                        {"server_ip"   , server_ip   },
                        {"server_port" , server_port },
                        {"package_size", package_size},
                        {"timeout"     , timeout     },
                        {"file_name"   , file_name   }
                    };
        }

        return bResult;
    }

    std::string                server_ip;
    std::string                server_port;
    std::uint32_t              package_size;
    std::vector<std::uint32_t> timeout;
    std::string                file_name;
    std::uint32_t              maximum_errors;
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
            config->maximum_errors = 10;
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
        fileProcessConfig.maximum_errors = clientConfig->maximum_errors;

        auto const strFileProcessConfig = nlohmann::json
            {
                {"timeouts"      , fileProcessConfig.timeouts      },
                {"package_size"  , fileProcessConfig.package_size  },
                {"file_name"     , fileProcessConfig.file_name     },
                {"maximum_errors", fileProcessConfig.maximum_errors}
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

    auto nFileCounter = std::uint32_t{ 0 };
    for (auto const& nTimeout : clientConfig->timeout)
    {

        auto fin = std::ifstream{ clientConfig->file_name, std::ios::binary };
        if (!fin) {
            print_err("Failed to open file: ", clientConfig->file_name);
            return 1;
        }

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
            fin.seekg(0, std::ios::end);
            auto const _nFileSize = std::int64_t{ fin.tellg() };
            fin.seekg(0, std::ios::beg);

            // Send timeout to server
            connection.send_val(_nFileSize);
            return _nFileSize;
        } ();

        print_std("---------------");
        print_std("--   START   --");
        print_std("---------------");
        print_std("Receiving file ", std::to_string(nFileCounter));
        print_std("File name: ", clientConfig->file_name);
        print_std("File size: ", nFileSize, " bytes");
        print_std("Timeout: ", nTimeout);
        print_std("---------------");

        if(clientConfig->maximum_errors > 0)
        {
            auto nErrorCounter = std::uint32_t{0};
            auto nErrorSeqCounter = std::uint32_t{0};
            auto nErrorMaxSeqCounter = std::uint32_t{0};

            auto nCurFileSize = std::int64_t{ 0 };

//        auto const tv = static_cast<int>(nTimeout);
//        setsockopt(*connectSocket, SOL_SOCKET, SO_SNDTIMEO, (char*)&tv, sizeof(tv));

            auto tv = [&]()
            {
                auto _tv = timeval{};
                _tv.tv_sec = 0;
                _tv.tv_usec = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::milliseconds{nTimeout}).count();
                return _tv;
            } ();

            while (nCurFileSize < nFileSize && nErrorMaxSeqCounter < clientConfig->maximum_errors)
            {
                // Send/Receive loop
                if(nErrorSeqCounter == 0)
                {
                    fin.read(buffer.data(), buffer.size());
                }

                fd_set fdWrite;
                FD_ZERO(&fdWrite);
                FD_SET(connection.getSocket(), &fdWrite);
                auto const iRet = select(0, nullptr, &fdWrite, nullptr, &tv);
                auto const bFdIsSet = FD_ISSET(connection.getSocket(), &fdWrite);

                if(iRet > 0 && bFdIsSet)
                {
                    auto const nBytesReed = fin.gcount();
                    connection.send(buffer.data(), nBytesReed);

                    nErrorMaxSeqCounter = std::max(nErrorMaxSeqCounter, nErrorSeqCounter);
                    nErrorSeqCounter = 0;

                    nCurFileSize += nBytesReed;
                }
                else
                {
                    ++nErrorCounter;
                    ++nErrorSeqCounter;
                }
            }

            if(nErrorMaxSeqCounter != clientConfig->maximum_errors)
            {
                print_std("!! File sent successfully");
            }
            else
            {
                print_err("?? File not sent");
            }

            print_std("-- Sent: ", nCurFileSize, " bytes");
            print_std("-- Errors: ", nErrorCounter);
            print_std("-- Max errors in sequence: ", nErrorMaxSeqCounter, "/", clientConfig->maximum_errors);

            print_std("---------------");
            print_std();
        }
        else
        {
            connection.setsockopt(SOL_SOCKET, SO_SNDTIMEO, static_cast<int>(nTimeout));

            auto nCurFileSize = std::int64_t{ 0 };

            while (nCurFileSize < nFileSize && !connection.is_socket_error())
            {
                fin.read(buffer.data(), buffer.size());

                auto const nBytesReed = fin.gcount();
                connection.send(buffer.data(), nBytesReed);

                nCurFileSize += nBytesReed;
            }

            if(connection.is_socket_error())
            {
                print_err("?? File not sent");
            }
            else
            {
                print_std("!! File sent successfully");
            }

            print_std("-- Sent: ", nCurFileSize, " bytes");
            print_std("---------------");
            print_std();
        }

        ++nFileCounter;
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
