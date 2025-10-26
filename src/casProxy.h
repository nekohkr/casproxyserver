#pragma once
#include <initializer_list>
#include <vector>
#include <string>
#include <optional>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
#include <winscard.h>

inline std::optional<std::vector<uint8_t>> hexToBytes(const std::string& hex) {
    if (hex.size() % 2 != 0)
        return std::nullopt;

    auto isHexChar = [](char c) {
        return std::isdigit(static_cast<unsigned char>(c)) ||
            (c >= 'a' && c <= 'f') ||
            (c >= 'A' && c <= 'F');
        };

    std::vector<uint8_t> bytes;
    bytes.reserve(hex.size() / 2);

    for (size_t i = 0; i < hex.size(); i += 2) {
        char high = hex[i];
        char low = hex[i + 1];

        if (!isHexChar(high) || !isHexChar(low))
            return std::nullopt;

        auto hexToNibble = [](char c) -> uint8_t {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            return c - 'A' + 10;
            };

        bytes.push_back((hexToNibble(high) << 4) | hexToNibble(low));
    }

    return bytes;
}

template <typename T>
inline std::string bytesToHex(const std::vector<T>& bytes) {
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (auto byte : bytes) {
        ss << std::setw(2) << (static_cast<int>(byte) & 0xFF);
    }
    return ss.str();
}

inline const SCARD_IO_REQUEST* getPciByType(int32_t type) {
    switch (type) {
    case 0: return SCARD_PCI_T0;
    case 1: return SCARD_PCI_T1;
    case 2: return SCARD_PCI_RAW;
    default: return nullptr;
    }
}

namespace casproxy {

inline constexpr char kVersion[] = "version";
inline constexpr char kSCardEstablishContext[] = "scardEstablishContext";
inline constexpr char kSCardReleaseContext[] = "scardReleaseContext";
inline constexpr char kSCardListReaders[] = "scardListReaders";
inline constexpr char kSCardConnect[] = "scardConnect";
inline constexpr char kSCardDisconnect[] = "scardDisconnect";
inline constexpr char kSCardBeginTransaction[] = "scardBeginTransaction";
inline constexpr char kSCardEndTransaction[] = "scardEndTransaction";
inline constexpr char kSCardTransmit[] = "scardTransmit";
inline constexpr char kSCardGetAttrib[] = "scardGetAttrib";

class BaseRequest {
public:
    virtual bool fromJson(const rapidjson::Document& doc) {
        if (!doc.IsArray() || doc.Size() < 2) {
            return false;
        }

        if (!doc[0].IsUint() || !doc[1].IsString()) {
            return false;
        }

        packetId = doc[0].GetUint();
        command = doc[1].GetString();
        return true;
    }

    virtual rapidjson::Document toJson() const {
        rapidjson::Document doc;
        doc.SetArray();
        doc.PushBack(rapidjson::Value().SetUint(packetId), doc.GetAllocator());
        doc.PushBack(rapidjson::Value(command.c_str(), doc.GetAllocator()), doc.GetAllocator());
        return doc;
    }

    uint32_t packetId;
    std::string command;

};

template<const char* CommandTag>
class Request : public BaseRequest {
public:
    Request() {
        command = CommandTag;
    }

    bool fromJson(const rapidjson::Document& doc) override {
        if (!BaseRequest::fromJson(doc)) {
            return false;
        }

        if (command != CommandTag) {
            return false;
        }

        return fromJsonPayload(doc);
    }

    rapidjson::Document toJson() const override {
        rapidjson::Document doc = BaseRequest::toJson();
        toJsonPayload(doc);
        return doc;
    }

protected:
    virtual bool fromJsonPayload(const rapidjson::Document& doc) = 0;
    virtual void toJsonPayload(rapidjson::Document& doc) const = 0;

};

class SCardEstablishContextRequest : public Request<kSCardEstablishContext> {
public:
    uint32_t dwScope;

protected:
    virtual bool fromJsonPayload(const rapidjson::Document& doc) {
        if (doc.Size() != 3) {
            return false;
        }
        if (!doc[2].IsUint()) {
            return false;
        }

        dwScope = doc[2].GetUint();
        return true;
    }

