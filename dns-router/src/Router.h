#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

struct RouteResult {
    uint16_t pop = 0;
    int scope = 0;
    bool found = false;
};

class Router {
public:
    void insert(const std::string & cidr, uint16_t pop);
    RouteResult route(const std::string & ecs_cidr) const;

private:
    struct PrefixKey {
        std::array<uint8_t, 16> bytes{};

        bool operator==(const PrefixKey & other) const noexcept {
            return bytes == other.bytes;
        }
    };

    struct PrefixKeyHash {
        std::size_t operator()(const PrefixKey & key) const noexcept;
    };

    using RouteBucket = std::unordered_map<PrefixKey, uint16_t, PrefixKeyHash>;

    std::array<RouteBucket, 129> routes_;

    static PrefixKey parseCidr(const std::string & cidr, int & prefix_len);
    static std::array<uint8_t, 16> parseIpv6(const std::string & ip);
    static void maskPrefix(PrefixKey & key, int prefix_len);
};
