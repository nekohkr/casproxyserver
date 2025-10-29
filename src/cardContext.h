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
    void addTask(const std::string& req);
    void stop();
    void run();

    SCARDHANDLE hCard;

private:
    void handleSCardConnect(rapidjson::Document& doc);
    void handleSCardDisconnect(rapidjson::Document& doc);
    void handleSCardBeginTransaction(rapidjson::Document& doc);
    void handleSCardEndTransaction(rapidjson::Document& doc);
    void handleSCardTransmit(rapidjson::Document& doc);
    void handleSCardGetAttrib(rapidjson::Document& doc);

    std::weak_ptr<Session> session;
    uint64_t virtualCardHandle;
    std::mutex queueMutex;
    std::queue<std::string> tasks;
    std::condition_variable cv;
    std::atomic<bool> running{true};
    std::thread workerThread;

};