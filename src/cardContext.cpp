#include "cardContext.h"
#include "session.h"

CardContext::CardContext(std::shared_ptr<Session> session, uint64_t virtualCardHandle) :
    session(session), virtualCardHandle(virtualCardHandle) {
}

void CardContext::addTask(std::shared_ptr<casproxy::RequestBase> req) {
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        tasks.push(req);
    }
    cv.notify_one();
}

void CardContext::run() {
    for (;;) {
        std::shared_ptr<casproxy::RequestBase> req;
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            cv.wait(lock, [this] { return !running || !tasks.empty(); });
            if (!running && tasks.empty()) {
                return;
            }
            req = std::move(tasks.front());
            tasks.pop();
        }

        casproxy::Opcode opcode = static_cast<casproxy::Opcode>(req->opcode);
        if (opcode == casproxy::Opcode::SCardConnectReq) {
            handleSCardConnect(std::static_pointer_cast<casproxy::SCardConnectRequest>(req));
        }
        if (opcode == casproxy::Opcode::SCardDisconnectReq) {
            handleSCardDisconnect(std::static_pointer_cast<casproxy::SCardDisconnectRequest>(req));
        }
        else if (opcode == casproxy::Opcode::SCardBeginTransactionReq) {
            handleSCardBeginTransaction(std::static_pointer_cast<casproxy::SCardBeginTransactionRequest>(req));
        }
        else if (opcode == casproxy::Opcode::SCardEndTransactionReq) {
            handleSCardEndTransaction(std::static_pointer_cast<casproxy::SCardEndTransactionRequest>(req));
        }
        else if (opcode == casproxy::Opcode::SCardTransmitReq) {
            handleSCardTransmit(std::static_pointer_cast<casproxy::SCardTransmitRequest>(req));
        }
        else if (opcode == casproxy::Opcode::SCardGetAttribReq) {
            handleSCardGetAttrib(std::static_pointer_cast<casproxy::SCardGetAttribRequest>(req));
        }
    }
}

void CardContext::stop() {
    running = false;
    cv.notify_all();
}

void CardContext::handleSCardConnect(std::shared_ptr<casproxy::SCardConnectRequest> req) {
    auto s = session.lock();
    if (!s) {
        return;
    }

    const auto hNativeContext = s->findContext(req->hContext);
    if (!hNativeContext) {
        casproxy::SCardConnectResponse res;
        res.packetId = req->packetId;
        res.apiReturn = SCARD_E_INVALID_HANDLE;
        session.lock()->sendResponse(res);
        return;
    }

    DWORD dwActiveProtocol = 0;
    LONG returnValue = SCardConnect(*hNativeContext, req->szReader.c_str(), req->dwShareMode, req->dwPreferredProtocols, &hCard, &dwActiveProtocol);
    if (returnValue != SCARD_S_SUCCESS) {
        stop();
    }

    casproxy::SCardConnectResponse res;
    res.packetId = req->packetId;
    res.apiReturn = returnValue;
    res.hCard = virtualCardHandle;
    res.dwActiveProtocol = dwActiveProtocol;
    s->sendResponse(res);
}

void CardContext::handleSCardDisconnect(std::shared_ptr<casproxy::SCardDisconnectRequest> req) {
    auto s = session.lock();
    if (!s) {
        return;
    }

    LONG returnValue = SCardDisconnect(hCard, req->dwDisposition);
    if (returnValue == SCARD_S_SUCCESS) {
        stop();
    }

    casproxy::SCardDisconnectResponse res;
    res.packetId = req->packetId;
    res.apiReturn = returnValue;
    s->sendResponse(res);
}

void CardContext::handleSCardBeginTransaction(std::shared_ptr<casproxy::SCardBeginTransactionRequest> req) {
    auto s = session.lock();
    if (!s) {
        return;
    }

    LONG returnValue = SCardBeginTransaction(hCard);

    casproxy::SCardBeginTransactionResponse res;
    res.packetId = req->packetId;
    res.apiReturn = returnValue;
    s->sendResponse(res);
}

void CardContext::handleSCardEndTransaction(std::shared_ptr<casproxy::SCardEndTransactionRequest> req) {
    auto s = session.lock();
    if (!s) {
        return;
    }

    LONG returnValue = SCardEndTransaction(hCard, req->dwDisposition);

    casproxy::SCardEndTransactionResponse res;
    res.packetId = req->packetId;
    res.apiReturn = returnValue;
    s->sendResponse(res);
}

void CardContext::handleSCardTransmit(std::shared_ptr<casproxy::SCardTransmitRequest> req) {
    auto s = session.lock();
    if (!s) {
        return;
    }

    std::vector<uint8_t> recvBuffer(req->recvLength);

    SCARD_IO_REQUEST* recvPci = nullptr;
    if (!req->isRecvPciNull) {
        SCARD_IO_REQUEST pci;
        pci.dwProtocol = req->recvPciProtocol;
        pci.cbPciLength = req->recvPciLength;
        recvPci = &pci;
    }

    DWORD recvLength = req->recvLength;
    LONG status = SCardTransmit(hCard, casproxy::getPciByType(req->sendPci), (BYTE*)req->sendBuffer.data(), (DWORD)req->sendBuffer.size(), recvPci, recvBuffer.data(), &recvLength);
    recvBuffer.resize(recvLength);

    casproxy::SCardTransmitResponse res;
    res.packetId = req->packetId;
    res.apiReturn = status;
    res.recvBuffer = recvBuffer;
    if (!req->isRecvPciNull) {
        res.recvPciProtocol = recvPci->dwProtocol;
        res.recvPciLength = recvPci->cbPciLength;
    }
    res.isRecvPciNull = req->isRecvPciNull;
    res.recvLength = recvLength;
    s->sendResponse(res);
}

void CardContext::handleSCardGetAttrib(std::shared_ptr<casproxy::SCardGetAttribRequest> req) {
    auto s = session.lock();
    if (!s) {
        return;
    }

    std::vector<uint8_t> recvBuffer(req->attrLength);
    DWORD recvLength = req->attrLength;
    LONG status = SCardGetAttrib(hCard, req->dwAttrId, recvBuffer.data(), &recvLength);
    recvBuffer.resize(recvLength);

    casproxy::SCardGetAttribResponse res;
    res.packetId = req->packetId;
    res.apiReturn = status;
    res.attrBuffer = recvBuffer;
    res.attrLength = recvLength;
    s->sendResponse(res);
}