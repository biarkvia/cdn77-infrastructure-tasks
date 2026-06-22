# LuaJIT FFI bonus

This is an optional bonus part. It is separate from the `X-Cache-Key` nginx
module. The cache header requirement is implemented in C and does not use Lua or
OpenResty.

The bonus exports one C function and calls it from Lua through LuaJIT FFI.

## Build the shared library

From the repository root:

```bash
gcc -shared -fPIC -O2 bonus-lua/cdn77_ffi.c -o bonus-lua/libcdn77_ffi.so
```

The exported function is:

```c
int cdn77_add(int a, int b)
{
    return a + b;
}
```

## Nginx / Lua requirement

The example expects nginx built with `lua-nginx-module`, or OpenResty.

If building nginx manually, use nginx with `ngx_devel_kit` and
`lua-nginx-module`:

```bash
export LUAJIT_LIB=/usr/local/lib
export LUAJIT_INC=/usr/local/include/luajit-2.1

./configure \
  --prefix=/tmp/nginx-lua \
  --with-compat \
  --add-module=/path/to/ngx_devel_kit \
  --add-module=/path/to/lua-nginx-module

make -j$(nproc)
```

The nginx config example is in:

```text
bonus-lua/nginx.conf.example
```

It loads the shared library with:

```lua
local lib = ffi.load("/mnt/d/cdn77-task/bonus-lua/libcdn77_ffi.so")
```

Change the path if the repository is located elsewhere.

## Expected result

With nginx running on the example config:

```bash
curl http://127.0.0.1:8080/ffi
```

Expected response:

```text
5
```
