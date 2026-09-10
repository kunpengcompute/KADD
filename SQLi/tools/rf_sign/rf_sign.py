#!/usr/bin/env python3
# Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
"""为 .krf / .kcn 模型追加 / 校验 HMAC-SHA256 认证尾部。

[FIX REPORT-2026-0000028] 配套工具，支持两种原生模型格式：

    .krf  随机森林模型（kadd_rf_engine）
    .kcn  TextCNN 模型（kadd_cnn_engine）

背景
----
这两种格式原先都只有 CRC32（多项式 0xEDB88320，公开且无需密钥）作为完整性控制。
CRC32 只能检测意外损坏，无法抵抗蓄意篡改：持有模型文件写权限的攻击者可以改写
权重 / 叶节点概率后重算 CRC32，加载器会照单全收，从而绕过 SQLi 检测。

现在两种加载器都强制要求认证：

    header(64) || payload || HMAC-SHA256(32)

认证范围覆盖 header + payload，因此元数据也被密钥绑定。
未签名的旧格式模型一律拒绝加载，所以每次重新训练产出新模型之后，
都必须用本工具重新签名。

密钥
----
密钥为 32 字节，以 64 个十六进制字符提供，来源优先级：

    --key-hex  >  --key-file  >  环境变量 KADD_MODEL_HMAC_KEY  >  旧名 KADD_RF_HMAC_KEY

生产环境请自行生成密钥并妥善保管，例如：

    export KADD_MODEL_HMAC_KEY=$(openssl rand -hex 32)

密钥只经环境变量 / 受限权限文件注入运行时，绝不写入模型文件、也不入库。
仓库自带的 assets/model/model_tokens_rf.krf 与 model_tokens_dl_cnn.kcn
使用公开的开发密钥签名，仅用于构建与测试；生产部署必须换成自己的密钥
并重新签名，否则不具备安全性。

用法
----
    # 签名（写到新文件）
    python rf_sign.py model_tokens_rf.krf model_tokens_rf.signed.krf
    python rf_sign.py model_tokens_dl_cnn.kcn model_tokens_dl_cnn.signed.kcn

    # 原地签名
    python rf_sign.py --in-place model_tokens_rf.krf

    # 校验已签名模型
    python rf_sign.py --verify model_tokens_dl_cnn.kcn
"""

from __future__ import annotations

import argparse
import hashlib
import hmac
import os
import struct
import sys
import tempfile
from pathlib import Path

# ========== 通用常量 ==========

HEADER_SIZE = 64
HMAC_SIZE = 32
KEY_HEX_LEN = 64
FLAG_HMAC_SHA256 = 0x00000001

KEY_ENV = "KADD_MODEL_HMAC_KEY"
KEY_ENV_LEGACY = "KADD_RF_HMAC_KEY"

RF_MAGIC = b"KADDRF1\x00"
CNN_MAGIC = b"KADDCN1\x00"

# .kcn 固定结构参数（与 kadd_cnn_engine.c 保持一致）
CNN_KERNEL_WIDTHS = (2, 3, 4, 5)


class SignError(RuntimeError):
    """签名 / 校验过程中的可预期错误。"""


# ========== 密钥读取 ==========

def load_key(args: argparse.Namespace) -> bytes:
    """按 --key-hex > --key-file > 环境变量 的优先级取得 32 字节密钥。"""
    if args.key_hex is not None:
        raw = args.key_hex.strip()
        source = "--key-hex"
    elif args.key_file is not None:
        raw = Path(args.key_file).read_text(encoding="utf-8").strip()
        source = f"--key-file {args.key_file}"
    else:
        raw = os.environ.get(KEY_ENV)
        source = f"env {KEY_ENV}"
        if raw is None:
            raw = os.environ.get(KEY_ENV_LEGACY)
            source = f"env {KEY_ENV_LEGACY}"
        if raw is None:
            raise SignError(
                f"no key available: set {KEY_ENV}, or pass --key-hex / --key-file"
            )
        raw = raw.strip()

    if len(raw) != KEY_HEX_LEN:
        raise SignError(
            f"{source}: expected exactly {KEY_HEX_LEN} hex chars "
            f"(32 bytes), got {len(raw)}"
        )
    try:
        return bytes.fromhex(raw)
    except ValueError as exc:
        raise SignError(f"{source}: not valid hex: {exc}") from exc


# ========== 头部解析 ==========

def detect_format(blob: bytes) -> str:
    """根据 magic 自动识别 .krf / .kcn。"""
    if len(blob) < HEADER_SIZE:
        raise SignError(f"file smaller than {HEADER_SIZE}-byte header")
    if blob[:8] == RF_MAGIC:
        return "rf"
    if blob[:8] == CNN_MAGIC:
        return "cnn"
    raise SignError("bad magic: not a KADDRF1 / KADDCN1 model")


