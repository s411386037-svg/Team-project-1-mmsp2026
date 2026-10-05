#include "textlink.h"


int utf8_validate(const uint8_t *s, size_t n) {
    size_t i = 0;

    if (s == NULL && n != 0) {
        return TL_ERR_DATA;
    }

    while (i < n) {
        uint8_t first = s[i];
        uint32_t cp;
        uint32_t minimum;
        size_t count;

        
        if (first <= 0x7F) {
            i++;
            continue;
        } else if (first >= 0xC2 && first <= 0xDF) {
            count = 2;
            cp = first & 0x1Fu;
            minimum = 0x80u;
        } else if (first >= 0xE0 && first <= 0xEF) {
            count = 3;
            cp = first & 0x0Fu;
            minimum = 0x800u;
        } else if (first >= 0xF0 && first <= 0xF4) {
            count = 4;
            cp = first & 0x07u;
            minimum = 0x10000u;
        } else {
            return TL_ERR_DATA;
        }

        
        if (n - i < count) {
            return TL_ERR_DATA;
        }

        for (size_t j = 1; j < count; j++) {
            uint8_t next = s[i + j];

            if ((next & 0xC0u) != 0x80u) {
                return TL_ERR_DATA;
            }

            cp = (cp << 6) | (next & 0x3Fu);
        }

        if (cp < minimum ||
            (cp >= 0xD800u && cp <= 0xDFFFu) ||
            cp > 0x10FFFFu) {
            return TL_ERR_DATA;
        }

        i += count;
    }

    return TL_OK;
}