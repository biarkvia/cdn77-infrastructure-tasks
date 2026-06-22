#include "Router.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

std::string trim(std::string_view value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
        value.remove_prefix(1);
    }

    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.remove_suffix(1);
    }

    return std::string(value);
}

uint16_t parseHextet(std::string_view token) {
    if (token.empty() || token.size() > 4) {
        throw std::invalid_argument("invalid IPv6 hextet");
    }

    uint16_t value = 0;

    for (char ch : token) {
        unsigned digit = 0;

        if (ch >= '0' && ch <= '9') {
            digit = static_cast<unsigned>(ch - '0');
        } else if (ch >= 'a' && ch <= 'f') {
            digit = static_cast<unsigned>(ch - 'a' + 10);
        } else if (ch >= 'A' && ch <= 'F') {
            digit = static_cast<unsigned>(ch - 'A' + 10);
        } else {
            throw std::invalid_argument("invalid IPv6 hextet");
        }

        value = static_cast<uint16_t>((value << 4) | digit);
    }

    return value;
}

std::vector<uint16_t> parseHextets(std::string_view part) {
    std::vector<uint16_t> hextets;

    if (part.empty()) {
        return hextets;
    }

    std::size_t start = 0;

    while (start <= part.size()) {
        const std::size_t colon = part.find(':', start);
        const std::size_t end = colon == std::string_view::npos ? part.size() : colon;

        hextets.push_back(parseHextet(part.substr(start, end - start)));

        if (colon == std::string_view::npos) {
            break;
        }

        start = colon + 1;
    }

    return hextets;
}

int parsePrefixLength(std::string_view value) {
    if (value.empty()) {
        throw std::invalid_argument("missing prefix length");
    }

    int prefix = 0;

    for (char ch : value) {
        if (ch < '0' || ch > '9') {
            throw std::invalid_argument("invalid prefix length");
        }

        prefix = prefix * 10 + (ch - '0');

        if (prefix > 128) {
            throw std::invalid_argument("IPv6 prefix length out of range");
        }
    }

    return prefix;
}

} // namespace

std::size_t Router::PrefixKeyHash::operator()(const PrefixKey & key) const noexcept {
    std::size_t hash = 1469598103934665603ull;

    for (uint8_t byte : key.bytes) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }

    return hash;
}

void Router::insert(const std::string & cidr, uint16_t pop) {
    int prefix_len = 0;
    PrefixKey key = parseCidr(cidr, prefix_len);
    maskPrefix(key, prefix_len);

    routes_[static_cast<std::size_t>(prefix_len)][key] = pop;
}

RouteResult Router::route(const std::string & ecs_cidr) const {
    int source_prefix_len = 0;
    PrefixKey key = parseCidr(ecs_cidr, source_prefix_len);

    for (int scope = source_prefix_len; scope >= 0; --scope) {
        PrefixKey candidate = key;
        maskPrefix(candidate, scope);

        const auto & bucket = routes_[static_cast<std::size_t>(scope)];
        const auto found = bucket.find(candidate);

        if (found != bucket.end()) {
            return RouteResult{found->second, scope, true};
        }
    }

    return RouteResult{};
}

Router::PrefixKey Router::parseCidr(const std::string & cidr, int & prefix_len) {
    const std::string normalized = trim(cidr);
    const std::size_t slash = normalized.find('/');

    if (slash == std::string::npos || normalized.find('/', slash + 1) != std::string::npos) {
        throw std::invalid_argument("expected IPv6 CIDR");
    }

    const std::string ip_part = trim(std::string_view(normalized).substr(0, slash));
    const std::string prefix_part = trim(std::string_view(normalized).substr(slash + 1));

    if (ip_part.empty()) {
        throw std::invalid_argument("missing IPv6 address");
    }

    PrefixKey key;
    key.bytes = parseIpv6(ip_part);
    prefix_len = parsePrefixLength(prefix_part);

    return key;
}

std::array<uint8_t, 16> Router::parseIpv6(const std::string & ip) {
    const std::string normalized = trim(ip);

    if (normalized.empty()) {
        throw std::invalid_argument("missing IPv6 address");
    }

    if (normalized.find('.') != std::string::npos) {
        throw std::invalid_argument("IPv4-embedded IPv6 is not supported");
    }

    const std::size_t compression = normalized.find("::");

    std::vector<uint16_t> hextets;

    if (compression == std::string::npos) {
        hextets = parseHextets(normalized);

        if (hextets.size() != 8) {
            throw std::invalid_argument("IPv6 address must contain 8 hextets without ::");
        }

    } else {
        if (normalized.find("::", compression + 2) != std::string::npos) {
            throw std::invalid_argument("IPv6 address contains multiple :: compressions");
        }

        const std::string_view view(normalized);
        std::vector<uint16_t> left = parseHextets(view.substr(0, compression));
        std::vector<uint16_t> right = parseHextets(view.substr(compression + 2));

        if (left.size() + right.size() >= 8) {
            throw std::invalid_argument("invalid IPv6 :: compression");
        }

        hextets.reserve(8);
        hextets.insert(hextets.end(), left.begin(), left.end());
        hextets.insert(hextets.end(), 8 - left.size() - right.size(), 0);
        hextets.insert(hextets.end(), right.begin(), right.end());
    }

    std::array<uint8_t, 16> bytes{};

    for (std::size_t i = 0; i < hextets.size(); ++i) {
        bytes[i * 2] = static_cast<uint8_t>(hextets[i] >> 8);
        bytes[i * 2 + 1] = static_cast<uint8_t>(hextets[i] & 0xff);
    }

    return bytes;
}

void Router::maskPrefix(PrefixKey & key, int prefix_len) {
    if (prefix_len < 0 || prefix_len > 128) {
        throw std::invalid_argument("IPv6 prefix length out of range");
    }

    const int full_bytes = prefix_len / 8;
    const int remaining_bits = prefix_len % 8;

    if (full_bytes < 16) {
        if (remaining_bits == 0) {
            key.bytes[static_cast<std::size_t>(full_bytes)] = 0;
        } else {
            const auto mask = static_cast<uint8_t>(0xffu << (8 - remaining_bits));
            key.bytes[static_cast<std::size_t>(full_bytes)] &= mask;
        }

        const int first_zero_byte = full_bytes + 1;
        std::fill(key.bytes.begin() + first_zero_byte, key.bytes.end(), 0);
    }
}