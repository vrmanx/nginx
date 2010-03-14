
/*
 * Copyright (C) Ngwsx
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>


#define NGX_IOVS  16


ssize_t
ngx_readv_chain(ngx_connection_t *c, ngx_chain_t *chain)
{
    int             flags, rc;
    u_char         *prev;
    WSABUF         *buf, bufs[NGX_IOVS];
    size_t          size;
    ssize_t         n;
    ngx_err_t       err;
    ngx_array_t     vec;
    ngx_event_t    *rev;
    WSAOVERLAPPED  *ovlp;

    rev = c->read;

    if (rev->eof || rev->closed) {
        return 0;
    }

    if (rev->error) {
        return NGX_ERROR;
    }

#if (NGX_HAVE_IOCP)
    if (ngx_event_flags & NGX_USE_IOCP_EVENT) {
        n = rev->available;

        if (rev->ready && n > 0) {
            rev->available = 0;
            return n;
        }

        rev->ovlp.event = rev;
        ovlp = (WSAOVERLAPPED *) &rev->ovlp;

    } else {
        ovlp = NULL;
    }

#else
    ovlp = NULL;
#endif

    vec.nelts = 0;
    vec.elts = bufs;
    vec.size = sizeof(WSABUF);
    vec.nalloc = NGX_IOVS;
    vec.pool = c->pool;

    prev = NULL;
    size = 0;

    /* coalesce the neighbouring bufs */

    while (chain && vec.nelts < NGX_IOVS) {

        if (prev == chain->buf->last) {
            buf->len += (DWORD) (chain->buf->end - chain->buf->last);

        } else {
            buf = ngx_array_push(&vec);
            if (buf == NULL) {
                return NGX_ERROR;
            }

            buf->buf = chain->buf->last;
            buf->len = (DWORD) (chain->buf->end - chain->buf->last);
        }

        size += (size_t) (chain->buf->end - chain->buf->last);
        prev = chain->buf->end;
        chain = chain->next;
    }

    n = 0;
    flags = 0;

    ngx_log_debug2(NGX_LOG_DEBUG_EVENT, c->log, 0,
                   "WSARecv() fd:%d, size:%uz", c->fd, size);

    rc = WSARecv(c->fd, vec.elts, (DWORD) vec.nelts, (DWORD *) &n, &flags, ovlp,
                 NULL);

    err = ngx_socket_errno;

    if (rc == 0) {
        if (ngx_event_flags & NGX_USE_IOCP_EVENT) {
            rev->ready = 0;
            return NGX_AGAIN;
        }

        if ((size_t) n < size) {
            rev->ready = 0;
        }

        if (n == 0) {
            rev->eof = 1;
        }

        return n;
    }

    if (err == WSA_IO_PENDING || err == WSAEWOULDBLOCK) {
        ngx_log_debug0(NGX_LOG_DEBUG_EVENT, c->log, err, "WSARecv() not ready");
        rev->ready = 0;
        return NGX_AGAIN;
    }

    ngx_connection_error(c, err, "WSARecv() failed");

    rev->ready = 0;
    rev->error = 1;

    return NGX_ERROR;
}
