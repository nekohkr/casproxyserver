#include "session.h"

Session::Session(asio::ip::tcp::socket socket, CloseHandler onClose)
    : socket(std::move(socket)), onClose(std::move(onClose))
{
}

void Session::clear() {
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

void Session::doRead() {
    auto self = shared_from_this();
    asio::async_read(socket, asio::buffer(&packetLength, 4),
        [this, self](std::error_code ec, std::size_t) {
            if (!ec) {
                packetLength = casproxy::swapEndian32(packetLength);
                readPacketData();
                doRead();
            }
            else {
                close();
            }
        }
    );
}

void Session::readPacketData() {
    if (packetLength > 1024 * 100) {
        return;
    }

    packetData.resize(packetLength);

    auto self = shared_from_this();
    asio::async_read(socket, asio::buffer(packetData.data(), packetLength),
        [this, self](std::error_code ec, std::size_t) {
            if (!ec) {
                handlePacket();
            }
            else {
                close();
            }
        }
    );
}

void Session::handlePacket() {
    casproxy::StreamReader reader(packetData);

    uint32_t packetId, opcodeValue;
    if (!reader.readBe(packetId) || !reader.readBe(opcodeValue)) {
        close();
        return;
    }
    ;
    casproxy::Opcode opcode = static_cast<casproxy::Opcode>(opcodeValue);

    switch (opcode) {
    case casproxy::Opcode::SCardEstablishContextReq: {
        casproxy::SCardEstablishContextRequest req;
        if (!req.unpack(packetId, reader)) {
            close();
            return;
        }

        handleSCardEstablishContext(req);
        break;
    }
    case casproxy::Opcode::SCardReleaseContextReq: {
        casproxy::SCardReleaseContextRequest req;
        if (!req.unpack(packetId, reader)) {
            close();
            return;
        }

        handleSCardReleaseContext(req);
        break;
    }
    case casproxy::Opcode::SCardListReadersReq: {
        casproxy::SCardListReadersRequest req;
        if (!req.unpack(packetId, reader)) {
            close();
            return;
        }

        handleSCardListReaders(req);
        break;
    }
    case casproxy::Opcode::SCardConnectReq: {
        casproxy::SCardConnectRequest req;
        if (!req.unpack(packetId, reader)) {
            close();
            return;
        }

        handleSCardConnect(req);
        break;
    }
    case casproxy::Opcode::SCardDisconnectReq: {
        casproxy::SCardDisconnectRequest req;
        if (!req.unpack(packetId, reader)) {
            close();
            return;
        }

        handleSCardDisconnect(req);
        break;
    }
    case casproxy::Opcode::SCardBeginTransactionReq: {
        casproxy::SCardBeginTransactionRequest req;
        if (!req.unpack(packetId, reader)) {
            close();
            return;
        }

        handleSCardBeginTransaction(req);
        break;
    }
    case casproxy::Opcode::SCardEndTransactionReq: {
        casproxy::SCardEndTransactionRequest req;
        if (!req.unpack(packetId, reader)) {
            close();
            return;
        }

        handleSCardEndTransaction(req);
        break;
    }
    case casproxy::Opcode::SCardTransmitReq: {
        casproxy::SCardTransmitRequest req;
        if (!req.unpack(packetId, reader)) {
            close();
            return;
        }

        handleSCardTransmit(req);
        break;
    }
    case casproxy::Opcode::SCardGetAttribReq: {
        casproxy::SCardGetAttribRequest req;
        if (!req.unpack(packetId, reader)) {
            close();
            return;
        }

        handleSCardGetAttrib(req);
        break;
    }
    default: {
        close();
    }
    }

}

void Session::handleSCardEstablishContext(const casproxy::SCardEstablishContextRequest& req) {
    SCARDCONTEXT hContext = 0;
    LONG returnValue = SCardEstablishContext(req.dwScope, nullptr, nullptr, &hContext);

    uint64_t virtualContext = 0;
    if (hContext != 0) {
        virtualContext = addContext(hContext);
    }

    casproxy::SCardEstablishContextResponse res;
    res.packetId = req.packetId;
    res.apiReturn = returnValue;
    res.hContext = virtualContext;
    sendResponse(res);
}

void Session::handleSCardReleaseContext(const casproxy::SCardReleaseContextRequest& req) {
    const auto hNativeContext = findContext(req.hContext);
    if (!hNativeContext) {
        casproxy::SCardReleaseContextResponse res;
        res.packetId = req.packetId;
        res.apiReturn = SCARD_E_INVALID_HANDLE;
        sendResponse(res);
        return;
    }

    LONG returnValue = SCardReleaseContext(*hNativeContext);
    if (returnValue == SCARD_S_SUCCESS) {
        removeCardContext(req.hContext);
    }

    casproxy::SCardReleaseContextResponse res;
    res.packetId = req.packetId;
    res.apiReturn = returnValue;
    sendResponse(res);
}

void Session::handleSCardListReaders(const casproxy::SCardListReadersRequest& req) {
    const auto hNativeContext = findContext(req.hContext);
    if (!hNativeContext) {
        casproxy::SCardListReadersResponse res;
        res.packetId = req.packetId;
        res.apiReturn = SCARD_E_INVALID_HANDLE;
        sendResponse(res);
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
    sendResponse(res);
}

void Session::handleSCardConnect(const casproxy::SCardConnectRequest& req) {
    const auto hNativeContext = findContext(req.hContext);
    if (!hNativeContext) {
        casproxy::SCardConnectResponse res;
        res.packetId = req.packetId;
        res.apiReturn = SCARD_E_INVALID_HANDLE;
        sendResponse(res);
        return;
    }

    auto cardContext = addCardContext();
    cardContext->addTask(std::make_shared<casproxy::SCardConnectRequest>(req));
    cardContext->workerThread = std::thread([cardContext]() {
        cardContext->run();
        });
    cardContext->workerThread.detach();
}

void Session::handleSCardDisconnect(const casproxy::SCardDisconnectRequest& req) {
    const auto cardContext = findCardContext(req.hCard);
    if (!cardContext || !cardContext->isRunning()) {
        casproxy::SCardDisconnectResponse res;
        res.packetId = req.packetId;
        res.apiReturn = SCARD_E_INVALID_HANDLE;
        sendResponse(res);
        return;
    }

    cardContext->addTask(std::make_shared<casproxy::SCardDisconnectRequest>(req));
}

void Session::handleSCardBeginTransaction(const casproxy::SCardBeginTransactionRequest& req) {
    const auto cardContext = findCardContext(req.hCard);
    if (!cardContext || !cardContext->isRunning()) {
        casproxy::SCardBeginTransactionResponse res;
        res.packetId = req.packetId;
        res.apiReturn = SCARD_E_INVALID_HANDLE;
        sendResponse(res);
        return;
    }

    cardContext->addTask(std::make_shared<casproxy::SCardBeginTransactionRequest>(req));
}

void Session::handleSCardEndTransaction(const casproxy::SCardEndTransactionRequest& req) {
    const auto cardContext = findCardContext(req.hCard);
    if (!cardContext || !cardContext->isRunning()) {
        casproxy::SCardEndTransactionResponse res;
        res.packetId = req.packetId;
        res.apiReturn = SCARD_E_INVALID_HANDLE;
        sendResponse(res);
        return;
    }

    cardContext->addTask(std::make_shared<casproxy::SCardEndTransactionRequest>(req));
}

void Session::handleSCardTransmit(const casproxy::SCardTransmitRequest& req) {
    const auto cardContext = findCardContext(req.hCard);
    if (!cardContext || !cardContext->isRunning()) {
        casproxy::SCardTransmitResponse res;
        res.packetId = req.packetId;
        res.apiReturn = SCARD_E_INVALID_HANDLE;
        sendResponse(res);
        return;
    }

    cardContext->addTask(std::make_shared<casproxy::SCardTransmitRequest>(req));
}

void Session::handleSCardGetAttrib(const casproxy::SCardGetAttribRequest& req) {
    const auto cardContext = findCardContext(req.hCard);
    if (!cardContext || !cardContext->isRunning()) {
        casproxy::SCardTransmitResponse res;
        res.packetId = req.packetId;
        res.apiReturn = SCARD_E_INVALID_HANDLE;
        sendResponse(res);
        return;
    }

    cardContext->addTask(std::make_shared<casproxy::SCardGetAttribRequest>(req));
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

void Session::sendResponse(const casproxy::ResponseBase& res) {
    casproxy::StreamWriter writer;
    res.pack(writer);

    uint32_t packetLength = static_cast<uint32_t>(writer.buffer.size());
    std::shared_ptr<std::vector<uint8_t>> packet = std::make_shared<std::vector<uint8_t>>(packetLength + 4);
    packetLength = casproxy::swapEndian32(packetLength);
    memcpy(packet->data(), &packetLength, 4);
    memcpy(packet->data() + 4, writer.buffer.data(), writer.buffer.size());

    std::lock_guard<std::mutex> lock(sendMutex);
    auto self = shared_from_this();
    asio::async_write(socket, asio::buffer(*packet),
        [this, self, packet](std::error_code ec, std::size_t) {
            if (ec) {
                close();
            }
        }
    );
}

void Session::close() {
    std::error_code ignored;
    socket.close(ignored);
    if (onClose) {
        onClose(shared_from_this());
    }
}
