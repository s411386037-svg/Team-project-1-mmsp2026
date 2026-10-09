#!/usr/bin/env python3
r"""measure.py -- TextLink 重複量測與原始 CSV 記錄工具（V 角色新增）。

本工具會執行 textlink.exe，收集每次傳輸的 STATS、程式結束碼、SHA-256，
並寫進 CSV。它不需要任何額外安裝，只使用 Python 標準函式庫。

一、同一台電腦快速測（會自動開 recv 與 send，並自動比 SHA）：
  python .\tests\measure.py local .\samples\zh_text.txt --huff --runs 5 --csv .\results\local_zh_huff.csv

二、兩台電腦正式量測（A 是接收端；B 是傳送端）：
  A: python .\tests\measure.py recv 6123 .\measure_received --file zh_text.txt --runs 5 --env hotspot --csv .\results\recv_zh_huff.csv
  B: python .\tests\measure.py send <A的IP> 6123 .\samples\zh_text.txt --huff --runs 5 --env hotspot --csv .\results\send_zh_huff.csv

  A 先執行。兩端的 runs、檔案、模式與 env 標籤要相同。
  A 的 CSV 會記錄「收到檔案」的 SHA-256；B 的 CSV 會記錄原檔 SHA-256。
  兩者相同，才代表逐 byte 相同。

模式請二選一：--raw 或 --huff（若不寫，TextLink 預設是 --huff）。
--env 是量測環境標籤，例如 ethernet、wifi、hotspot。
"""

import argparse
import csv
import hashlib
import os
from pathlib import Path
import re
import subprocess
import sys
import time


FIELDS = [
    "timestamp", "role", "run", "environment", "file", "mode", "exit_code",
    "sha256", "file_bytes", "wire_bytes", "ratio", "encode_ms", "decode_ms",
    "send_ms", "total_ms", "wall_ms", "note",
]

STATS_RE = re.compile(r"STATS\s+(.*)")
PAIR_RE = re.compile(r"([A-Za-z_]+)=([^\s]+)")
SAVED_RE = re.compile(r"已存檔[：:]\s*([^（(\r\n]+)")


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def executable(value):
    path = Path(value)
    if path.exists():
        return str(path)
    # 預設從專案根目錄執行，所以 textlink.exe 在目前資料夾。
    return value


def mode_flag(args):
    return "--raw" if args.raw else "--huff"


def run_program(command, cwd):
    start = time.perf_counter()
    done = subprocess.run(command, cwd=cwd, capture_output=True, text=True,
                          encoding="utf-8", errors="replace")
    wall_ms = (time.perf_counter() - start) * 1000.0
    text = done.stdout + "\n" + done.stderr
    return done.returncode, text, wall_ms


def stats_from(text):
    """從 STATS 那一行取出 key=value；沒有 STATS 仍可留下原始量測列。"""
    match = STATS_RE.search(text)
    if not match:
        return {}
    return dict(PAIR_RE.findall(match.group(1)))


def append_row(csv_path, row):
    csv_path = Path(csv_path)
    csv_path.parent.mkdir(parents=True, exist_ok=True)
    new_file = not csv_path.exists()
    with open(csv_path, "a", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=FIELDS)
        if new_file:
            writer.writeheader()
        writer.writerow({key: row.get(key, "") for key in FIELDS})


def row_for(role, run_no, args, exit_code, output, wall_ms, digest, note=""):
    stats = stats_from(output)
    return {
        "timestamp": time.strftime("%Y-%m-%d %H:%M:%S"),
        "role": role,
        "run": run_no,
        "environment": args.env,
        "file": getattr(args, "file", ""),
        "mode": stats.get("mode", "raw" if args.raw else "huff"),
        "exit_code": exit_code,
        "sha256": digest,
        "file_bytes": stats.get("file_bytes", ""),
        "wire_bytes": stats.get("wire_bytes", ""),
        "ratio": stats.get("ratio", ""),
        "encode_ms": stats.get("encode_ms", ""),
        "decode_ms": stats.get("decode_ms", ""),
        "send_ms": stats.get("send_ms", ""),
        "total_ms": stats.get("total_ms", ""),
        "wall_ms": f"{wall_ms:.1f}",
        "note": note,
    }


def send_runs(args):
    source = Path(args.file)
    if not source.is_file():
        sys.exit(f"找不到傳送檔案：{source}")
    digest = sha256(source)
    exe = executable(args.exe)

    print(f"原檔 SHA-256：{digest}")
    for run_no in range(1, args.runs + 1):
        command = [exe, "send", args.ip, str(args.port), str(source), mode_flag(args)]
        print(f"[{run_no}/{args.runs}] 傳送中…")
        code, output, wall_ms = run_program(command, args.cwd)
        print(output.strip())
        append_row(args.csv, row_for("send", run_no, args, code, output, wall_ms, digest))
    print(f"完成。CSV：{args.csv}")


