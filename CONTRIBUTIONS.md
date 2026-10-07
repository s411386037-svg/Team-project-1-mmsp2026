個人貢獻

| 組員 | 負責 TODO | 實作內容 | 相關檔案 | 驗證重點 |
|---|---|---|---|---|
| 陳志嘉 | TODO 1、2、3 | 完成 frame 的 5-byte 標頭封裝（長度與 type）、標頭解析與 payload 長度合法性檢查；完成整段資料的 UTF-8 合法性驗證，處理多位元組字元、截斷序列、overlong encoding、代理區與超出 Unicode 範圍等不合法輸入。 | `src/frame.c`、`src/utf8.c` | 驗證一般封包、半包與黏包的 frame 處理；驗證中文、emoji 與不合法 UTF-8 輸入。 |
| 張家睿 | TODO 4 | 完成 Huffman 編碼：依 `SYM_BYTE`、`SYM_CHAR`、`SYM_S16` 三種符號類型統計資料並建立可獨立解碼的壓縮區塊，包含符號資訊、原始長度、codebook 與 bitstream；不適用的輸入回傳資料錯誤，供呼叫端改用 byte 模式處理。 | `src/huffman.c`（`huff_encode`） | 驗證空資料、單一符號、UTF-8 文字與 16-bit PCM WAV 的編碼結果可供解碼端還原。 |
| 彭珮珊 | TODO 5 | 完成 Huffman 解碼：讀取壓縮區塊內的符號類型、長度、codebook 與 bitstream，重建原始 bytes；針對截斷資料、不合理長度、非法 codebook、輸出上限與 bitstream 不完整等情況進行邊界檢查，避免越界讀寫。 | `src/huffman.c`（`huff_decode`） | 驗證 encode/decode 後資料逐 byte 相同；驗證截斷壓縮區塊及 `max_out` 小於原始長度時正確拒絕。