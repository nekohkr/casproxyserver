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
    uint16_t port = 24000;
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
                else {
                    throw std::runtime_error("Invalid CIDR '" + cidr + "'");
                }
            }
        }
    }

    bool isAllowedIp(const std::string& ip) const {
        auto ipNum = parseIpv4(ip);
        if (!ipNum) {
            return false;
        }

        for (auto& cidr : allowedIpv4Ranges) {
            if ((*ipNum & cidr.mask) == cidr.network) {
                return true;
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

};