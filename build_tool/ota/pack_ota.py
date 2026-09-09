#!/usr/bin/env python3
"""读取固件内置OTA密钥头文件，封装带AES-256-GCM加密的OTA包。"""

import argparse
import os
from pathlib import Path
import re
import secrets
import struct
import tempfile
import zlib

from cryptography.hazmat.primitives.ciphers.aead import AESGCM

KEY_SIZE = 32
IV_SIZE = 12
TAG_SIZE = 16
HEAD_SIZE = 24
FILE_SIZE = 76
STRUCT_VERSION = 1


def read_key(path):
    """读取ota_aes_key数组中的32个十六进制字节，不修改头文件或输出密钥。"""
    source = re.sub(r"/\*.*?\*/|//[^\n]*", "", Path(path).read_text(), flags=re.DOTALL)
    arrays = re.findall(r"\bstatic\s+const\s+uint8_t\s+ota_aes_key\s*\[\s*32\s*\]"
                        r"\s*=\s*\{([^{}]*)\}\s*;", source)
    if len(arrays) != 1:
        raise ValueError("OTA key header must define one static const uint8_t ota_aes_key[32] array")
    values = [value.strip() for value in arrays[0].split(",")]
    if not values[-1]:
        values.pop()
    if len(values) != KEY_SIZE or any(re.fullmatch(r"0[xX][0-9a-fA-F]{2}", value) is None for value in values):
        raise ValueError("OTA key array must contain exactly 32 hexadecimal bytes")
    return bytes(int(value, 16) for value in values)


def write_file(path, data):
    """同目录临时文件替换，失败时不留下不完整输出。"""
    path = Path(path)
    with tempfile.NamedTemporaryFile(dir=path.parent, prefix=path.name + ".", delete=False) as stream:
        temporary = Path(stream.name)
        try:
            stream.write(data)
            stream.close()
            os.replace(temporary, path)
        finally:
            temporary.unlink(missing_ok=True)


def encode_string(value, size):
    encoded = value.encode("ascii")
    if not encoded or len(encoded) > size or any(byte < 0x20 or byte > 0x7e for byte in encoded):
        raise ValueError(f"Field must contain 1 to {size} printable ASCII characters")
    return encoded.ljust(size, b"\0")


def pack_files(files, model_version, key):
    """files为(name, version, plaintext)列表；key为None时生成未加密封装。"""
    if not 1 <= len(files) <= 255:
        raise ValueError("OTA package must contain 1 to 255 files")
    if len({name for name, _, _ in files}) != len(files):
        raise ValueError("Duplicate OTA file name")
    if key is not None and len(key) != KEY_SIZE:
        raise ValueError("OTA key must contain exactly 32 bytes")
    cipher_type = 0 if key is None else 1
    head = bytes([STRUCT_VERSION, len(files)]) + encode_string(model_version, 8)
    head += bytes([cipher_type]) + bytes(9)
    head += struct.pack(">I", zlib.crc32(head))
    offset = HEAD_SIZE + FILE_SIZE * len(files)
    entries = []
    contents = []
    for name, version, plain in files:
        if not plain:
            raise ValueError("Empty OTA file")
        size = len(plain) + (IV_SIZE + TAG_SIZE if cipher_type else 0)
        if offset + size > 0xffffffff:
            raise ValueError("OTA package exceeds 32-bit length")
        entry = encode_string(name, 32) + encode_string(version, 16)
        # 与文档CRC32一致：反射多项式0xEDB88320，初值和最终异或均为0xFFFFFFFF。
        entry += struct.pack(">III", offset, size, zlib.crc32(plain))
        entry += struct.pack(">I", zlib.crc32(entry)) + bytes(12)
        if key is None:
            content = plain
        else:
            iv = secrets.token_bytes(IV_SIZE)
            # 认证外层头和当前文件属性，包含明文CRC、长度、偏移和加密类型。
            content = iv + AESGCM(key).encrypt(iv, plain, head + entry)
        entries.append(entry)
        contents.append(content)
        offset += size
    return head + b"".join(entries) + b"".join(contents)


def read_version(header):
    match = re.search(r'^\s*#define\s+SONOFF_SOFTWARE_VERSION_STRING\s+"([^"]+)"',
                      Path(header).read_text(), re.MULTILINE)
    if match is None:
        raise ValueError("SONOFF_SOFTWARE_VERSION_STRING not found")
    return match.group(1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    pack = commands.add_parser("pack", help="对原始文件加密并生成公司外层OTA包")
    pack.add_argument("--input", type=Path, required=True)
    pack.add_argument("--output", type=Path, required=True)
    pack.add_argument("--key-header", type=Path,
                      default=Path(__file__).resolve().parents[2] / "sonoff/ota/sonoff_ota_key.h",
                      help="固件内置OTA密钥头文件，默认读取项目中的sonoff_ota_key.h")
    pack.add_argument("--cipher", choices=["aes-256-gcm", "none"], default="aes-256-gcm")
    version = pack.add_mutually_exclusive_group(required=True)
    version.add_argument("--version")
    version.add_argument("--version-header", type=Path)
    try:
        args = parser.parse_args()
        source = args.input
        if source.resolve() == args.output.resolve():
            raise ValueError("Output must differ from the input file")
        key = None if args.cipher == "none" else read_key(args.key_header)
        version = args.version if args.version is not None else read_version(args.version_header)
        plain = source.read_bytes()
        output = pack_files([("ota.bin", version, plain)], version, key)
        write_file(args.output, output)
        print(f"OTA package created: {args.output} ({len(output)} bytes, {args.cipher}, input: {source.name})")
    except (OSError, ValueError) as error:
        parser.exit(1, f"OTA packaging failed: {error}\n")


if __name__ == "__main__":
    main()
