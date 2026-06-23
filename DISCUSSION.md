# Discussion

This repository contains three main parts: CDN DNS routing, nginx cache-key
notes with a small C module, and wildcard DNS lookup notes. The LuaJIT FFI part
is kept as a separate bonus.

## CDN DNS

The routing problem is a longest-prefix match over IPv6 ECS data. I chose a
fixed array of hash maps:

```text
routes[0..128] -> hash map(masked IPv6 prefix -> pop)
```

On insert, the CIDR is parsed once, converted to 16 bytes, masked to its prefix
length and stored in the bucket for that length. On lookup, the ECS CIDR is
parsed and the code checks only prefixes that are not longer than the ECS source
prefix length:

```text
/source, /source-1, ..., /0
```

The first hit is the selected PoP and the response scope.

This gives at most 129 hash lookups for IPv6. That is constant with respect to
the number of routing records, while memory stays `O(n)` because each route is
stored once. The implementation is also easy to reason about and easy to test.

I considered a Patricia/radix trie. It is a natural fit for IP prefixes and
could reduce the number of hash lookups, but for IPv6 the maximum depth is fixed
at 128 bits anyway. For this task the hash-bucket solution is smaller, has a
clear worst case, and avoids tricky compressed-node code.

One important ECS detail is that a route more specific than the ECS source
prefix must not be matched, because the resolver did not provide those address
bits. The self-test covers this with a stored `/48` route and an incoming `/32`
ECS query.

For a production authoritative DNS server I would add:

- input loading from the real routing-data format;
- duplicate and overlap validation;
- metrics for route misses and loaded prefix counts;
- atomic reload of the routing table;
- fuzz tests for CIDR parsing;
- probably `inet_pton` or the server's existing IP parser instead of a local
  parser.

RFC 7871 also warns about overlapping tailored responses because intermediate
resolver caches can observe answers in an unlucky order. For this exercise I
implemented longest-prefix match over the given routing data. In production I
would either reject problematic overlaps during load or deaggregate broader
prefixes as described by the RFC.

## nginx cache key

The nginx part is not solved by repeating `proxy_cache_key` from configuration.
That directive is only the input expression. The proxy module evaluates it into
request key parts in `r->cache->keys`; the file-cache code then computes `CRC32`
and a 16-byte `MD5` digest in `r->cache->key`.

That digest is what nginx uses for the shared-memory cache index and for the
cache file name. The filename hint from the task points exactly there: the cache
file basename is the MD5 digest rendered as 32 lowercase hex characters.

The module in `nginx-module/` is an HTTP header filter. It checks that the
request has a cache context, reads `r->cache->key`, converts the 16 bytes to hex
and adds:

```text
X-Cache-Key: <32 hex chars>
```

to the response sent to the client. It does not send anything to the origin and
does not use Lua or OpenResty.

From the OS point of view there are several locations involved:

- `r->cache->keys` and `r->cache->key` live in nginx worker private memory,
  allocated from request-related pools;
- active cache metadata lives in the configured shared memory zone, mapped into
  all workers and indexed with nginx rbtree nodes;
- cached responses live as regular files on disk, usually passing through the
  kernel page cache.

The part I had to check most carefully was where the configuration expression
ends and where the real cache digest starts. The useful source path is:

```text
ngx_http_proxy_create_key()
ngx_http_file_cache_create_key()
ngx_http_file_cache_name()
```

In production I would probably make the header conditional, for example behind
a module directive, because exposing cache keys is useful for debugging but not
always something I would want enabled on every public response.

## Wildcard DNS lookup

The wildcard DNS task is about avoiding a scan over all stored records. The
nginx referer module uses the right idea: exact hash plus wildcard hash tables.
At load time names are normalized and split into exact names and wildcard
names. At request time the code hashes the exact name first, then checks
wildcard candidates at label boundaries.

For DNS wildcard records such as:

```text
*.bbb.cc.d
```

or an internal suffix representation:

```text
.bbb.cc.d
```

the query:

```text
q.w.e.r.t.y.bbb.cc.d
```

matches by finding the suffix `bbb.cc.d` in the wildcard hash, not by comparing
the query with every wildcard record.

The work depends on the length of the domain name and number of labels. DNS
names are bounded to 255 bytes, so this is constant with respect to the number
of stored records.

The practical DNS caveat is that wildcards are not general glob patterns. A
wildcard must be the leftmost label, exact existing names can block wildcard
synthesis, and zone cuts matter. For the task, explaining the hash structure and
bounded lookup is enough; for a real authoritative server I would implement the
full RFC 1034/RFC 4592 lookup semantics.

## LuaJIT FFI bonus

The bonus is intentionally separate from the cache-key module. The main nginx
requirement says not to use Lua for `X-Cache-Key`, so the cache header is done
in C.

The bonus exports a tiny C function:

```c
int cdn77_add(int a, int b)
{
    return a + b;
}
```

and calls it from `content_by_lua_block` through LuaJIT FFI. This demonstrates
the requested Lua/C boundary without mixing it into the cache solution.

## Verification

The DNS router self-test covers:

- exact match;
- fallback from a longer ECS prefix to a stored shorter route;
- choosing a more specific stored prefix;
- refusing to match a stored prefix longer than the ECS source prefix;
- miss for an unrelated prefix.

The nginx module was compile-checked against nginx source as a dynamic module.
The Lua bonus shared object was built separately and the nginx config example
shows how to call it through FFI.

Overall this took me roughly one working day. The DNS routing part was the
simplest after choosing the data structure. Most of the time went into nginx:
checking how the proxy cache key is built, where it is stored, and making sure
the header filter uses the computed key rather than a reconstructed config
string.
