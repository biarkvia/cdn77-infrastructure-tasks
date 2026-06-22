#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

static ngx_int_t ngx_http_x_cache_key_header_filter(ngx_http_request_t * r);
static ngx_int_t ngx_http_x_cache_key_filter_init(ngx_conf_t * cf);

static ngx_http_output_header_filter_pt ngx_http_next_header_filter;

static ngx_http_module_t ngx_http_x_cache_key_filter_module_ctx = {
    NULL,
    ngx_http_x_cache_key_filter_init,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};

ngx_module_t ngx_http_x_cache_key_filter_module = {
    NGX_MODULE_V1,
    &ngx_http_x_cache_key_filter_module_ctx,
    NULL,
    NGX_HTTP_MODULE,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NGX_MODULE_V1_PADDING
};

static ngx_int_t ngx_http_x_cache_key_header_filter(ngx_http_request_t * r)
{
#if (NGX_HTTP_CACHE)
    u_char * value;
    ngx_table_elt_t * header;

    if (r != r->main || r->cache == NULL) {
        return ngx_http_next_header_filter(r);
    }

    value = ngx_pnalloc(r->pool, 2 * NGX_HTTP_CACHE_KEY_LEN);
    if (value == NULL) {
        return NGX_ERROR;
    }

    (void) ngx_hex_dump(value, r->cache->key, NGX_HTTP_CACHE_KEY_LEN);

    header = ngx_list_push(&r->headers_out.headers);
    if (header == NULL) {
        return NGX_ERROR;
    }

    header->hash = 1;
    ngx_str_set(&header->key, "X-Cache-Key");
    header->value.len = 2 * NGX_HTTP_CACHE_KEY_LEN;
    header->value.data = value;
#endif

    return ngx_http_next_header_filter(r);
}

static ngx_int_t ngx_http_x_cache_key_filter_init(ngx_conf_t * cf)
{
    ngx_http_next_header_filter = ngx_http_top_header_filter;
    ngx_http_top_header_filter = ngx_http_x_cache_key_header_filter;

    return NGX_OK;
}
