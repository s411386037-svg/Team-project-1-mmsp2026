#include "textlink.h"

//s：要檢查的資料起點。n：要檢查幾個 bytes。
int utf8_validate(const uint8_t *s, size_t n) {
    //i 是目前正在檢查第幾個 byte
    size_t i = 0;
//若資料指標不存在，卻說要讀取非 0 bytes，表示資料不合法，立刻回傳錯誤
    if (s == NULL && n != 0) {
        return TL_ERR_DATA;
    }

    while (i < n) {
        //目前字元的第一個 byte
        uint8_t first = s[i];
        // Unicode 編號
        uint32_t cp;
        //此長度UTF-8 可接受的最小 Unicode 編號
        uint32_t minimum;
        //這個字元總共有幾個 bytes
        size_t count;

        //這段是在檢查目前的 first 是否是 ASCII 字元
        //0x00 到 0x7F 是 ASCII
        if (first <= 0x7F) {
            i++;
            continue;
            //若第一個 byte 在 C2 到 DF，代表這是 2-byte UTF-8 字元，例如 é。
        } else if (first >= 0xC2 && first <= 0xDF) {
            count = 2;
            //first & 0x1F：去掉前面的格式 bits 110，留下真正屬於 Unicode 編號的 bits
            cp = first & 0x1Fu;
            //minimum = 0x80：2-byte 字元解出來的 code point 最少要是 U+0080
            minimum = 0x80u;
            //若開頭是 E0 到 EF，代表是 3-byte UTF-8 字元，例如中文字
        } else if (first >= 0xE0 && first <= 0xEF) {
            count = 3;
            //保留 1110 後面的 4 個有效 bits
            cp = first & 0x0Fu;
            //3-byte 字元至少要代表 U+0800
            minimum = 0x800u;
            //F0 到 F4 是 4-byte UTF-8 字元😀𠮷
        } else if (first >= 0xF0 && first <= 0xF4) {
            count = 4;
            //去掉前面的格式 bits 11110保留留下第一個 byte 最後 3 個有效 bits
            cp = first & 0x07u;
            //4-byte 字元最小是 U+10000
            minimum = 0x10000u;
        } else {
            return TL_ERR_DATA;
        }

        //檢查剩下的 bytes 是否足夠組成一個完整的 UTF-8 字元。
        //中文字「你」需要 3 bytes，但只剩 2 bytes，就會判定為錯誤
        if (n - i < count) {
            return TL_ERR_DATA;
        }
//檢查並讀取 UTF-8 字元後面的 bytes。
//j = 1 開始，是因為第 0 個 byte 已經是 first，不用再讀
        for (size_t j = 1; j < count; j++) {
            //從原始資料取出這個字元接下來的 byte
            uint8_t next = s[i + j];
//UTF-8 的後續 byte 必須是0x80 =10xxxxxx，0xC0 = 11000000
//next = BD = 10111101，0xC0 = 11000000，AND 結果 = 10000000 = 0x80
            if ((next & 0xC0u) != 0x80u) {
                return TL_ERR_DATA;
            }
//10xxxxxx前兩位 10 是格式資訊，真正 Unicode 資料只有後 6 位：
//next & 0x3Fu用途是把前面的 10 去掉，只保留 6 個資料 bits
// cp << 6 是把目前已收集到的 bits 往左移 6 位，留出空間。最後 | 把新取得的 6 bits 接上去
            cp = (cp << 6) | (next & 0x3Fu);
        }
//cp < minimum代表 用了太多 bytes 來表示一個本來可以更短表示的字元
//代表 cp 落在 surrogate 範圍，這些不是可以單獨顯示的 Unicode 字元，它們只在 UTF-16 中用來組合某些大編號字元，例如 emoji
//cp > 0x10FFFFu代表超過 Unicode 可用的最大編號
        if (cp < minimum ||
            (cp >= 0xD800u && cp <= 0xDFFFu) ||
            cp > 0x10FFFFu) {
            return TL_ERR_DATA;
        }
//i += count 會直接跳到下一個 UTF-8 字元的開頭，繼續檢查。
        i += count;
    }

    return TL_OK;
}