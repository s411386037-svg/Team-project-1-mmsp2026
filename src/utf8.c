#include "textlink.h"

/* TODO 3：檢查整段資料是否為合法 UTF-8。 */
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

        /* 根據第一個 byte 判斷字元長度。 */
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

        /* 避免讀取超出資料範圍。 */
        if (n - i < count) {
            return TL_ERR_DATA;
        }

        /* 檢查續位元組，並組出 Unicode code point。 */
        for (size_t j = 1; j < count; j++) {
            uint8_t next = s[i + j];

            if ((next & 0xC0u) != 0x80u) {
                return TL_ERR_DATA;
            }

            cp = (cp << 6) | (next & 0x3Fu);
        }

        /* 拒絕 overlong、代理區及超過 U+10FFFF。 */
        if (cp < minimum ||
            (cp >= 0xD800u && cp <= 0xDFFFu) ||
            cp > 0x10FFFFu) {
            return TL_ERR_DATA;
        }

        i += count;
    }

    return TL_OK;
}