def recv_runs(args):
    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)
    exe = executable(args.exe)

    for run_no in range(1, args.runs + 1):
        print(f"[{run_no}/{args.runs}] 正在監聽 {args.port}，等待 B 傳送…")
        command = [exe, "recv", str(args.port), str(outdir)]
        code, output, wall_ms = run_program(command, args.cwd)
        print(output.strip())

        received = outdir / args.file
        digest = sha256(received) if code == 0 and received.is_file() else ""
        note = "" if digest else "未找到完整接收檔或接收程式失敗"
        append_row(args.csv, row_for("recv", run_no, args, code, output, wall_ms, digest, note))
    print(f"完成。CSV：{args.csv}")


def local_runs(args):
    source = Path(args.file)
    if not source.is_file():
        sys.exit(f"找不到傳送檔案：{source}")
    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)
    exe = executable(args.exe)
    source_hash = sha256(source)

    for run_no in range(1, args.runs + 1):
        # 先背景開接收端，再由同一台送端連到 127.0.0.1。
        recv_cmd = [exe, "recv", str(args.port), str(outdir)]
        receiver = subprocess.Popen(recv_cmd, cwd=args.cwd, stdout=subprocess.PIPE,
                                    stderr=subprocess.PIPE, text=True, encoding="utf-8",
                                    errors="replace")
        time.sleep(0.5)
        send_cmd = [exe, "send", "127.0.0.1", str(args.port), str(source), mode_flag(args)]
        code_s, output_s, wall_ms = run_program(send_cmd, args.cwd)
        try:
            output_r, error_r = receiver.communicate(timeout=20)
            code_r = receiver.returncode
        except subprocess.TimeoutExpired:
            receiver.kill()
            output_r, error_r = receiver.communicate()
            code_r = -1
        output_r = output_r + "\n" + error_r

        received = outdir / source.name
        received_hash = sha256(received) if received.is_file() else ""
        same = bool(received_hash) and source_hash == received_hash
        note = "SHA256 相同" if same else "SHA256 不同或未收到檔案"
        print(f"[{run_no}/{args.runs}] send={code_s}, recv={code_r}, {note}")
        append_row(args.csv, row_for("local-send", run_no, args, code_s, output_s,
                                     wall_ms, source_hash, note))
        append_row(args.csv, row_for("local-recv", run_no, args, code_r, output_r,
                                     wall_ms, received_hash, note))
    print(f"完成。CSV：{args.csv}")


def add_common(parser):
    parser.add_argument("--runs", type=int, default=5, help="重複次數，預設 5")
    parser.add_argument("--env", default="unspecified", help="量測環境，例如 ethernet、wifi")
    parser.add_argument("--csv", required=True, help="輸出 CSV 路徑")
    parser.add_argument("--exe", default=".\\textlink.exe", help="textlink.exe 路徑")
    parser.add_argument("--cwd", default=".", help="專案根目錄（通常不用改）")


def add_mode(parser):
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--raw", action="store_true", help="不壓縮")
    group.add_argument("--huff", action="store_true", help="Huffman 壓縮")


def main():
    parser = argparse.ArgumentParser(description="TextLink 量測工具")
    sub = parser.add_subparsers(dest="command", required=True)

    send = sub.add_parser("send", help="兩台電腦時的傳送端")
    send.add_argument("ip")
    send.add_argument("port", type=int)
    send.add_argument("file")
    add_mode(send); add_common(send)
    send.set_defaults(func=send_runs)

    recv = sub.add_parser("recv", help="兩台電腦時的接收端")
    recv.add_argument("port", type=int)
    recv.add_argument("outdir")
    recv.add_argument("--file", required=True, help="預期收到的檔名，例如 zh_text.txt")
    add_mode(recv); add_common(recv)
    recv.set_defaults(func=recv_runs)

    local = sub.add_parser("local", help="同一台電腦自動收送與比對")
    local.add_argument("file")
    local.add_argument("--port", type=int, default=6123)
    local.add_argument("--outdir", default="measure_received")
    add_mode(local); add_common(local)
    local.set_defaults(func=local_runs)

    args = parser.parse_args()
    if args.runs < 1:
        sys.exit("--runs 必須至少是 1")
    args.func(args)


if __name__ == "__main__":
    main()