    virtual void toJsonPayload(rapidjson::Document& doc) const {
        doc.PushBack(rapidjson::Value().SetUint(dwScope), doc.GetAllocator());
    }

};

class SCardReleaseContextRequest : public Request<kSCardReleaseContext> {
public:
    uint64_t hContext;

protected:
    virtual bool fromJsonPayload(const rapidjson::Document& doc) {
        if (doc.Size() != 3) {
            return false;
        }
        if (!doc[2].IsUint64()) {
            return false;
        }

        hContext = doc[2].GetUint64();
        return true;
    }

    virtual void toJsonPayload(rapidjson::Document& doc) const {
        doc.PushBack(rapidjson::Value().SetUint64(hContext), doc.GetAllocator());
    }

};

class SCardListReadersRequest : public Request<kSCardListReaders> {
public:
    uint64_t hContext;
    bool isGroupsNull;
    std::string groups;
    uint32_t readersLength;

protected:
    virtual bool fromJsonPayload(const rapidjson::Document& doc) {
        if (doc.Size() != 5) {
            return false;
        }

        if (!doc[2].IsUint64() || (!doc[3].IsString() && !doc[3].IsNull()) || !doc[4].IsUint()) {
            return false;
        }

        hContext = doc[2].GetUint64();
        isGroupsNull = doc[3].IsNull();

        if (!isGroupsNull) {
            this->groups = doc[3].GetString();
        }

        readersLength = doc[4].GetUint();
        return true;
    }

    virtual void toJsonPayload(rapidjson::Document& doc) const {
        doc.PushBack(rapidjson::Value().SetUint64(hContext), doc.GetAllocator());
        if (isGroupsNull) {
            doc.PushBack(rapidjson::Value().SetNull(), doc.GetAllocator());
        }
        else {
            doc.PushBack(rapidjson::Value(groups.c_str(), doc.GetAllocator()), doc.GetAllocator());
        }
        doc.PushBack(rapidjson::Value().SetUint(readersLength), doc.GetAllocator());
    }

};

class SCardConnectRequest : public Request<kSCardConnect> {
public:
    uint64_t hContext;
    std::string szReader;
    uint32_t dwShareMode;
    uint32_t dwPreferredProtocols;

protected:
    virtual bool fromJsonPayload(const rapidjson::Document& doc) {
        if (doc.Size() != 6) {
            return false;
        }

        if (!doc[2].IsUint64() || !doc[3].IsString() || !doc[4].IsUint() || !doc[5].IsUint()) {
            return false;
        }

        hContext = doc[2].GetUint64();
        szReader = doc[3].GetString();
        dwShareMode = doc[4].GetUint();
        dwPreferredProtocols = doc[5].GetUint();
        return true;
    }

    virtual void toJsonPayload(rapidjson::Document& doc) const {
        doc.PushBack(rapidjson::Value().SetUint64(hContext), doc.GetAllocator());
        doc.PushBack(rapidjson::Value(szReader.c_str(), doc.GetAllocator()), doc.GetAllocator());
        doc.PushBack(rapidjson::Value().SetUint(dwShareMode), doc.GetAllocator());
        doc.PushBack(rapidjson::Value().SetUint(dwPreferredProtocols), doc.GetAllocator());
    }

};

class SCardDisconnectRequest : public Request<kSCardDisconnect> {
public:
    uint64_t hCard;
    uint32_t dwDisposition;

protected:
    virtual bool fromJsonPayload(const rapidjson::Document& doc) {
        if (doc.Size() != 4) {
            return false;
        }

        if (!doc[2].IsUint64() || !doc[3].IsUint()) {
            return false;
        }

        hCard = doc[2].GetUint64();
        dwDisposition = doc[3].GetUint();
        return true;
    }

