#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
#include <winscard.h>
#include "cardContext.h"

class Session : public std::enable_shared_from_this<Session> {
public:
    Session(websocketpp::server<websocketpp::config::asio>& server, websocketpp::connection_hdl);
    ~Session();
    uint64_t addContext(SCARDCONTEXT hContext);
    std::shared_ptr<CardContext> addCardContext();
    std::optional<SCARDCONTEXT> findContext(uint64_t virtualContext);
    std::shared_ptr<CardContext> findCardContext(uint64_t virtualCardHandle);
    void removeCardContext(uint64_t virtualContext);
    void removeCardHandle(uint64_t virtualCardHandle);
    void sendResponse(const casproxy::Response& res);
    std::mutex mutex;
    std::mutex connectionMutex;
    std::string ip;
    bool connected{true};

private:
    websocketpp::server<websocketpp::config::asio>& server;
    websocketpp::connection_hdl hdl;
    std::map<uint64_t, SCARDCONTEXT> mapContext;
    std::map<uint64_t, std::shared_ptr<CardContext>> mapCardContext;
    uint64_t nextContext{1};
    uint64_t nextCardHandle{1};

};