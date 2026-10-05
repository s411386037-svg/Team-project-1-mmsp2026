#include "textlink.h"


int frame_pack_header(uint8_t hdr[TL_HDR_LEN],
                      uint8_t type, size_t payload_len) {
    if (payload_len >= (size_t)TL_MAX_FRAME) {
        return TL_ERR_PROTO;
    }

    uint32_t length = (uint32_t)payload_len + 1u;

    hdr[0] = (uint8_t)(length >> 24);
    hdr[1] = (uint8_t)(length >> 16);
    hdr[2] = (uint8_t)(length >> 8);
    hdr[3] = (uint8_t)length;
    hdr[4] = type;

    return TL_OK;
}


int frame_parse_header(const uint8_t hdr[TL_HDR_LEN],
                       uint8_t *type, size_t *payload_len) {
    uint32_t length = ((uint32_t)hdr[0] << 24)
                    | ((uint32_t)hdr[1] << 16)
                    | ((uint32_t)hdr[2] << 8)
                    | (uint32_t)hdr[3];

    if (length == 0 || length > TL_MAX_FRAME) {
        return TL_ERR_PROTO;
    }

    *type = hdr[4];
    *payload_len = (size_t)(length - 1u);

    return TL_OK;
}


int frame_send(socket_t s, uint8_t type,
               const uint8_t *payload, size_t len) {
    uint8_t hdr[TL_HDR_LEN];

    int rc = frame_pack_header(hdr, type, len);
    if (rc != TL_OK) {
        return rc;
    }

    net_send_lock();

    rc = send_all(s, hdr, TL_HDR_LEN);
    if (rc == TL_OK && len > 0) {
        rc = send_all(s, payload, len);
    }

    net_send_unlock();
    return rc;
}


int frame_recv(socket_t s, uint8_t *type,
               uint8_t **payload, size_t *len) {
    uint8_t hdr[TL_HDR_LEN];

    *payload = NULL;
    *len = 0;

    /* 先收滿 5 bytes 標頭。 */
    int rc = recv_all(s, hdr, TL_HDR_LEN);
    if (rc != TL_OK) {
        return rc;
    }

    size_t n = 0;
    rc = frame_parse_header(hdr, type, &n);
    if (rc != TL_OK) {
        return rc;
    }

    if (n >= TL_MAX_FRAME) {
        return TL_ERR_PROTO;
    }

    uint8_t *buf = (uint8_t *)malloc(n + 1);
    if (buf == NULL) {
        return TL_ERR_NOMEM;
    }

  
    if (n > 0) {
        rc = recv_all(s, buf, n);
        if (rc != TL_OK) {
            free(buf);
            return rc;
        }
    }

    buf[n] = '\0';
    *payload = buf;
    *len = n;

    return TL_OK;
}