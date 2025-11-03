#include <winscard.h>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <thread>
#include <fstream>
#include <optional>
#include <chrono>
#include <asio.hpp>
#include "casProxy.h"
#include "config.h"
#include "session.h"

#ifdef _WIN32
constexpr const char* defaultConfigPath = "config.yml";
#else
constexpr const char* defaultConfigPath = "/usr/local/etc/casproxyserver.yml";
#endif

namespace {

std::string currentTime() {
    using clock = std::chrono::system_clock;
    auto now = clock::to_time_t(clock::now());
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

}

class CasProxyServer {
public:
    CasProxyServer() {
    }

    void run(const std::string configFilePath) {
        config.loadConfig(configFilePath);

        asio::error_code ec;
        asio::ip::address addr = asio::ip::make_address(config.listenIp, ec);
        if (ec) {
            throw std::runtime_error("Invalid address: " + ec.message());
        }

        acceptor = std::make_unique<asio::ip::tcp::acceptor>(io_context);
        acceptor->open(asio::ip::tcp::v4());
        acceptor->bind({ addr, config.port });
        acceptor->listen();

        std::cout << "casproxyserver listening on " << config.listenIp << ":" << config.port << std::endl;
        startAccept();
        io_context.run();
    }


private:
    void startAccept() {
        acceptor->async_accept(
            [this](std::error_code ec, asio::ip::tcp::socket socket) {
                if (!ec) {
                    std::string ip = socket.remote_endpoint().address().to_string();
                    if (!config.isAllowedIp(ip)) {
                        socket.close();
                    }
                    else {
                        auto session = std::make_shared<Session>(std::move(socket),
                            [this](std::shared_ptr<Session> s) { onClose(s); });
                        session->ip = ip;
                        mapSession[session.get()] = session;

                        std::cout << session->ip << " - " << currentTime() << " - New connection" << "\n";
                        session->doRead();
                    }
                }
                startAccept();
            }
        );
    }

    void onClose(std::shared_ptr<Session> session) {
        std::cout << session->ip << " - " << currentTime() << " - Connection closed" << "\n";
        mapSession.erase(session.get());
    }

    asio::io_context io_context;
    std::unique_ptr<asio::ip::tcp::acceptor> acceptor;
    Config config;
    std::map<void*, std::shared_ptr<Session>> mapSession;
    std::mutex mutex;

};

int main(int argc, char* argv[]) {
    try {
        CasProxyServer server;
        std::string configFilePath = defaultConfigPath;
        if (argc > 1) {
            configFilePath = argv[1];
        }

        server.run(configFilePath);
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}