def parse_rf_header(blob: bytes) -> dict:
    """解析并校验 .krf 头部。"""
    version = struct.unpack_from("<I", blob, 8)[0]
    header_size = struct.unpack_from("<I", blob, 12)[0]
    node_size = struct.unpack_from("<I", blob, 16)[0]
    if version != 1:
        raise SignError(f"unsupported .krf version {version} (expected 1)")
    if header_size != HEADER_SIZE or node_size != 32:
        raise SignError(
            f"unexpected .krf header_size={header_size} / node_size={node_size}"
        )

    tree_count = struct.unpack_from("<I", blob, 32)[0]
    node_count = struct.unpack_from("<I", blob, 36)[0]
    flags = struct.unpack_from("<I", blob, 40)[0]
    payload_size = struct.unpack_from("<Q", blob, 44)[0]

    if (flags & ~FLAG_HMAC_SHA256) != 0:
        raise SignError(f"unknown .krf flags 0x{flags:08x}")

    expected = (tree_count + 1) * 4 + node_count * node_size
    if payload_size != expected:
        raise SignError(
            f"declared .krf payload {payload_size} != computed {expected} "
            f"(tree_count={tree_count}, node_count={node_count})"
        )

    return {
        "format": "rf",
        "magic": RF_MAGIC,
        "flags_offset": 40,
        "tree_count": tree_count,
        "node_count": node_count,
        "flags": flags,
        "payload_size": payload_size,
        "signed_region": HEADER_SIZE + payload_size,
    }


def parse_cnn_header(blob: bytes) -> dict:
    """解析并校验 .kcn 头部。"""
    version = struct.unpack_from("<I", blob, 8)[0]
    header_size = struct.unpack_from("<I", blob, 12)[0]
    sequence_length = struct.unpack_from("<I", blob, 16)[0]
    vocab_size = struct.unpack_from("<I", blob, 20)[0]
    embed_dim = struct.unpack_from("<I", blob, 24)[0]
    channels = struct.unpack_from("<I", blob, 28)[0]
    branches = struct.unpack_from("<I", blob, 32)[0]
    hidden = struct.unpack_from("<I", blob, 36)[0]
    class_count = struct.unpack_from("<I", blob, 40)[0]
    pad_id = struct.unpack_from("<I", blob, 44)[0]
    payload_size = struct.unpack_from("<Q", blob, 48)[0]
    flags = struct.unpack_from("<I", blob, 60)[0]

    if version != 1:
        raise SignError(f"unsupported .kcn version {version} (expected 1)")
    if header_size != HEADER_SIZE:
        raise SignError(f"unexpected .kcn header_size={header_size}")
    if sequence_length != 300 or vocab_size != 29 or embed_dim != 32:
        raise SignError("unexpected .kcn sequence/vocab/embedding dimensions")
    if channels != 64 or branches != 4 or hidden != 64 or class_count != 2:
        raise SignError("unexpected .kcn channels/branches/hidden/class dimensions")
    if pad_id != 28:
        raise SignError(f"unexpected .kcn pad_id {pad_id}")

    if (flags & ~FLAG_HMAC_SHA256) != 0:
        raise SignError(f"unknown .kcn flags 0x{flags:08x}")

    # 与 kadd_cnn_engine.c 的 expected_payload_float_count() 保持一致
    float_count = vocab_size * embed_dim
    for width in CNN_KERNEL_WIDTHS:
        float_count += channels * embed_dim * width
        float_count += channels
    float_count += hidden * (channels * branches)
    float_count += hidden
    float_count += class_count * hidden
    float_count += class_count
    expected = float_count * 4

    if payload_size != expected:
        raise SignError(
            f"declared .kcn payload {payload_size} != computed {expected} "
            f"(float_count={float_count})"
        )

    return {
        "format": "cnn",
        "magic": CNN_MAGIC,
        "flags_offset": 60,
        "sequence_length": sequence_length,
        "vocab_size": vocab_size,
        "embed_dim": embed_dim,
        "channels": channels,
        "branches": branches,
        "hidden": hidden,
        "class_count": class_count,
        "flags": flags,
        "payload_size": payload_size,
        "signed_region": HEADER_SIZE + payload_size,
    }


def parse_header(blob: bytes) -> dict:
    """自动识别格式并解析头部。"""
    fmt = detect_format(blob)
    if fmt == "rf":
        return parse_rf_header(blob)
    return parse_cnn_header(blob)


# ========== 签名 / 校验 ==========

def strip_trailer(blob: bytes, meta: dict) -> bytes:
    """返回不含 HMAC 尾部的 header||payload 部分，并校验总长度。"""
    signed_region = meta["signed_region"]
    authenticated = bool(meta["flags"] & FLAG_HMAC_SHA256)
    expected_size = signed_region + (HMAC_SIZE if authenticated else 0)
    if len(blob) != expected_size:
        raise SignError(
            f"size mismatch: file is {len(blob)} bytes, expected {expected_size} "
            f"(signed_region={signed_region}, authenticated={authenticated})"
        )
    return blob[:signed_region]


