#include "session.h"

Session::Session(websocketpp::server<websocketpp::config::asio>& server, websocketpp::connection_hdl hdl)
    : server(server), hdl(hdl) {
}

Session::~Session() {
    for (const auto& [virtualHandle, context] : mapCardContext) {
        if (context->hCard) {
            SCardDisconnect(context->hCard, SCARD_LEAVE_CARD);
        }
        context->stop();
    }
    mapCardContext.clear();

    for (const auto& [virtualContext, hContext] : mapContext) {
        SCardReleaseContext(hContext);
    }
    mapContext.clear();
}

uint64_t Session::addContext(SCARDCONTEXT hContext) {
    uint64_t virtualContext = nextContext;
    mapContext[virtualContext] = hContext;
    ++nextContext;

    if (nextContext == 0xFFFFFFFF) {
        ++nextContext;
    }

    if (nextContext == 0) {
        ++nextContext;
    }

    return virtualContext;
}

std::shared_ptr<CardContext> Session::addCardContext() {
    uint64_t virtualCardHandle = nextCardHandle;
    mapCardContext[virtualCardHandle] = std::make_shared<CardContext>(shared_from_this(), virtualCardHandle);
    ++nextCardHandle;

    if (nextCardHandle == 0xFFFFFFFF) {
        ++nextCardHandle;
    }

    if (nextCardHandle == 0) {
        ++nextCardHandle;
    }

    return mapCardContext[virtualCardHandle];
}

std::optional<SCARDCONTEXT> Session::findContext(uint64_t virtualContext) {
    if (auto it = mapContext.find(virtualContext); it != mapContext.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::shared_ptr<CardContext> Session::findCardContext(uint64_t virtualCardHandle) {
    if (auto it = mapCardContext.find(virtualCardHandle); it != mapCardContext.end()) {
        return it->second;
    }
    return nullptr;
}

void Session::removeCardContext(uint64_t virtualContext) {
    mapContext.erase(virtualContext);
}

void Session::removeCardHandle(uint64_t virtualCardHandle) {
    mapCardContext[virtualCardHandle]->stop();
    mapCardContext.erase(virtualCardHandle);
}

void Session::sendResponse(const casproxy::Response& res) {
    if (!connected) {
        return;
    }

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    res.toJson().Accept(writer);

    std::string output = buffer.GetString();
    server.get_io_service().post([this, output]() {
        std::lock_guard<std::mutex> lock(this->connectionMutex);
        if (!connected) {
            return;
        }

        this->server.send(this->hdl, output, websocketpp::frame::opcode::text);
    });
}
