# NGINX proxy cache key

This note describes the cache key used by nginx when `ngx_http_proxy_module`
runs with `proxy_cache` enabled.

## How the key is built

The proxy module does not search the cache by the literal text written in
`proxy_cache_key`. That directive is only an expression used to build the real
request key.

For a cached proxy request nginx first fills `r->cache->keys`, an array of
`ngx_str_t` values. Each item is one already evaluated key part:

- if `proxy_cache_key` is configured, nginx evaluates that complex value and
  stores the resulting string as a key part;
- otherwise the proxy module builds its default key parts from the request and
  proxy target data.

This is done by the proxy module in `ngx_http_proxy_create_key`.

Then the file cache code iterates over `r->cache->keys` and feeds the bytes into
two hashes:

- `CRC32`, stored in `r->cache->crc32`;
- `MD5`, stored as 16 bytes in `r->cache->key`.

This digest is produced in `ngx_http_file_cache_create_key`.

So the cache key used internally is not the configuration template. It is the
MD5 digest of the already computed key bytes. The same computed key bytes are
also kept so nginx can write and later validate the original key stored inside
the cache file.

`r->cache->main` is a copy of the initial 16-byte MD5 key. It is relevant for
`Vary`, where nginx may need a secondary variant key while still remembering the
main cache key.

## Where the key is stored

The key exists in several forms.

`r->cache->keys`

Array of computed key parts. It belongs to the current HTTP request and is
allocated from the worker process request pool. From the operating system point
of view this is private memory in one nginx worker process. It is gone when the
request pool is destroyed.

`r->cache->key`

The 16-byte MD5 digest computed from `r->cache->keys`. It also belongs to the
current request in the worker process private memory. This is the value a header
filter can expose as `X-Cache-Key` after converting it to 32 hex
characters.

Shared memory cache node / rbtree

The cache zone configured by `proxy_cache_path ... keys_zone=...` is nginx shared
memory. All workers map it into their address space. The OS sees it as shared
memory, not normal per-request heap.

In that zone nginx stores `ngx_http_file_cache_node_t` nodes in an rbtree. The
node key is the 16-byte MD5 split between:

- `node.key`, the rbtree key field;
- `fcn->key`, the remaining bytes stored in the cache node.

This is the in-memory index used to find cache metadata without scanning files
on disk.

Cache file on disk

The cache file name is derived from the same 16-byte MD5 digest. Nginx renders
the digest as 32 lowercase hex characters and then applies the directory levels
configured by `proxy_cache_path`.

The file name is built in `ngx_http_file_cache_name`.

From the OS point of view this is a regular file in the filesystem. Reads and
writes pass through the kernel page cache and persist on disk according to the
filesystem behavior.

Inside the file header nginx also stores cache metadata, including validity
timestamps, `CRC32`, `ETag`, `Vary` data and the variant hash. After the binary
header nginx writes the original computed key bytes prefixed by its cache-key
marker. This lets nginx validate that the file content still belongs to the
request key it is trying to use.

## What the key is used for

The computed key is used for four related purposes.

Shared memory lookup

Nginx uses the MD5 key to look up the cache node in the shared-memory rbtree.
That node contains metadata such as expiry time, usage counters, file size,
update flags and the file identity.

Cache file name

The same MD5 key becomes the cache file basename in hex form. This is why the
file name in the cache directory is a practical hint for finding the key.

Cache file validation

When nginx reads or updates a cache file, it checks the file header and the
stored key-related metadata. The `CRC32` and stored key bytes help detect that a
file does not match the request key nginx is currently processing.

`Vary` and secondary keys

For responses with `Vary`, nginx may need a variant-specific key. The main key
is kept in `r->cache->main`, while the variant hash is stored in the cache
header. This prevents serving a cached response variant for the wrong request
headers.

## Why `X-Cache-Key` cannot be done by configuration only

The required header must contain the computed cache key, not a copy of the
configuration expression.

Configuration can build strings from variables, but nginx does not expose a
standard variable containing the final 16-byte `r->cache->key` MD5 digest. The
`proxy_cache_key` directive is not itself such a variable. It is an input used by
the proxy module before the file cache code computes `CRC32` and `MD5`.

Therefore a configuration-only solution can only repeat an approximation of the
key expression, for example by combining `$scheme`, host and URI variables. That
does not satisfy the requirement because it does not read the real key nginx
actually used for cache lookup and file naming.

The correct implementation point is C code inside nginx, for example an HTTP
header filter. After the cache key has been computed, the filter can check
`r->cache != NULL`, read `r->cache->key`, convert the 16 bytes to 32 hex
characters and append:

```text
X-Cache-Key: <32 hex chars>
```

to the response sent to the client.

Relevant nginx source files:

- `src/http/modules/ngx_http_proxy_module.c`
- `src/http/ngx_http_file_cache.c`
- `src/http/ngx_http_cache.h`

## Wildcard DNS lookup

The wildcard DNS part should not be implemented by comparing the incoming name
with every stored wildcard record. The records are normalized and indexed at
load time.

Normalization should make names canonical before insertion and lookup:

- convert labels to lowercase;
- use one representation for the trailing root dot;
- split names only on label boundaries;
- store exact names and wildcard names separately.

The data structure follows the same principle nginx uses in
`ngx_http_referer_module`: a combined hash made of an exact hash and wildcard
hash tables. In nginx terms this is the `ngx_hash_combined_t` idea: exact names
are looked up first, wildcard-head and wildcard-tail hashes are used only if the
exact lookup misses.

For DNS wildcard records such as:

```text
*.bbb.cc.d
```

or an equivalent internal representation:

```text
.bbb.cc.d
```

the useful index is the suffix after the wildcard. A query like:

```text
q.w.e.r.t.y.bbb.cc.d
```

does not need to be compared with all wildcard records. The lookup walks the
labels of the queried name and checks possible suffixes through the wildcard
hash structure. If the suffix `bbb.cc.d` is present in the wildcard hash and the
query has at least one label before it, the wildcard record matches.

The lookup order is:

1. normalize the queried domain name;
2. try the exact-name hash;
3. if exact lookup misses, try the wildcard hash at label boundaries;
4. return match or miss.

The amount of work depends on the length of the queried domain name, not on the
number of stored records. DNS names are limited to 255 bytes, so for this task
the lookup time is constant with respect to the number of records.

Relevant nginx source files:

- `src/http/modules/ngx_http_referer_module.c`
- `src/core/ngx_hash.c`