    virtual void toJsonPayload(rapidjson::Document& doc) const {
        doc.PushBack(rapidjson::Value().SetUint64(hCard), doc.GetAllocator());
        doc.PushBack(rapidjson::Value().SetUint(dwDisposition), doc.GetAllocator());
    }

};

class SCardBeginTransactionRequest : public Request<kSCardBeginTransaction> {
public:
    uint64_t hCard;

protected:
    virtual bool fromJsonPayload(const rapidjson::Document& doc) {
        if (doc.Size() != 3) {
            return false;
        }

        if (!doc[2].IsUint64()) {
            return false;
        }

        hCard = doc[2].GetUint64();
        return true;
    }

    virtual void toJsonPayload(rapidjson::Document& doc) const {
        doc.PushBack(rapidjson::Value().SetUint64(hCard), doc.GetAllocator());
    }

};

class SCardEndTransactionRequest : public Request<kSCardEndTransaction> {
public:
    uint64_t hCard;
    uint32_t dwDisposition;

protected:
    virtual bool fromJsonPayload(const rapidjson::Document& doc) {
        if (doc.Size() != 4) {
            return false;
        }

        if (!doc[2].IsUint64() || !doc[3].IsUint()) {
            return false;
        }

        hCard = doc[2].GetUint64();
        dwDisposition = doc[3].GetUint();
        return true;
    }

    virtual void toJsonPayload(rapidjson::Document& doc) const {
        doc.PushBack(rapidjson::Value().SetUint64(hCard), doc.GetAllocator());
        doc.PushBack(rapidjson::Value().SetUint(dwDisposition), doc.GetAllocator());
    }

};

class SCardTransmitRequest : public Request<kSCardTransmit> {
public:
    uint64_t hCard;
    uint32_t sendPci;
    std::vector<uint8_t> sendBuffer;
    bool isRecvPciNull;
    uint32_t recvPciProtocol;
    uint32_t recvPciLength;
    uint32_t recvLength;

protected:
    virtual bool fromJsonPayload(const rapidjson::Document& doc) {
        if (doc.Size() != 8) {
            return false;
        }

        if (!doc[2].IsUint64() || !doc[3].IsUint() || !doc[4].IsString() ||
           (!doc[5].IsUint() && !doc[5].IsNull()) ||
            (!doc[6].IsUint() && !doc[6].IsNull()) || !doc[7].IsUint64()) {
            return false;
        }

        hCard = doc[2].GetUint64();
        sendPci = doc[3].GetUint();

        auto decodedSendBuffer = hexToBytes(doc[4].GetString());
        if (!decodedSendBuffer) {
            return false;
        }

        sendBuffer = *decodedSendBuffer;

        isRecvPciNull = doc[5].IsNull() || doc[6].IsNull();
        if (isRecvPciNull) {
            recvPciProtocol = 0;
            recvPciLength = 0;
        }
        else {
            recvPciProtocol = doc[5].GetUint();
            recvPciLength = doc[6].GetUint();
        }
        recvLength = doc[7].GetUint();

        return true;
    }

