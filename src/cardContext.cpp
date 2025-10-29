#include "cardContext.h"
#include "session.h"

CardContext::CardContext(std::shared_ptr<Session> session, uint64_t virtualCardHandle) :
    session(session), virtualCardHandle(virtualCardHandle) {
}

void CardContext::addTask(const std::string& req) {
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        tasks.push(req);
    }
    cv.notify_one();
}

void CardContext::run() {
    for (;;) {
        std::string req;
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            cv.wait(lock, [this] { return !running || !tasks.empty(); });
            if (!running && tasks.empty()) {
                return;
            }
            req = std::move(tasks.front());
            tasks.pop();
        }

        rapidjson::Document doc;
        doc.Parse(req.c_str());

        casproxy::BaseRequest baseReq;
        if (!baseReq.fromJson(doc)) {
            return;
        }
        if (baseReq.command == casproxy::kSCardConnect) {
            handleSCardConnect(doc);
        }
        if (baseReq.command == casproxy::kSCardDisconnect) {
            handleSCardDisconnect(doc);
        }
        else if (baseReq.command == casproxy::kSCardBeginTransaction) {
            handleSCardBeginTransaction(doc);
        }
        else if (baseReq.command == casproxy::kSCardEndTransaction) {
            handleSCardEndTransaction(doc);
        }
        else if (baseReq.command == casproxy::kSCardTransmit) {
            handleSCardTransmit(doc);
        }
        else if (baseReq.command == casproxy::kSCardGetAttrib) {
            handleSCardGetAttrib(doc);
        }
    }
}

void CardContext::stop() {
    running = false;
    cv.notify_all();
}

void CardContext::handleSCardConnect(rapidjson::Document& doc) {
    casproxy::SCardConnectRequest req;
    if (!req.fromJson(doc)) {
        return;
    }

    const auto hNativeContext = session.lock()->findContext(req.hContext);
    if (!hNativeContext) {
        casproxy::SCardEstablishContextResponse res;
        res.packetId = req.packetId;
        res.apiReturn = SCARD_E_INVALID_HANDLE;
        session.lock()->sendResponse(res);
        return;
    }

    DWORD dwActiveProtocol = 0;
    LONG returnValue = SCardConnect(*hNativeContext, req.szReader.c_str(), req.dwShareMode, req.dwPreferredProtocols, &hCard, &dwActiveProtocol);
    if (returnValue != SCARD_S_SUCCESS) {
        session.lock()->removeCardHandle(virtualCardHandle);
    }

    casproxy::SCardConnectResponse res;
    res.packetId = req.packetId;
    res.apiReturn = returnValue;
    res.hCard = virtualCardHandle;
    res.dwActiveProtocol = dwActiveProtocol;
    session.lock()->sendResponse(res);
}


void CardContext::handleSCardDisconnect(rapidjson::Document& doc) {
    casproxy::SCardDisconnectRequest req;
    req.fromJson(doc);

    LONG returnValue = SCardDisconnect(hCard, req.dwDisposition);
    if (returnValue == SCARD_S_SUCCESS) {
        session.lock()->removeCardHandle(req.hCard);
    }

    casproxy::SCardDisconnectResponse res;
    res.packetId = req.packetId;
    res.apiReturn = returnValue;
    session.lock()->sendResponse(res);
}

void CardContext::handleSCardBeginTransaction(rapidjson::Document& doc) {
    casproxy::SCardBeginTransactionRequest req;
    req.fromJson(doc);

    LONG returnValue = SCardBeginTransaction(hCard);

    casproxy::SCardBeginTransactionResponse res;
    res.packetId = req.packetId;
    res.apiReturn = returnValue;
    session.lock()->sendResponse(res);
}

void CardContext::handleSCardEndTransaction(rapidjson::Document& doc) {
    casproxy::SCardEndTransactionRequest req;
    req.fromJson(doc);

    LONG returnValue = SCardEndTransaction(hCard, req.dwDisposition);

    casproxy::SCardEndTransactionResponse res;
    res.packetId = req.packetId;
    res.apiReturn = returnValue;
    session.lock()->sendResponse(res);
}

void CardContext::handleSCardTransmit(rapidjson::Document& doc) {
    casproxy::SCardTransmitRequest req;
    req.fromJson(doc);

    std::vector<uint8_t> recvBuffer(req.recvLength);

    SCARD_IO_REQUEST* recvPci = nullptr;
    if (!req.isRecvPciNull) {
        SCARD_IO_REQUEST pci;
        pci.dwProtocol = req.recvPciProtocol;
        pci.cbPciLength = req.recvPciLength;
        recvPci = &pci;
    }

    DWORD recvLength = req.recvLength;
    LONG status = SCardTransmit(hCard, getPciByType(req.sendPci), (BYTE*)req.sendBuffer.data(), (DWORD)req.sendBuffer.size(), recvPci, recvBuffer.data(), &recvLength);
    recvBuffer.resize(recvLength);

    casproxy::SCardTransmitResponse res;
    res.packetId = req.packetId;
    res.apiReturn = status;
    res.recvBuffer = recvBuffer;
    if (!req.isRecvPciNull) {
        res.recvPciProtocol = recvPci->dwProtocol;
        res.recvPciLength = recvPci->cbPciLength;
    }
    res.isRecvPciNull = req.isRecvPciNull;
    res.recvLength = recvLength;
    session.lock()->sendResponse(res);
}

void CardContext::handleSCardGetAttrib(rapidjson::Document& doc) {
    casproxy::SCardGetAttribRequest req;
    req.fromJson(doc);

    std::vector<uint8_t> recvBuffer(req.attrLength);
    DWORD recvLength = req.attrLength;
    LONG status = SCardGetAttrib(hCard, req.dwAttrId, recvBuffer.data(), &recvLength);
    recvBuffer.resize(recvLength);

    casproxy::SCardGetAttribResponse res;
    res.packetId = req.packetId;
    res.apiReturn = status;
    res.attrBuffer = recvBuffer;
    res.attrLength = recvLength;
    session.lock()->sendResponse(res);
}