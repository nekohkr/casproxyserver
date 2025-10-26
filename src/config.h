#pragma once
#include <yaml-cpp/yaml.h>
#include <iostream>
#include <charconv>

class Config {
public:
    struct Ipv4Cidr {
        uint32_t network;
        uint32_t mask;
    };

    struct Ipv6Cidr {
        std::array<uint8_t, 16> network;
        std::array<uint8_t, 16> mask;
    };

    std::string listenIp = "0.0.0.0";
    uint16_t port = 9002;
    std::vector<Ipv4Cidr> allowedIpv4Ranges;
    std::vector<Ipv6Cidr> allowedIpv6Ranges;

public:
    void loadConfig(const std::string& configFile) {
        std::ifstream fs(configFile);
        if (!fs.good()) {
            throw std::runtime_error("Config file '" + configFile + "' not found");
        }

        YAML::Node yaml = YAML::LoadFile(configFile);

        if (yaml["listenIp"]) {
            listenIp = yaml["listenIp"].as<std::string>();
        }
        if (yaml["port"]) {
            port = yaml["port"].as<uint16_t>();
        }
        if (yaml["allowedIps"]) {
            for (const auto& node : yaml["allowedIps"]) {
                std::string cidr = node.as<std::string>();
                if (cidr.find('.') != std::string::npos) {
                    auto v4 = parseIpv4Cidr(cidr);
                    if (!v4) {
                        throw std::runtime_error("Invalid CIDR '" + cidr + "'");
                    }

                    allowedIpv4Ranges.push_back(*v4);
                }
                else if (cidr.find(':') != std::string::npos) {
                    auto v6 = parseIpv6Cidr(cidr);
                    if (!v6) {
                        throw std::runtime_error("Invalid CIDR '" + cidr + "'");
                    }

                    allowedIpv6Ranges.push_back(*v6);
                }
                else {
                    throw std::runtime_error("Invalid CIDR '" + cidr + "'");
                }
            }
        }
    }

    bool isAllowedIp(const std::string& ip) const {
        if (ip.find(':') == std::string::npos) {
            auto ipNum = parseIpv4(ip);
            if (!ipNum) {
                return false;
            }

            for (auto& cidr : allowedIpv4Ranges) {
                if ((*ipNum & cidr.mask) == cidr.network) {
                    return true;
                }
            }
        }
        else {
            auto ipBytes = parseIpv6(ip);
            if (!ipBytes) {
                return false;
            }
            for (auto& cidr : allowedIpv6Ranges) {
                bool ok = true;
                for (int i = 0; i < 16; i++) {
                    if (((*ipBytes)[i] & cidr.mask[i]) != cidr.network[i]) {
                        ok = false;
                        break;
                    }
                }
                if (ok) {
                    return true;
                }
            }
        }
        return false;
    }

    static std::optional<Ipv4Cidr> parseIpv4Cidr(const std::string& cidrStr) {
        auto pos = cidrStr.find('/');
        std::string ip;
        int prefix;

        if (pos == std::string::npos) {
            ip = cidrStr;
            prefix = 32;
        }
        else {
            ip = cidrStr.substr(0, pos);
            prefix = std::stoi(cidrStr.substr(pos + 1));
            if (prefix < 0 || prefix > 32) {
                return std::nullopt;
            }
        }

        auto ipNum = parseIpv4(ip);
        if (!ipNum) {
            return std::nullopt;
        }

        uint32_t mask = (prefix == 0) ? 0 : 0xFFFFFFFF << (32 - prefix);
        Ipv4Cidr out;
        out.network = (*ipNum) & mask;
        out.mask = mask;
        return out;
    }

    static std::optional<Ipv6Cidr> parseIpv6Cidr(const std::string& cidrStr) {
        auto pos = cidrStr.find('/');
        std::string ip;
        int prefix;
        if (pos == std::string::npos) {
            ip = cidrStr;
            prefix = 128;
        }
        else {
            ip = cidrStr.substr(0, pos);
            prefix = std::stoi(cidrStr.substr(pos + 1));
            if (prefix < 0 || prefix>128) {
                return std::nullopt;
            }
        }

        auto ipBytes = parseIpv6(ip);
        if (!ipBytes) {
            return std::nullopt;
        }

        std::array<uint8_t, 16> mask{};
        for (int i = 0; i < 16; i++) {
            if (prefix >= 8) {
                mask[i] = 0xFF;
                prefix -= 8;
            }
            else if (prefix > 0) {
                mask[i] = 0xFF << (8 - prefix);
                prefix = 0;
            }
            else {
                mask[i] = 0;
            }
        }

        Ipv6Cidr out;
        for (int i = 0; i < 16; i++) {
            out.network[i] = (*ipBytes)[i] & mask[i];
            out.mask[i] = mask[i];
        }
        return out;
    }

    static std::optional<uint32_t> parseIpv4(const std::string& ip) {
        std::stringstream ss{ std::string(ip) };
        std::string token;
        uint32_t result = 0;

        for (int i = 0; i < 4; ++i) {
            if (!std::getline(ss, token, '.')) {
                return std::nullopt;
            }

            int octet{};
            auto [ptr, ec] = std::from_chars(token.data(), token.data() + token.size(), octet);
            if (ec != std::errc{} || octet < 0 || octet > 255) {
                return std::nullopt;
            }
            result = (result << 8) | static_cast<uint32_t>(octet);
        }
        return result;
    }

    static std::optional<std::array<uint8_t, 16>> parseIpv6(const std::string& ip) {
        std::array<uint8_t, 16> out{};
        std::vector<std::string> parts;

        std::stringstream ss{ std::string(ip) };
        std::string temp;
        while (std::getline(ss, temp, ':')) {
            parts.push_back(temp);
        }

        size_t left = 0;
        size_t right = 15;
        bool doubleColon = false;

        for (const auto& part : parts) {
            if (part.empty()) {
                if (doubleColon) {
                    return std::nullopt;
                }
                doubleColon = true;
                continue;
            }

            uint16_t val{};
            auto [ptr, ec] = std::from_chars(part.data(), part.data() + part.size(), val, 16);
            if (ec != std::errc{}) {
                return std::nullopt;
            }

            if (!doubleColon) {
                if (left >= 16) {
                    return std::nullopt;
                }
                out[left++] = static_cast<uint8_t>(val >> 8);
                out[left++] = static_cast<uint8_t>(val & 0xFF);
            }
            else {
                if (right < 1) {
                    return std::nullopt;
                }
                out[right--] = static_cast<uint8_t>(val & 0xFF);
                out[right--] = static_cast<uint8_t>(val >> 8);
            }
        }
        return out;
    }
};