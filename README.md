# CDN77 task

This repository contains the implementation and notes for the test task:

- `dns-router/` - IPv6 ECS routing lookup;
- `NGINX_THEORY.md` - nginx proxy cache key and wildcard DNS notes;
- `nginx-module/` - nginx C header filter adding `X-Cache-Key`;
- `bonus-lua/` - optional LuaJIT FFI bonus;
- `DISCUSSION.md` - reasoning, alternatives and production notes.

## Verification

The DNS router was built and checked with:

```bash
cmake -S dns-router -B dns-router/build -DCMAKE_BUILD_TYPE=Release
cmake --build dns-router/build
./dns-router/build/dns_router
```

Expected output:

```text
CDN DNS router self-check OK
```

The Lua bonus shared library can be built with:

```bash
gcc -shared -fPIC -O2 bonus-lua/cdn77_ffi.c -o bonus-lua/libcdn77_ffi.so
```

`libcdn77_ffi.so` is a build artifact and is intentionally ignored by git.

The nginx `X-Cache-Key` module was compile-checked as a dynamic module against
nginx `1.28.0` source with:

```bash
./configure \
  --with-compat \
  --without-http_rewrite_module \
  --without-http_gzip_module \
  --add-dynamic-module=/path/to/cdn77-task/nginx-module

make modules
```

## CDN DNS routing

This repository contains a small C++ implementation of the CDN DNS routing
lookup described in the task.

The server receives a DNS query with an IPv6 ECS subnet and has to choose:

- the CDN PoP ID used for the response;
- the scope prefix length returned with the response.

The lookup is a longest-prefix match over IPv6 routing data. Each routing record
has the form:

```text
IPv6 subnet -> PoP ID
```

Example:

```text
2001:49f0:d0b8::/48 -> 174
```

For an incoming ECS subnet:

```text
2001:49f0:d0b8:8a00::/56
```

the router returns:

```text
pop=174, scope=48
```

because `/48` is the most specific matching routing prefix stored in the data.

### Data structure

The implementation uses an array of hash maps:

```text
routes[0..128] -> hash map(masked IPv6 prefix -> PoP ID)
```

The hash map key is not a string. CIDR input is parsed once into a 16-byte IPv6
address:

```cpp
struct PrefixKey {
    std::array<uint8_t, 16> bytes;
};
```

Before insertion the address is masked to the route prefix length and stored in
`routes[prefix_len]`.

During lookup the ECS address is parsed into the same 16-byte form. The router
checks prefixes from the ECS source prefix length down to `/0`:

```text
/source, /source-1, ..., /0
```

The first match is the longest matching prefix and defines both the selected PoP
and the response scope.

The lookup checks only prefixes not longer than the ECS source prefix length.

### Complexity

Lookup performs at most 129 hash table lookups for IPv6, therefore it is
constant with respect to the number of stored routing records.

Space usage is `O(n)` with respect to the number of routing records. Each stored
prefix is represented once in the bucket for its prefix length.

### Limitations

- The router expects IPv6 CIDR input.
- IPv4-mapped IPv6 addresses are not supported.
- Invalid input throws `std::invalid_argument`.

### Build

```bash
cmake -S dns-router -B dns-router/build -DCMAKE_BUILD_TYPE=Release
cmake --build dns-router/build
```

### Run

```bash
./dns-router/build/dns_router
```

Expected output:

```text
CDN DNS router self-check OK
```

The self-check loads several IPv6 routing records and verifies:

- exact match;
- fallback from `/56` ECS to a stored `/48` route;
- selection of a more specific prefix;
- no match when the ECS source prefix is shorter than the stored route prefix;
- not found.
