#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
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
#include "casProxy.h"
#include "config.h"
#include "session.h"

#ifdef _WIN32
constexpr const char* defaultConfigPath = "config.yaml";
#else
constexpr const char* defaultConfigPath = "/usr/local/etc/casproxyserver.yaml";
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
        server.init_asio();
        server.clear_access_channels(websocketpp::log::alevel::all);
        server.clear_error_channels(websocketpp::log::elevel::all);

        using websocketpp::lib::bind;
        using websocketpp::lib::placeholders::_1;
        using websocketpp::lib::placeholders::_2;
        server.set_open_handler(bind(&CasProxyServer::onOpen, this, _1));
        server.set_close_handler(bind(&CasProxyServer::onClose, this, _1));
        server.set_message_handler(bind(&CasProxyServer::onMessage, this, _1, _2));
    }

    void run(const std::string configFilePath) {
        config.loadConfig(configFilePath);

        server.listen(config.listenIp, std::to_string(config.port));
        server.start_accept();

        std::cout << "casproxyserver listening on " << config.listenIp << ":" << config.port << std::endl;
        server.run();
    }

private:
    void onOpen(websocketpp::connection_hdl hdl) {
        auto con = server.get_con_from_hdl(hdl);
        std::string clientIp = con->get_remote_endpoint();
        auto pos = clientIp.find_last_of(':');
        if (pos != std::string::npos) {
            clientIp = clientIp.substr(0, pos);
        }

        if (!config.isAllowedIp(clientIp)) {
            server.close(hdl, websocketpp::close::status::policy_violation, "IP address not allowed");
            return;
        }

        mapSession[hdl] = std::make_shared<Session>(server, hdl);
        mapSession[hdl]->ip = clientIp;

        std::cout << "[" << currentTime() << "] New connection: " << clientIp << "\n";
    }

    void onClose(websocketpp::connection_hdl hdl) {
        std::cout << "[" << currentTime() << "] Connection closed: " << mapSession[hdl]->ip << "\n";
        {
            std::lock_guard<std::mutex> lock(mapSession[hdl]->connectionMutex);
            mapSession[hdl]->connected = false;
        }
        mapSession.erase(hdl);
    }

    void onMessage(websocketpp::connection_hdl hdl, websocketpp::server<websocketpp::config::asio>::message_ptr msg) {
        rapidjson::Document doc;
        doc.Parse(msg->get_payload().c_str());

        if (doc.HasParseError()) {
            return;
        }

        casproxy::BaseRequest baseReq;
        if (!baseReq.fromJson(doc)) {
            return;
        }

        if (baseReq.command == casproxy::kSCardEstablishContext) {
            handleSCardEstablishContext(hdl, msg->get_payload());
        }
        else if (baseReq.command == casproxy::kSCardReleaseContext) {
            handleSCardReleaseContext(hdl, msg->get_payload());
        }
        else if (baseReq.command == casproxy::kSCardListReaders) {
            handleSCardListReaders(hdl, msg->get_payload());
        }
        else if (baseReq.command == casproxy::kSCardConnect) {
            handleSCardConnect(hdl, msg->get_payload());
        }
        else if (baseReq.command == casproxy::kSCardDisconnect) {
            handleSCardDisconnect(hdl, msg->get_payload());
        }
        else if (baseReq.command == casproxy::kSCardBeginTransaction) {
            handleSCardBeginTransaction(hdl, msg->get_payload());
        }
        else if (baseReq.command == casproxy::kSCardEndTransaction) {
            handleSCardEndTransaction(hdl, msg->get_payload());
        }
        else if (baseReq.command == casproxy::kSCardTransmit) {
            handleSCardTransmit(hdl, msg->get_payload());
        }
        else if (baseReq.command == casproxy::kSCardGetAttrib) {
            handleSCardGetAttrib(hdl, msg->get_payload());
        }
        else {
            sendError(hdl, baseReq.packetId, "Unknown command");
        }
    }

    void handleSCardEstablishContext(websocketpp::connection_hdl hdl, const std::string& msg) {
        rapidjson::Document doc;
        doc.Parse(msg.c_str());

        casproxy::SCardEstablishContextRequest req;
        if (!req.fromJson(doc)) {
            return;
        }

        auto session = getSession(hdl);
        std::lock_guard<std::mutex> lock(session->mutex);

        SCARDCONTEXT hContext = 0;
        LONG returnValue = SCardEstablishContext(req.dwScope, nullptr, nullptr, &hContext);

        uint64_t virtualContext = 0;
        if (hContext != 0) {
            virtualContext = session->addContext(hContext);
        }

        casproxy::SCardEstablishContextResponse res;
        res.packetId = req.packetId;
        res.apiReturn = returnValue;
        res.hContext = virtualContext;
        sendResponse(hdl, res);
    }

    void handleSCardReleaseContext(websocketpp::connection_hdl hdl, const std::string& msg) {
        rapidjson::Document doc;
        doc.Parse(msg.c_str());

        casproxy::SCardReleaseContextRequest req;
        if (!req.fromJson(doc)) {
            return;
        }

        auto session = getSession(hdl);
        std::lock_guard<std::mutex> lock(session->mutex);

        const auto hNativeContext = session->findContext(req.hContext);
        if (!hNativeContext) {
            casproxy::SCardEstablishContextResponse res;
            res.packetId = req.packetId;
            res.apiReturn = SCARD_E_INVALID_HANDLE;
            sendResponse(hdl, res);
            return;
        }

        LONG returnValue = SCardReleaseContext(*hNativeContext);
        if (returnValue == SCARD_S_SUCCESS) {
            session->removeCardContext(req.hContext);
        }

        casproxy::SCardReleaseContextResponse res;
        res.packetId = req.packetId;
        res.apiReturn = returnValue;
        sendResponse(hdl, res);
    }

    void handleSCardListReaders(websocketpp::connection_hdl hdl, const std::string& msg) {
        rapidjson::Document doc;
        doc.Parse(msg.c_str());

        casproxy::SCardListReadersRequest req;
        if (!req.fromJson(doc)) {
            return;
        }

        if (req.readersLength > 25600) {
            casproxy::SCardListReadersResponse res;
            res.packetId = req.packetId;
            res.apiReturn = SCARD_E_INSUFFICIENT_BUFFER;
            sendResponse(hdl, res);
            return;
        }

        auto session = getSession(hdl);
        std::lock_guard<std::mutex> lock(session->mutex);

        const auto hNativeContext = session->findContext(req.hContext);
        if (!hNativeContext) {
            casproxy::SCardListReadersResponse res;
            res.packetId = req.packetId;
            res.apiReturn = SCARD_E_INVALID_HANDLE;
            sendResponse(hdl, res);
            return;
        }

        DWORD readersLength = req.readersLength;
        std::vector<uint8_t> readersBuffer(req.readersLength);
        LONG returnValue = SCardListReaders(*hNativeContext, req.isGroupsNull ? nullptr : (char*)req.groups.data(),
            req.readersLength == 0 ? nullptr : (char*)readersBuffer.data(), &readersLength);

        if (readersLength < req.readersLength) {
            readersBuffer.resize(static_cast<uint32_t>(readersLength));
        }

        casproxy::SCardListReadersResponse res;
        res.packetId = req.packetId;
        res.apiReturn = returnValue;
        res.readers = readersBuffer;
        res.readersLength = readersLength;
        sendResponse(hdl, res);
    }

    void handleSCardConnect(websocketpp::connection_hdl hdl, const std::string& msg) {
        rapidjson::Document doc;
        doc.Parse(msg.c_str());

        casproxy::SCardConnectRequest req;
        if (!req.fromJson(doc)) {
            return;
        }

        auto session = getSession(hdl);
        std::lock_guard<std::mutex> lock(session->mutex);

        const auto hNativeContext = session->findContext(req.hContext);
        if (!hNativeContext) {
            casproxy::SCardEstablishContextResponse res;
            res.packetId = req.packetId;
            res.apiReturn = SCARD_E_INVALID_HANDLE;
            sendResponse(hdl, res);
            return;
        }

        auto cardContext = session->addCardContext();
        cardContext->addTask(msg);

        std::thread thread([cardContext]() {
            cardContext->run();
            });
        thread.detach();
    }

    void handleSCardDisconnect(websocketpp::connection_hdl hdl, const std::string& msg) {
        rapidjson::Document doc;
        doc.Parse(msg.c_str());

        casproxy::SCardDisconnectRequest req;
        if (!req.fromJson(doc)) {
            return;
        }

        auto session = getSession(hdl);
        std::lock_guard<std::mutex> lock(session->mutex);

        const auto cardContext = session->findCardContext(req.hCard);
        if (!cardContext) {
            casproxy::SCardDisconnectResponse res;
            res.packetId = req.packetId;
            res.apiReturn = SCARD_E_INVALID_HANDLE;
            sendResponse(hdl, res);
            return;
        }

        cardContext->addTask(msg);
    }

    void handleSCardBeginTransaction(websocketpp::connection_hdl hdl, const std::string& msg) {
        rapidjson::Document doc;
        doc.Parse(msg.c_str());

        casproxy::SCardBeginTransactionRequest req;
        if (!req.fromJson(doc)) {
            return;
        }

        auto session = getSession(hdl);
        std::lock_guard<std::mutex> lock(session->mutex);

        const auto cardContext = session->findCardContext(req.hCard);
        if (!cardContext) {
            casproxy::SCardBeginTransactionResponse res;
            res.packetId = req.packetId;
            res.apiReturn = SCARD_E_INVALID_HANDLE;
            sendResponse(hdl, res);
            return;
        }

        cardContext->addTask(msg);
    }

    void handleSCardEndTransaction(websocketpp::connection_hdl hdl, const std::string& msg) {
        rapidjson::Document doc;
        doc.Parse(msg.c_str());

        casproxy::SCardEndTransactionRequest req;
        if (!req.fromJson(doc)) {
            return;
        }

        auto session = getSession(hdl);
        std::lock_guard<std::mutex> lock(session->mutex);

        const auto cardContext = session->findCardContext(req.hCard);
        if (!cardContext) {
            casproxy::SCardEndTransactionResponse res;
            res.packetId = req.packetId;
            res.apiReturn = SCARD_E_INVALID_HANDLE;
            sendResponse(hdl, res);
            return;
        }

        cardContext->addTask(msg);
    }

    void handleSCardTransmit(websocketpp::connection_hdl hdl, const std::string& msg) {
        rapidjson::Document doc;
        doc.Parse(msg.c_str());

        casproxy::SCardTransmitRequest req;
        if (!req.fromJson(doc)) {
            return;
        }

        auto session = getSession(hdl);
        std::lock_guard<std::mutex> lock(session->mutex);

        const auto cardContext = session->findCardContext(req.hCard);
        if (!cardContext) {
            casproxy::SCardTransmitResponse res;
            res.packetId = req.packetId;
            res.apiReturn = SCARD_E_INVALID_HANDLE;
            sendResponse(hdl, res);
            return;
        }

        cardContext->addTask(msg);
    }

    void handleSCardGetAttrib(websocketpp::connection_hdl hdl, const std::string& msg) {
        rapidjson::Document doc;
        doc.Parse(msg.c_str());

        casproxy::SCardGetAttribRequest req;
        if (!req.fromJson(doc)) {
            return;
        }

        auto session = getSession(hdl);
        std::lock_guard<std::mutex> lock(session->mutex);

        const auto cardContext = session->findCardContext(req.hCard);
        if (!cardContext) {
            casproxy::SCardTransmitResponse res;
            res.packetId = req.packetId;
            res.apiReturn = SCARD_E_INVALID_HANDLE;
            sendResponse(hdl, res);
            return;
        }

        cardContext->addTask(msg);
    }

    void sendError(websocketpp::connection_hdl hdl, uint32_t packetId, const std::string& error) {
        casproxy::ErrorResponse res;
        res.packetId = packetId;
        res.resultCode = 100;
        res.errorMessage = error;
        sendResponse(hdl, res);
    }

    void sendResponse(websocketpp::connection_hdl hdl, const casproxy::Response& res) {
        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        res.toJson().Accept(writer);

        server.send(hdl, buffer.GetString(), websocketpp::frame::opcode::text);
    }

    std::shared_ptr<Session> getSession(websocketpp::connection_hdl hdl) {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = mapSession.find(hdl);
        if (it != mapSession.end()) {
            return it->second;
        }
        return nullptr;
    }

    websocketpp::server<websocketpp::config::asio> server;
    Config config;
    std::map<websocketpp::connection_hdl, std::shared_ptr<Session>, std::owner_less<websocketpp::connection_hdl>> mapSession;
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