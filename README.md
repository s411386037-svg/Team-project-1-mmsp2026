# TextLink

TextLink 是以 C 實作的 TCP 傳輸程式，支援 UTF-8 文字聊天、文字檔與 WAV 檔傳輸。每次傳輸可選擇直接傳送（RAW）或使用 Huffman coding 壓縮（HUFF）；接收端會將資料還原為原始 bytes。

## 功能

- TCP 文字聊天，支援中文、emoji 與 1–4 bytes UTF-8 字元。
- 長度前綴 frame，能正確處理 TCP 的半包與黏包。
- 聊天中使用 `/files` 與 `/send` 傳送 `.txt`、`.wav` 檔案。
- 命令列傳送／接收單一檔案，最大檔案大小為 64 MiB。
- Huffman 符號依資料類型選擇：文字用 UTF-8 code point、16-bit PCM WAV 用 sample，其他資料用 byte。
- `send` 與 `recv` 在 stderr 輸出 `STATS`，記錄實際上線 bytes、壓縮率與時間。

## 建置與測試

在專案根目錄執行：

```bash
make
make test
```

`make` 會產生 `textlink`；Windows MinGW 可使用 `mingw32-make`。`make test` 會測試 frame、UTF-8 驗證，以及 byte／UTF-8 字元／16-bit sample 的 Huffman 編解碼與邊界條件。

## 使用方式

預設模式為 `--huff`。`--raw` 代表不壓縮。

```text
textlink chat server <port> [--bind <ip>] [--raw|--huff]
textlink chat client <ip> <port> [--raw|--huff]
textlink recv <port> <outdir> [--bind <ip>]
textlink send <ip> <port> <file> [--raw|--huff]
```

`--bind` 僅用在監聽端；未指定時會監聽所有網路介面。`<ip>` 是對方電腦的 IPv4 位址；同一台電腦測試可使用 `127.0.0.1`。

### 文字聊天

電腦 A 的 IP 為 `192.168.1.10` 時：

```bash
# 電腦 A
./textlink chat server 5000

# 電腦 B
./textlink chat client 192.168.1.10 5000
```

聊天中可使用：

```text
/files [資料夾]       列出 .txt 與 .wav 檔案
/send <編號或路徑>    傳送檔案
/raw                  切換為原樣傳送
/huff                 切換為 Huffman 壓縮
/quit                 離開聊天
```

接收的檔案會存入 `received/`。聊天訊息與檔案傳輸都會顯示原始大小、實際上線大小、壓縮率及時間資訊。

### 命令列傳檔

```bash
# 接收端：監聽並將檔案存入 out/
./textlink recv 5000 out

# 傳送端：傳送 big.txt
./textlink send 192.168.1.10 5000 big.txt --huff
```

成功時程式結束碼為 0；連線、格式、解碼或寫檔失敗時，結束碼非 0。接收端先將檔案寫為 `.part`，確認資料完整後才改為正式檔名。

## 壓縮率與量測

本專題的壓縮率定義為：

```text
ratio = wire_bytes / file_bytes
```

`wire_bytes` 包含所有 frame 標頭、檔案控制 frame 與 Huffman codebook；因此大於 1 代表壓縮後實際傳輸資料反而較大。

範例：

```text
STATS role=send mode=huff sym=char file_bytes=1048576 wire_bytes=743210 ratio=0.7088 encode_ms=35.2 send_ms=12.8 total_ms=51.0
```

## 專案結構

```text
src/        程式實作：網路、frame、UTF-8、Huffman、聊天與檔案傳輸
include/    共用標頭檔
tests/      自動測試與測試資料產生程式
docs/       介面與封包格式說明
Makefile    建置與測試指令
```

詳細的 frame、檔案傳輸與 Huffman 區塊格式請見 [docs/interface.md](docs/interface.md)。