    virtual void toJsonPayload(rapidjson::Document& doc) const {
        doc.PushBack(rapidjson::Value().SetUint64(hCard), doc.GetAllocator());
        doc.PushBack(rapidjson::Value().SetUint(sendPci), doc.GetAllocator());
        doc.PushBack(rapidjson::Value(bytesToHex(sendBuffer).c_str(), doc.GetAllocator()), doc.GetAllocator());

        if (isRecvPciNull) {
            doc.PushBack(rapidjson::Value().SetNull(), doc.GetAllocator());
            doc.PushBack(rapidjson::Value().SetNull(), doc.GetAllocator());
        }
        else {
            doc.PushBack(rapidjson::Value().SetUint(recvPciProtocol), doc.GetAllocator());
            doc.PushBack(rapidjson::Value().SetUint(recvPciLength), doc.GetAllocator());
        }

        doc.PushBack(rapidjson::Value().SetUint(recvLength), doc.GetAllocator());
    }

};

class SCardGetAttribRequest : public Request<kSCardGetAttrib> {
public:
    uint64_t hCard;
    uint32_t dwAttrId;
    uint32_t attrLength;

protected:
    virtual bool fromJsonPayload(const rapidjson::Document& doc) {
        if (doc.Size() != 5) {
            return false;
        }

        if (!doc[2].IsUint64() || !doc[3].IsUint() || !doc[4].IsUint()) {
            return false;
        }

        hCard = doc[2].GetUint64();
        dwAttrId = doc[3].GetUint();
        attrLength = doc[4].GetUint();

        return true;
    }

    virtual void toJsonPayload(rapidjson::Document& doc) const {
        doc.PushBack(rapidjson::Value().SetUint64(hCard), doc.GetAllocator());
        doc.PushBack(rapidjson::Value().SetUint(dwAttrId), doc.GetAllocator());
        doc.PushBack(rapidjson::Value().SetUint(attrLength), doc.GetAllocator());
    }

};

class Response {
public:
    virtual ~Response() = default;

    bool fromJson(const rapidjson::Document& doc) {
        if (!doc.IsArray() || doc.Size() < 2 || !doc[0].IsUint()) {
            return false;
        }

        packetId = doc[0].GetUint();
        resultCode = doc[1].GetUint();
        if (resultCode != 0) {
            if (doc.Size() != 3 || !doc[2].IsString()) {
                return false;
            }

            errorMessage = doc[2].GetString();
            return true;
        }
        else {
            return fromJsonPayload(doc);
        }
    }

    rapidjson::Document toJson() const {
        rapidjson::Document doc;
        doc.SetArray();
        doc.PushBack(rapidjson::Value().SetUint(packetId), doc.GetAllocator());
        doc.PushBack(rapidjson::Value().SetUint(resultCode), doc.GetAllocator());
        toJsonPayload(doc);
        return doc;
    }

    uint32_t packetId{0};
    uint32_t resultCode{0};
    std::string errorMessage;

protected:
    virtual bool fromJsonPayload(const rapidjson::Document& doc) { return true; }
    virtual void toJsonPayload(rapidjson::Document& doc) const {}

};

class ErrorResponse : public Response {
public:
    std::string errorMessage;

protected:
    virtual bool fromJsonPayload(const rapidjson::Document& doc) {
        if (!doc.IsArray() || doc.Size() != 3) {
            return false;
        }

        if (!doc[2].IsString()) {
            return false;
        }

        errorMessage = doc[2].GetString();
        return true;
    }

    virtual void toJsonPayload(rapidjson::Document& doc) const {
        doc.PushBack(rapidjson::Value(errorMessage.c_str(), doc.GetAllocator()), doc.GetAllocator());
    }

};

class SCardEstablishContextResponse : public Response {
public:
    uint32_t apiReturn{0};
    uint64_t hContext{0};

protected:
    virtual bool fromJsonPayload(const rapidjson::Document& doc) {
        if (!doc.IsArray() || doc.Size() != 4) {
            return false;
        }

        if (!doc[2].IsUint() || !doc[3].IsUint64()) {
            return false;
        }

        apiReturn = doc[2].GetUint();
        hContext = doc[3].GetUint64();
        return true;
    }

    virtual void toJsonPayload(rapidjson::Document& doc) const {
        doc.PushBack(rapidjson::Value().SetUint(apiReturn), doc.GetAllocator());
        doc.PushBack(rapidjson::Value().SetUint64(hContext), doc.GetAllocator());
    }

};

class SCardReleaseContextResponse : public Response {
public:
    uint32_t apiReturn{ 0 };

protected:
    virtual bool fromJsonPayload(const rapidjson::Document& doc) {
        if (!doc.IsArray() || doc.Size() != 3) {
            return false;
        }

        if (!doc[2].IsUint()) {
            return false;
        }

        apiReturn = doc[2].GetUint();
        return true;
    }

