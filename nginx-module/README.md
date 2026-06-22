# X-Cache-Key nginx module

This is a minimal nginx HTTP header filter module. It adds a response header
with the computed proxy cache key:

```text
X-Cache-Key: <32 hex chars>
```

The module reads `r->cache->key`, converts the 16-byte MD5 digest to hex and
adds the header to the response sent to the client.

It does not use Lua or OpenResty.

## Build as a dynamic module

Build it against the same nginx source version as the target nginx binary:

```bash
cd /path/to/nginx-source

./configure \
  --with-compat \
  --add-dynamic-module=/path/to/cdn77-task/nginx-module

make modules
```

The dynamic module will be built as:

```text
objs/ngx_http_x_cache_key_filter_module.so
```

This module was compile-checked against nginx `1.28.0` source with:

```bash
./configure \
  --with-compat \
  --without-http_rewrite_module \
  --without-http_gzip_module \
  --add-dynamic-module=/path/to/cdn77-task/nginx-module

make modules
```

## Build statically

```bash
cd /path/to/nginx-source

./configure \
  --add-module=/path/to/cdn77-task/nginx-module

make
```

## Example nginx config

See:

```text
nginx-module/nginx.conf.example
```

The module has no directives. Once loaded, it adds `X-Cache-Key` when the current
request has nginx cache context available.
