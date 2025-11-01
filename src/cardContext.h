#pragma once
#include "casProxy.h"
#include <mutex>
#include <memory>
#include <queue>
#include <thread>
#include <condition_variable>
#include <atomic>

class Session;

class CardContext {
public:
    CardContext(std::shared_ptr<Session> session, uint64_t virtualCardHandle);
    void addTask(std::shared_ptr<casproxy::RequestBase> req);
    void stop();
    void run();
    void handleSCardConnect(std::shared_ptr<casproxy::SCardConnectRequest> req);
    void handleSCardDisconnect(std::shared_ptr<casproxy::SCardDisconnectRequest> req);
    void handleSCardBeginTransaction(std::shared_ptr<casproxy::SCardBeginTransactionRequest> req);
    void handleSCardEndTransaction(std::shared_ptr<casproxy::SCardEndTransactionRequest> req);
    void handleSCardTransmit(std::shared_ptr<casproxy::SCardTransmitRequest> req);
    void handleSCardGetAttrib(std::shared_ptr<casproxy::SCardGetAttribRequest> req);
    bool isRunning() const { return running; }
    SCARDHANDLE hCard;
    std::thread workerThread;

private:
    std::weak_ptr<Session> session;
    uint64_t virtualCardHandle;
    std::mutex queueMutex;
    std::queue<std::shared_ptr<casproxy::RequestBase>> tasks;
    std::condition_variable cv;
    std::atomic<bool> running{true};

};