    virtual void toJsonPayload(rapidjson::Document& doc) const {
        doc.PushBack(rapidjson::Value().SetUint(apiReturn), doc.GetAllocator());
    }

};

class SCardListReadersResponse : public Response {
public:
    uint32_t apiReturn{0};
    std::vector<uint8_t> readers;
    uint32_t readersLength{0};

protected:
    virtual bool fromJsonPayload(const rapidjson::Document& doc) {
        if (!doc.IsArray() || doc.Size() != 5) {
            return false;
        }

        if (!doc[2].IsUint() || !doc[3].IsString() || !doc[4].IsUint()) {
            return false;
        }

        apiReturn = doc[2].GetUint();
        auto decodedReaders = hexToBytes(doc[3].GetString());
        if (!decodedReaders) {
            return false;
        }
        readers = *decodedReaders;
        readersLength = doc[4].GetUint();
        return true;
    }

    virtual void toJsonPayload(rapidjson::Document& doc) const {
        doc.PushBack(rapidjson::Value().SetUint(apiReturn), doc.GetAllocator());
        doc.PushBack(rapidjson::Value(bytesToHex(readers).c_str(), doc.GetAllocator()), doc.GetAllocator());
        doc.PushBack(rapidjson::Value().SetUint(readersLength), doc.GetAllocator());
    }

};

class SCardConnectResponse : public Response {
public:
    uint32_t apiReturn{0};
    uint64_t hCard{0};
    uint32_t dwActiveProtocol{0};

protected:
    virtual bool fromJsonPayload(const rapidjson::Document& doc) {
        if (!doc.IsArray() || doc.Size() != 5) {
            return false;
        }

        if (!doc[2].IsUint() || !doc[3].IsUint() || !doc[4].IsUint()) {
            return false;
        }

        apiReturn = doc[2].GetUint();
        hCard = doc[3].GetUint64();
        dwActiveProtocol = doc[4].GetUint();
        return true;
    }

    virtual void toJsonPayload(rapidjson::Document& doc) const {
        doc.PushBack(rapidjson::Value().SetUint(apiReturn), doc.GetAllocator());
        doc.PushBack(rapidjson::Value().SetUint64(hCard), doc.GetAllocator());
        doc.PushBack(rapidjson::Value().SetUint(dwActiveProtocol), doc.GetAllocator());
    }

};

class SCardDisconnectResponse : public Response {
public:
    uint32_t apiReturn{0};

protected:
    virtual bool fromJsonPayload(const rapidjson::Document& doc) {
        if (!doc.IsArray() || doc.Size() != 3) {
            return false;
        }

        if (!doc[2].IsUint()) {
            return false;
        }

        apiReturn = doc[2].GetUint();
        return true;
    }

    virtual void toJsonPayload(rapidjson::Document& doc) const {
        doc.PushBack(rapidjson::Value().SetUint(apiReturn), doc.GetAllocator());
    }

};

class SCardBeginTransactionResponse : public Response {
public:
    uint32_t apiReturn{0};

protected:
    virtual bool fromJsonPayload(const rapidjson::Document& doc) {
        if (!doc.IsArray() || doc.Size() != 3) {
            return false;
        }

        if (!doc[2].IsUint()) {
            return false;
        }

        apiReturn = doc[2].GetUint();
        return true;
    }

