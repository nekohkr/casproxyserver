#pragma once
#include <cstdint>
#include <memory>
#include <map>
#include <vector>
#include <optional>
#include <functional>
#include <asio.hpp>
#include <winscard.h>
#include "cardContext.h"

class Session : public std::enable_shared_from_this<Session> {
public:
    using CloseHandler = std::function<void(std::shared_ptr<Session>)>;
    Session(asio::ip::tcp::socket socket, CloseHandler onClose);
    void clear();
    void doRead();
    void readPacketData();
    void handlePacket();
    void handleSCardEstablishContext(const casproxy::SCardEstablishContextRequest& req);
    void handleSCardReleaseContext(const casproxy::SCardReleaseContextRequest& req);
    void handleSCardListReaders(const casproxy::SCardListReadersRequest& req);
    void handleSCardConnect(const casproxy::SCardConnectRequest& req);
    void handleSCardDisconnect(const casproxy::SCardDisconnectRequest& req);
    void handleSCardBeginTransaction(const casproxy::SCardBeginTransactionRequest& req);
    void handleSCardEndTransaction(const casproxy::SCardEndTransactionRequest& req);
    void handleSCardTransmit(const casproxy::SCardTransmitRequest& req);
    void handleSCardGetAttrib(const casproxy::SCardGetAttribRequest& req);
    uint64_t addContext(SCARDCONTEXT hContext);
    std::shared_ptr<CardContext> addCardContext();
    std::optional<SCARDCONTEXT> findContext(uint64_t virtualContext);
    std::shared_ptr<CardContext> findCardContext(uint64_t virtualCardHandle);
    void removeCardContext(uint64_t virtualContext);
    void sendResponse(const casproxy::ResponseBase& res);
    void doWrite();
    void close();

    std::string ip;
    asio::ip::tcp::socket socket;

private:
    uint32_t packetLength;
    std::vector<uint8_t> packetData;
    std::map<uint64_t, SCARDCONTEXT> mapContext;
    std::map<uint64_t, std::shared_ptr<CardContext>> mapCardContext;
    uint64_t nextContext{ 1 };
    uint64_t nextCardHandle{ 1 };
    CloseHandler onClose;
    std::deque<std::vector<uint8_t>> sendQueue;
    std::mutex sendMutex;

};