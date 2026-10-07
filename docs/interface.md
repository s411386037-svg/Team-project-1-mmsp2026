# TextLink 介面文件

組別：第四組　成員與角色：張家睿、彭珮珊、陳志嘉　文件版本／日期：2026-10-07

## 1. 模組與資料流

`main` 解析命令列並啟動 `chat` 或 `transfer`。`net` 建立 TCP 連線並以 `send_all`、`recv_all` 保證完整收發；`frame` 在 TCP byte stream 上加入訊息邊界；`utf8` 驗證文字；`huffman` 負責壓縮與還原。

檔案資料流如下：

```text
檔案 → huff_encode（HUFF 模式）→ FILE_BEGIN / FILE_DATA / FILE_END
     → TCP → frame_recv → huff_decode（HUFF 模式）→ .part → 正式檔案
```

RAW 模式略過 Huffman 編解碼。聊天訊息使用相同的 frame 與 Huffman 模組；文字 HUFF 資料直接放入 `TEXT_HUFF` payload。

## 2. Frame 外框

每個 TCP frame 的格式如下：

| 欄位 | bytes | 位元組順序／說明 |
|---|---:|---|
| `length` | 4 | big-endian 無號整數，值為 `type + payload` 的總 bytes，不含自身；合法範圍 1–16,777,216 |
| `type` | 1 | frame 類型 |
| `payload` | `length - 1` | 類型對應資料 |

`frame_recv` 一律先收滿 5-byte 標頭，再依 `length` 收滿 payload，因此半包、黏包不會改變訊息邊界。`length` 為 0 或超過上限時，連線視為協定錯誤。

| type | 名稱 | payload |
|---:|---|---|
| `0x01` | `TEXT_RAW` | UTF-8 文字 |
| `0x02` | `TEXT_HUFF` | 第 4 節的 HFS1 Huffman 區塊 |
| `0x10` | `FILE_BEGIN` | 檔案控制資訊 |
| `0x11` | `FILE_DATA` | 檔案資料區段 |
| `0x12` | `FILE_END` | 傳輸結束或接收結果 |

## 3. 各 type 的 payload

### `0x01 TEXT_RAW`

Payload 為合法 UTF-8 文字，不含結尾 `\0`，最多 4095 bytes。接收端以 RFC 3629 規則驗證；非法 UTF-8 會被拒絕。

### `0x02 TEXT_HUFF`

Payload 是完整的 HFS1 Huffman 區塊，符號模式固定為 `char`。程式目前會直接送出壓縮結果，因此短訊息可能因 codebook 成本而大於原文；聊天畫面會顯示原始與上線 bytes。

### `0x10 FILE_BEGIN`（傳送端 → 接收端）

| 欄位 | bytes | 說明 |
|---|---:|---|
| `mode` | 1 | `0`＝RAW、`1`＝HUFF |
| `orig_size` | 8 | big-endian；原檔 bytes，最大 64 MiB |
| `data_size` | 8 | big-endian；後續所有 `FILE_DATA` payload 的總 bytes |
| `name` | 其餘 | 檔名，無 `\0` 結尾 |

接收端只保留檔名的最後一段路徑，並將非 `0-9 A-Z a-z . - _` 字元替換為 `_`。

### `0x11 FILE_DATA`（傳送端 → 接收端）

Payload 為一段資料，每段最多 65,536 bytes。RAW 模式為原檔 bytes；HUFF 模式為完整 HFS1 區塊切割後的其中一段。接收端依序串接，累計大小必須剛好等於 `data_size`。

### `0x12 FILE_END`

- 傳送端 → 接收端：payload 為空，表示全部 `FILE_DATA` 已送完。
- 接收端 → 傳送端：payload 為 1 byte，`0` 表示解碼並寫檔成功，其他值表示失敗。

傳送端收到成功狀態才以結束碼 0 結束。

## 4. Huffman 區塊格式：HFS1

Huffman 區塊由 `huff_encode` 產生。所有 32-bit 欄位皆為 **little-endian**；這和 TCP frame 的 `length` 使用 big-endian 不同。

| offset | 欄位 | bytes | 說明 |
|---:|---|---:|---|
| 0 | magic | 4 | ASCII `HFS1` |
| 4 | symbol mode | 4 | `0`＝byte、`1`＝UTF-8 code point、`2`＝16-bit PCM sample |
| 8 | original size | 4 | 還原後總 bytes |
| 12 | region offset | 4 | 實際進行 Huffman 編碼的起點 |
| 16 | region size | 4 | 實際進行 Huffman 編碼的 bytes 數 |
| 20 | leaf count K | 4 | codebook 中的符號種類數 |
| 24 | codebook | `K × 8` | 每項為 `symbol` 4 bytes + `frequency` 4 bytes |
| 後續 | preserved bytes | `original size - region size` | 未壓縮保留的 bytes |
| 最後 | bitstream | 可變 | Huffman 編碼資料 |

### 符號與區段

| mode | 符號 | region |
|---:|---|---|
| `0` byte | 一個 byte | 全部檔案 |
| `1` char | 一個合法 UTF-8 code point | 全部檔案；非法 UTF-8 會拒絕 |
| `2` s16 | 一個 little-endian 16-bit PCM sample | 僅 WAV `data` chunk 中完整的 sample |

WAV 模式會解析 RIFF chunk，不假設 `data` 固定在第 44 byte。檔頭、非 data chunk 及 data chunk 最後無法組成 sample 的 1 byte 都放在 `preserved bytes`，並依原位置還原。

### Codebook 與 bitstream

Codebook 依 symbol 遞增儲存。編碼端與解碼端使用相同規則建樹：優先合併頻率較小的節點；頻率相同時以節點索引決定順序。因此接收端只需 codebook 中的 symbol 與 frequency 就能重建相同樹與碼字。

bitstream 採 MSB-first：每個 byte 由 bit 7 至 bit 0 依序填入。最後未填滿的位置為 0；解碼端依 codebook 頻率推得有效 bit 數，並驗證補位必須為 0。

空輸入的 `leaf count` 為 0，bitstream 為空。只有一種符號時，程式仍為每個符號寫入一個 bit `0`，避免零長碼字造成無法判定重複次數。

## 5. 錯誤處理

| 情況 | 行為 |
|---|---|
| `length` 為 0 或超過 16 MiB | 回報協定錯誤並結束連線 |
| 未知 type 或不符合預期的 frame 順序 | 回報協定錯誤，傳輸失敗 |
| IP、port、連線失敗 | 印出錯誤訊息，結束碼非 0 |
| 傳到一半斷線或資料量不等於 `data_size` | 傳輸失敗，不產生正式輸出檔 |
| HFS1 magic、欄位、codebook、有效 bit 或還原結果不合法 | 拒絕解碼，結束碼非 0 |
| 解碼後大小超過宣告的 `orig_size` | 拒絕資料，避免過度配置記憶體 |
| 非法 UTF-8 文字 | 拒絕該則訊息或退回 byte 符號處理檔案 |
| 寫檔失敗 | 保留失敗狀態，不將 `.part` 改名為正式檔案 |

## 6. 已知限制

- 檔案最大為 64 MiB，因為傳送端會先將整個檔案讀入記憶體以統計頻率。
- 聊天訊息最多 4095 bytes。
- 聊天 HUFF 模式每則訊息都附帶自己的 codebook；短訊息通常會因額外資訊而膨脹。
- 接收端檔名僅支援安全 ASCII 字元；其他字元會替換為 `_`。
- Huffman 區塊的 `original size`、region 欄位與頻率目前以 32-bit 儲存，與本程式 64 MiB 檔案上限相容。