    virtual void toJsonPayload(rapidjson::Document& doc) const {
        doc.PushBack(rapidjson::Value().SetUint(apiReturn), doc.GetAllocator());
    }

};

class SCardEndTransactionResponse : public Response {
public:
    uint32_t apiReturn{0};

protected:
    virtual bool fromJsonPayload(const rapidjson::Document& doc) {
        if (!doc.IsArray() || doc.Size() != 3) {
            return false;
        }

        if (!doc[2].IsUint()) {
            return false;
        }

        apiReturn = doc[2].GetUint();
        return true;
    }

    virtual void toJsonPayload(rapidjson::Document& doc) const {
        doc.PushBack(rapidjson::Value().SetUint(apiReturn), doc.GetAllocator());
    }

};

class SCardTransmitResponse : public Response {
public:
    uint32_t apiReturn{0};
    std::vector<uint8_t> recvBuffer;
    uint32_t recvLength{0};
    bool isRecvPciNull{0};
    uint32_t recvPciProtocol{0};
    uint32_t recvPciLength{0};

protected:
    virtual bool fromJsonPayload(const rapidjson::Document& doc) {
        if (!doc.IsArray() || doc.Size() != 7) {
            return false;
        }

        if (!doc[2].IsUint() || !doc[3].IsString() || !doc[4].IsUint() ||
            (!doc[5].IsUint() && !doc[5].IsNull()) || (!doc[6].IsUint() && !doc[6].IsNull())) {
            return false;
        }

        apiReturn = doc[2].GetUint();
        auto decodedRecvBuffer = hexToBytes(doc[3].GetString());
        if (!decodedRecvBuffer) {
            return false;
        }
        recvBuffer = *decodedRecvBuffer;

        recvLength = doc[4].GetUint();
        isRecvPciNull = doc[5].IsNull() || doc[6].IsNull();
        if (isRecvPciNull) {
            recvPciProtocol = 0;
            recvPciLength = 0;
        }
        else {
            recvPciProtocol = doc[5].GetUint();
            recvPciLength = doc[6].GetUint();
        }
        return true;
    }

    virtual void toJsonPayload(rapidjson::Document& doc) const {
        doc.PushBack(rapidjson::Value().SetUint(apiReturn), doc.GetAllocator());
        doc.PushBack(rapidjson::Value(bytesToHex(recvBuffer).c_str(), doc.GetAllocator()), doc.GetAllocator());
        doc.PushBack(rapidjson::Value().SetUint(recvLength), doc.GetAllocator());

        if (isRecvPciNull) {
            doc.PushBack(rapidjson::Value().SetNull(), doc.GetAllocator());
            doc.PushBack(rapidjson::Value().SetNull(), doc.GetAllocator());
        }
        else {
            doc.PushBack(rapidjson::Value().SetUint(recvPciProtocol), doc.GetAllocator());
            doc.PushBack(rapidjson::Value().SetUint(recvPciLength), doc.GetAllocator());
        }
    }

};

class SCardGetAttribResponse : public Response {
public:
    uint32_t apiReturn{0};
    std::vector<uint8_t> attrBuffer;
    uint32_t attrLength{0};

protected:
    virtual bool fromJsonPayload(const rapidjson::Document& doc) {
        if (!doc.IsArray() || doc.Size() != 5) {
            return false;
        }

        if (!doc[2].IsUint() || !doc[3].IsString() || !doc[4].IsUint()) {
            return false;
        }

        apiReturn = doc[2].GetUint();
        auto decodedAttrBuffer = hexToBytes(doc[3].GetString());
        if (!decodedAttrBuffer) {
            return false;
        }
        attrBuffer = *decodedAttrBuffer;

        attrLength = doc[4].GetUint();
        return true;
    }

    virtual void toJsonPayload(rapidjson::Document& doc) const {
        doc.PushBack(rapidjson::Value().SetUint(apiReturn), doc.GetAllocator());
        doc.PushBack(rapidjson::Value(bytesToHex(attrBuffer).c_str(), doc.GetAllocator()), doc.GetAllocator());
        doc.PushBack(rapidjson::Value().SetUint(attrLength), doc.GetAllocator());
    }

};

}