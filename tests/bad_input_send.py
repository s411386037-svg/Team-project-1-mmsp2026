#!/usr/bin/env python3
"""bad_input_send.py -- TextLink 壞輸入測試工具（V 角色新增）。

用途：故意送出不合法的 TextLink frame，確認接收端會印出錯誤並正常結束，
而不是當機、卡住或越界。

先在 A 電腦開一個對應的接收程式，再從 B 電腦執行本工具：

  聊天類（zero、huge、unknown、utf8、codebook）：
    A: .\textlink.exe chat server 6123 --raw
    B: python .\tests\bad_input_send.py <A的IP> 6123 <case>

  中途斷線（disconnect）：
    A: .\textlink.exe recv 6123 received
    B: python .\tests\bad_input_send.py <A的IP> 6123 disconnect

case 可為：
  zero        length = 0（非法）
  huge        length = 16 MiB + 1（超過 TL_MAX_FRAME）
  unknown     合法長度，但 type = 0x7F（未知 type）
  utf8        TEXT_RAW 的 payload 是非法 UTF-8（C0 80）
  codebook    TEXT_HUFF 的 HFS1 codebook 含 frequency = 0（損壞）
  disconnect  宣告要傳 100 bytes，實際只送 3 bytes 後斷線

每次只跑一個 case，因為接收端收到壞封包後正常情況會關閉該連線。
本檔只使用 Python 標準函式庫，格式配合 TextLink 的 frame 規格。
"""

import socket
import struct
import sys
import time

TEXT_RAW = 0x01
TEXT_HUFF = 0x02
FILE_BEGIN = 0x10
FILE_DATA = 0x11
TL_MAX_FRAME = 16 * 1024 * 1024


def frame(frame_type, payload=b""):
    """組成 [4-byte big-endian length][type][payload]。"""
    return struct.pack(">I", len(payload) + 1) + bytes([frame_type]) + payload


def bad_hfs1_codebook():
    """組成一個 TextLink huffman.c 應拒絕的 HFS1 壓縮資料。

    HFS1 標頭欄位皆為 little-endian。leaf 的 frequency 不能是 0，
    這裡故意填 0，讓 huff_decode() 回傳資料錯誤。
    """
    return (
        b"HFS1"
        + struct.pack("<IIIII", 0, 1, 0, 1, 1)  # mode、原長、region 起點、長度、leaves
        + struct.pack("<II", ord("A"), 0)       # symbol A，但 frequency 非法地為 0
        + b"\x00"                                # 一個無意義的 bit byte
    )


def send_case(sock, case):
    if case == "zero":
        sock.sendall(struct.pack(">I", 0) + bytes([TEXT_RAW]))
        return "送出 length = 0 的標頭。"

    if case == "huge":
        sock.sendall(struct.pack(">I", TL_MAX_FRAME + 1) + bytes([TEXT_RAW]))
        return "送出 length = 16 MiB + 1 的標頭。"

    if case == "unknown":
        sock.sendall(frame(0x7F))
        return "送出未知 type 0x7F。"

    if case == "utf8":
        sock.sendall(frame(TEXT_RAW, b"\xC0\x80"))
        return "送出非法 UTF-8 overlong 序列 C0 80。"

    if case == "codebook":
        sock.sendall(frame(TEXT_HUFF, bad_hfs1_codebook()))
        return "送出 frequency = 0 的損壞 HFS1 codebook。"

    if case == "disconnect":
        # FILE_BEGIN: mode(1) + original bytes(8 BE) + wire bytes(8 BE) + filename
        begin = b"\x00" + struct.pack(">QQ", 100, 100) + b"interrupted.bin"
        sock.sendall(frame(FILE_BEGIN, begin))
        sock.sendall(frame(FILE_DATA, b"abc"))
        return "已宣告 100 bytes，實際送 3 bytes 後會立刻斷線。"

    raise ValueError(case)


def main():
    cases = {"zero", "huge", "unknown", "utf8", "codebook", "disconnect"}
    if len(sys.argv) != 4 or sys.argv[3] not in cases:
        sys.exit(__doc__)

    ip, port, case = sys.argv[1], int(sys.argv[2]), sys.argv[3]
    with socket.create_connection((ip, port), timeout=10) as sock:
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        print(f"已連到 {ip}:{port}；case = {case}")
        print(send_case(sock, case))
        # 給 OS 一點時間把最後資料送出；with 結束時會關閉連線。
        time.sleep(0.2)

    print("已主動關閉連線。請查看 A：應顯示錯誤並回到 PowerShell，不能當機。")


if __name__ == "__main__":
    main()
