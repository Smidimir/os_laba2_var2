#include <os2var2_common.h>

#include <chrono>
#include <thread>
#include <array>
#include <algorithm>
#include <future>


int main(int argc, char** argv)
{
    // Initialize Winsock
    auto const wsaData = createWSADataRaii();
    if (!wsaData) {
        print_err("WSAStartup failed with");
        return 1;
    }

    auto n_send = std::size_t{0};
    auto n_recv = std::size_t{0};

    auto const tv = DWORD{25};

    auto send_thread = std::thread{
    [&n_send, &tv]()
    {
        auto buffer = std::array<char, 1024>{};

        auto iResult = int{0};

        auto hints = addrinfo{};
        ZeroMemory(&hints, sizeof(hints));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        // Resolve the server address and port
        auto result = getaddrinfoRaii("localhost", "777", &hints);
        print_std("send_thread: ", "resolve address and port");

        auto ConnectSocket = SocketRaii{};

        // Attempt to connect to an address until one succeeds
        for (addrinfo* ptr = result.get(); ptr != nullptr; ptr = ptr->ai_next)
        {

            // Create a SOCKET for connecting to server
            ConnectSocket = createSocketRaii(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);

            // Connect to server.
            iResult = connect(*ConnectSocket, ptr->ai_addr, (int)ptr->ai_addrlen);
            if (iResult == SOCKET_ERROR) {
                ConnectSocket.reset();
                continue;
            }

            print_std("send_thread: ", "connected");
            break;
        }

        // TODO
        setsockopt(*ConnectSocket, SOL_SOCKET, SO_SNDTIMEO, (char*)&tv, sizeof(tv));
        setsockopt(*ConnectSocket, SOL_SOCKET, SO_RCVTIMEO, (char*)&tv, sizeof(tv));


        auto send_time_sum = std::uint64_t{ 0 };
        auto send_time_min = ~std::uint64_t{ 0 };
        auto send_time_max = std::uint64_t{ 0 };

        auto const max_error_count = std::uint64_t{ 5000 };
        auto send_error_cunter_all = std::uint64_t{ 0 };
        auto send_error_cunter_seq = std::uint64_t{ 0 };
        auto send_error_cunter_seq_max = std::uint64_t{ 0 };

        auto const time = exec_duration([&]()
        {
            for (std::size_t i{0}; i < 100000; ++i)
            {
                auto bStop = false;
                auto const time = exec_duration<std::chrono::microseconds>([&]()
                {
                    iResult = send(*ConnectSocket, buffer.data(), 16, 0);
                    if (iResult == SOCKET_ERROR)
                    {
                        ++send_error_cunter_all;
                        ++send_error_cunter_seq;
                        bStop = send_error_cunter_seq >= max_error_count;
                    }
                    else
                    {
                        n_send += iResult;
                        send_error_cunter_seq = 0;
                    }

                    send_error_cunter_seq_max = std::max(send_error_cunter_seq_max, send_error_cunter_seq);
               }).count();

                send_time_sum += time;
                send_time_min = std::min<std::uint64_t>(send_time_min, time);
                send_time_max = std::max<std::uint64_t>(send_time_max, time);

                if (bStop)
                    break;
//                    print_std("send_thread: ", "send >> ", i, " (", iResult, ")");
            }
        });

        auto const avg_send_time = send_time_sum / 100000;

        print_std("send time: ", time.count(), " ms", ", avg: ", avg_send_time, ", min: ", send_time_min, ", max: ", send_time_max, ", errors: ", send_error_cunter_all, ", seq errors: ", send_error_cunter_seq_max);

    }
    };

    auto recv_thread = std::thread{
        [&n_recv, &tv]()
        {
            auto buffer = std::array<char, 1024>{};

            auto iResult = int{0};

            auto hints = addrinfo{};
            ZeroMemory(&hints, sizeof(hints));
            hints.ai_family = AF_INET;
            hints.ai_socktype = SOCK_STREAM;
            hints.ai_protocol = IPPROTO_TCP;
            hints.ai_flags = AI_PASSIVE;

            // Resolve the server address and port
            auto result = getaddrinfoRaii(nullptr, "777", &hints);
            print_std("recv_thread: ", "resolve address and port");

            // Create a SOCKET for connecting to server
            auto ListenSocket = createSocketRaii(result->ai_family, result->ai_socktype, result->ai_protocol);

            // Setup the TCP listening socket
            iResult = bind(*ListenSocket, result->ai_addr, (int)result->ai_addrlen);

            iResult = listen(*ListenSocket, SOMAXCONN);
            if(iResult == SOCKET_ERROR) return;
            print_std("recv_thread: ", "listen");

            // Waiting for client and Accept socket
            auto ClientSocket = acceptSocketRaii(*ListenSocket, nullptr, nullptr);
            print_std("recv_thread: ", "connected");

            // No longer need server socket
            ListenSocket.reset();

            //// TODO
            //setsockopt(*ClientSocket, SOL_SOCKET, SO_SNDTIMEO, (char*)&tv, sizeof(tv));
            setsockopt(*ClientSocket, SOL_SOCKET, SO_RCVTIMEO, (char*)&tv, sizeof(tv));

            auto coutnter = std::uint64_t{ 0 };

            auto const time = exec_duration([&]()
            {
                for(std::size_t i{0}; i < 100000; ++i)
                {
                    iResult = recv(*ClientSocket, buffer.data(), 16, 0);
                    if (iResult == SOCKET_ERROR)
                    {
                        --i;
                        ++coutnter;
                    }
                    else
                    {
                        n_recv += iResult;
                    }

    //                std::this_thread::sleep_for(std::chrono::milliseconds{1000});

    //                print_std("recv_thread: ", "recv << ", i, " (", iResult, ")");
                }
            });

            print_std("recv time: ", time.count(), " ms", ", COUNTER: ", coutnter);
        }
    };

    send_thread.join();
    recv_thread.join();

    print_std("n_send: ", n_send);
    print_std("n_recv: ", n_recv);

    return 0;
}