def compute_mac(body: bytes, key: bytes) -> bytes:
    """HMAC-SHA256(flags 已置位的 header || payload)。"""
    return hmac.new(key, body, hashlib.sha256).digest()


def sign(blob: bytes, key: bytes) -> bytes:
    meta = parse_header(blob)
    if meta["flags"] & FLAG_HMAC_SHA256:
        raise SignError(
            "model is already signed; refusing to re-sign. "
            "Sign the original unsigned model instead."
        )
    body = strip_trailer(blob, meta)

    out = bytearray(body)
    struct.pack_into("<I", out, meta["flags_offset"],
                     meta["flags"] | FLAG_HMAC_SHA256)
    mac = compute_mac(bytes(out), key)
    return bytes(out) + mac


def verify(blob: bytes, key: bytes) -> dict:
    meta = parse_header(blob)
    if not (meta["flags"] & FLAG_HMAC_SHA256):
        raise SignError(
            "model is NOT authenticated (flags bit0 = 0); "
            "the loader will reject it"
        )
    body = strip_trailer(blob, meta)
    expected = blob[meta["signed_region"]:]
    actual = compute_mac(body, key)
    return {"ok": hmac.compare_digest(actual, expected), "meta": meta}


def write_atomic(path: Path, data: bytes) -> None:
    """先写同目录临时文件再替换，避免中断留下半个模型。"""
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, tmp_name = tempfile.mkstemp(
        prefix=f".{path.name}.", suffix=".tmp", dir=str(path.parent)
    )
    try:
        with os.fdopen(fd, "wb") as fh:
            fh.write(data)
        os.replace(tmp_name, str(path))
    except BaseException:
        try:
            os.unlink(tmp_name)
        except OSError:
            pass
        raise


# ========== CLI ==========

def _format_summary(meta: dict) -> str:
    if meta["format"] == "rf":
        return (
            f"tree_count={meta['tree_count']} "
            f"node_count={meta['node_count']}"
        )
    return (
        f"seq={meta['sequence_length']} vocab={meta['vocab_size']} "
        f"embed={meta['embed_dim']} channels={meta['channels']} "
        f"branches={meta['branches']} hidden={meta['hidden']}"
    )


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Sign or verify the HMAC-SHA256 trailer of a .krf / .kcn model.",
    )
    parser.add_argument("input", help="input .krf or .kcn model")
    parser.add_argument(
        "output", nargs="?",
        help="output model (omit with --in-place or --verify)"
    )
    parser.add_argument("--key-hex", help=f"{KEY_HEX_LEN} hex chars (32 bytes)")
    parser.add_argument("--key-file", help="path to a file containing the hex key")
    parser.add_argument(
        "--in-place", action="store_true", help="overwrite the input file atomically"
    )
    parser.add_argument(
        "--verify", action="store_true", help="only verify, do not write anything"
    )
    args = parser.parse_args(argv)

    if args.verify and (args.output or args.in_place):
        parser.error("--verify cannot be combined with an output path or --in-place")
    if not args.verify and not args.in_place and not args.output:
        parser.error("an output path is required unless --in-place or --verify is given")

    try:
        key = load_key(args)
        blob = Path(args.input).read_bytes()
        fmt = detect_format(blob)

        if args.verify:
            result = verify(blob, key)
            meta = result["meta"]
            if not result["ok"]:
                print(
                    f"[model_sign] FAIL: HMAC mismatch for {args.input} "
                    f"- tampered, or wrong key",
                    file=sys.stderr,
                )
                return 1
            print(f"[model_sign] OK: {args.input} is authentic ({fmt})")
            print(f"[model_sign]   {_format_summary(meta)}")
            print(
                f"[model_sign]   signed_region={meta['signed_region']} bytes"
            )
            return 0

        signed = sign(blob, key)
        target = Path(args.input) if args.in_place else Path(args.output)
        write_atomic(target, signed)

        meta = parse_header(signed)
        print(f"[model_sign] signed {args.input} -> {target} ({fmt})")
        print(f"[model_sign]   {_format_summary(meta)}")
        print(
            f"[model_sign]   signed region : {len(signed) - HMAC_SIZE} bytes "
            f"(header {HEADER_SIZE} + payload)"
        )
        print(f"[model_sign]   output size   : {len(signed)} bytes (+{HMAC_SIZE} HMAC)")
        print(f"[model_sign]   flags         : 0x{meta['flags']:08x}")
        print(
            f"[model_sign]   remember to export {KEY_ENV} in the runtime environment"
        )
        return 0
    except SignError as exc:
        print(f"[model_sign] ERROR: {exc}", file=sys.stderr)
        return 1
    except OSError as exc:
        print(f"[model_sign] ERROR: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
