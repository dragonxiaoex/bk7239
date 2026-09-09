"""OTA流解析和核心写入回归：python3 tests/ota/test_ota.py。"""
import os
import importlib.util
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
import zlib

from cryptography.hazmat.primitives.ciphers.aead import AESGCM

ROOT = Path(__file__).resolve().parents[2]
FLASH_OFFSET = 0x264000
FLASH_SIZE = 1492 * 1024
TEST_KEY = bytes(range(32))
PACK_SCRIPT = ROOT / "build_tool/ota/pack_ota.py"
PACK_SPEC = importlib.util.spec_from_file_location("ota_pack", PACK_SCRIPT)
ota_pack = importlib.util.module_from_spec(PACK_SPEC)
PACK_SPEC.loader.exec_module(ota_pack)


def bk_package(payload, gap=b""):
    image = struct.pack("<8I", len(payload), 64 + len(gap), FLASH_OFFSET,
                        zlib.crc32(payload) ^ 0xffffffff, 0, 0x10000, 0, 0)
    tail = struct.pack("<IHHIII", 0, 32, 1, 0, 0, 0) + image
    return b"BK723658" + struct.pack("<I", zlib.crc32(tail) ^ 0xffffffff) + tail + gap + payload


def rbl_package(payload):
    head = b"RBL\0" + struct.pack("<II", 0, 0)
    head += b"app".ljust(16, b"\0") + b"1.0.0".ljust(24, b"\0") + bytes(24)
    head += struct.pack("<IIII", zlib.crc32(payload), 0, len(payload), len(payload))
    return head + struct.pack("<I", zlib.crc32(head)) + payload


def wrapped_package(files, gaps=None):
    gaps = gaps or [b""] * len(files)
    head = bytes([ota_pack.STRUCT_VERSION, len(files)]) + b"1.0.0".ljust(8, b"\0") + bytes(10)
    head += struct.pack(">I", zlib.crc32(head))
    offset = 24 + 76 * len(files)
    entries = []
    bodies = []
    for (name, content), gap in zip(files, gaps):
        offset += len(gap)
        entry = name.ljust(32, b"\0") + b"V1.0".ljust(16, b"\0")
        entry += struct.pack(">III", offset, len(content), zlib.crc32(content))
        entries.append(entry + struct.pack(">I", zlib.crc32(entry)) + bytes(12))
        bodies.append(gap + content)
        offset += len(content)
    return head + b"".join(entries) + b"".join(bodies)


def repair_metadata(data, index=None):
    data = bytearray(data)
    start, length = (0, 20) if index is None else (24 + index * 76, 60)
    struct.pack_into(">I", data, start + length, zlib.crc32(data[start:start + length]))
    return bytes(data)


class OtaTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="sonoff-ota-test-")
        cls.work = Path(cls.temp.name)
        cls.exe = cls.work / "ota_test"
        sdk = ROOT / "bk_openthread/bk_idk/components/ota"
        crypto = ROOT / "bk_openthread/bk_idk/components/psa_mbedtls/mbedtls"
        cls.key_header = cls.work / "sonoff_ota_key.h"
        cls.key_header.write_text("#include <stdint.h>\nstatic const uint8_t ota_aes_key[32] =\n{\n    "
                                  + ", ".join(f"0x{byte:02x}" for byte in TEST_KEY) + ",\n};\n")
        flags = ["gcc", "-std=c99", "-Wall", "-Werror", "-g", "-fsanitize=undefined",
                 "-fno-sanitize-recover=all", "-I" + str(cls.work),
                 "-I" + str(ROOT / "tests/ota/stubs"),
                 "-I" + str(ROOT / "sonoff/utils"), "-I" + str(crypto / "include"),
                 '-DMBEDTLS_CONFIG_FILE="ota_mbedtls_config.h"',
                 "-I" + str(ROOT / "sonoff/ota"), "-I" + str(sdk / "include")]
        subprocess.run(flags + ["-Wextra", "-Wconversion", "-Wshadow", "-c",
                               str(ROOT / "sonoff/ota/sonoff_ota_parse.c"), "-o",
                               str(cls.work / "parse.o")], check=True)
        subprocess.run(flags + ["-I" + str(sdk / "include/bk_private"), "-c",
                               str(sdk / "CheckSumUtils.c"), "-o", str(cls.work / "crc.o")], check=True)
        objects = [str(cls.work / "parse.o"), str(cls.work / "crc.o")]
        sources = [ROOT / "sonoff/utils/sonoff_crc32.c", ROOT / "sonoff/utils/sonoff_aes_gcm.c"]
        sources += [crypto / "library" / name for name in
                    ["aes.c", "gcm.c", "cipher.c", "cipher_wrap.c", "platform_util.c", "constant_time.c"]]
        for source in sources:
            obj = cls.work / (source.stem + ".o")
            subprocess.run(flags + ["-c", str(source), "-o", str(obj)], check=True)
            objects.append(str(obj))
        subprocess.run(flags + [str(ROOT / "tests/ota/ota_core_test.c")] + objects + [
                               "-o", str(cls.exe)], check=True)
        cls.payload = bytes(range(256)) * 33 + b"tail"

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def run_package(self, package, expected=None, chunk=1024, name="ota.bin", total=None, failure="", raw=False):
        source = self.work / "package.bin"
        output = self.work / "written.bin"
        source.write_bytes(package)
        result = subprocess.run([str(self.exe), str(source), str(output), str(chunk), name,
                                 str(len(package) if total is None else total), "raw" if raw else "package"], capture_output=True,
                                env=dict(os.environ, OTA_TEST_FAIL=failure))
        self.assertNotIn(b"runtime error", result.stderr)
        if expected is None:
            self.assertNotEqual(result.returncode, 0, result.stderr.decode())
            self.assertIn(b"apply=0", result.stderr)
        else:
            self.assertEqual(result.returncode, 0, result.stderr.decode())
            self.assertEqual(output.read_bytes(), expected)
            self.assertIn(b"apply=1", result.stderr)
        return result

    def test_all_header_split_positions(self):
        wrapped = wrapped_package([(b"ota.bin", self.payload)])
        for size in range(1, 178):
            with self.subTest(chunk=size):
                self.run_package(wrapped, self.payload, chunk=size)
        self.run_package(wrapped, self.payload, chunk=len(wrapped))

    def test_raw_input_and_package_format(self):
        for image in [self.payload, bk_package(self.payload, bytes(13)), rbl_package(self.payload)]:
            self.run_package(image)
            for chunk in [1, 7, 24, 64, 4096, len(image)]:
                self.run_package(image, image, chunk=chunk, raw=True)
        self.run_package(bytes(FLASH_SIZE + 1), raw=True)
        self.assertEqual((self.work / "written.bin").read_bytes(), b"")

    def test_select_multiple_files_and_skip_gaps(self):
        image = self.payload
        for index in range(3):
            files = [(b"config", bytes(33)), (b"radio", bytes(61))]
            files.insert(index, (b"ota.bin", image))
            package = wrapped_package(files, [bytes(7), bytes(21), bytes(5)])
            for chunk in [1, 23, 76, 255, 1024, len(package)]:
                self.run_package(package, self.payload, chunk=chunk)

    def test_exact_width_name_and_version(self):
        name = "a" * 32
        package = bytearray(wrapped_package([(name.encode(), self.payload)]))
        package[56:72] = b"0123456789abcdef"
        self.run_package(repair_metadata(package, 0), self.payload, name=name)

    def test_fixed_width_fields_preserve_bytes(self):
        package = bytearray(wrapped_package([(b"ota.bin", self.payload)]))
        package[2:10] = b"\x01\0\x80abcde"
        package[24:56] = b"ota.bin\0" + bytes(range(1, 25))
        package[56:72] = b"\xff\0" + bytes(range(1, 15))
        package = repair_metadata(repair_metadata(package), 0)
        for chunk in [1, 1024]:
            self.run_package(package, self.payload, chunk=chunk)

    def test_crc_errors(self):
        package = wrapped_package([(b"ota.bin", self.payload)])
        for pos in [2, 20, 24, 56, 72, 80, 84, 108, 140, 180, len(package) - 1]:
            data = bytearray(package)
            data[pos] ^= 1
            self.run_package(data)

    def test_reserved_fields(self):
        package = bytearray(wrapped_package([(b"ota.bin", self.payload)]))
        package[11:20] = bytes(range(1, 10))
        self.run_package(package)
        package = bytearray(repair_metadata(package))
        package[88:100] = bytes(range(0xa0, 0xac))
        for chunk in [1, 19, 63, 99, 1024]:
            self.run_package(package, self.payload, chunk=chunk)

    def test_bad_metadata(self):
        original = wrapped_package([(b"ota.bin", self.payload)])
        for pos, value in [(0, 2), (0, 3), (1, 0), (1, 255), (2, 0)]:
            data = bytearray(original)
            data[pos] = value
            self.run_package(repair_metadata(data))
        for position, value in [(72, 0), (72, 99), (72, 0xffffffff),
                                (76, 0), (76, 63), (76, 0xffffffff)]:
            data = bytearray(original)
            struct.pack_into(">I", data, position, value)
            self.run_package(repair_metadata(data, 0))
        for position in [24, 56]:
            data = bytearray(original)
            data[position] = 0
            self.run_package(repair_metadata(data, 0))

    def test_missing_duplicate_and_overlapping_files(self):
        image = self.payload
        self.run_package(wrapped_package([(b"ota.bin", image)]), name="")
        self.assertEqual((self.work / "written.bin").read_bytes(), b"")
        self.run_package(wrapped_package([(b"different", image)]))
        self.run_package(wrapped_package([(b"ota.bin", image), (b"ota.bin", image)]))
        package = bytearray(wrapped_package([(b"ota.bin", image), (b"config", b"abc")]))
        struct.pack_into(">I", package, 24 + 76 + 48, 176)
        self.run_package(repair_metadata(package, 1))

    def test_internal_image_bytes_are_opaque(self):
        bk = bytearray(bk_package(self.payload, bytes(17)))
        bk[16:64] = bytes(48)
        rbl = bytearray(rbl_package(self.payload))
        rbl[4:96] = bytes(92)
        images = [b"\0", b"BK723658", b"RBL\0", b"unknown!" + self.payload,
                  bk_package(self.payload, bytes(17)), rbl_package(self.payload), bytes(bk), bytes(rbl)]
        for image in images:
            for key in [None, TEST_KEY]:
                package = ota_pack.pack_files([("ota.bin", "1.0.0", image)], "1.0.0", key)
                for chunk in [1, 1024]:
                    self.run_package(package, image, chunk=chunk)

    def test_truncation_and_restart(self):
        package = wrapped_package([(b"ota.bin", self.payload)])
        for length in list(range(1, 177)) + [len(package) - 1]:
            self.run_package(package[:length], total=len(package))
        self.run_package(package, self.payload)

    def test_deterministic_random_chunk_sizes(self):
        package = wrapped_package([(b"ota.bin", self.payload)])
        for chunk in [193, 509, 1021, 2053, 4093]:
            self.run_package(package, self.payload, chunk=chunk)

    def test_flash_failures_do_not_apply(self):
        package = wrapped_package([(b"ota.bin", self.payload)])
        for failure in ["erase", "write"]:
            self.run_package(package, failure=failure)

    def test_existing_build_artifact(self):
        artifact = ROOT / "build/secure/bk7239n/onoff_plug/package/ota_raw.bin"
        if not artifact.exists():
            self.skipTest("当前工作区没有已生成的安全镜像ota_raw.bin")
        image = artifact.read_bytes()
        self.run_package(wrapped_package([(b"ota.bin", image)]), image)
        self.run_package(ota_pack.pack_files([("ota.bin", "1.0.0", image)], "1.0.0", TEST_KEY), image)

    def test_encrypted_header_splits(self):
        plain = bk_package(self.payload, bytes(17))
        package = ota_pack.pack_files([("ota.bin", "1.0.0", plain)], "1.0.0", TEST_KEY)
        self.assertEqual(package[10], 1)
        self.assertEqual(struct.unpack_from(">I", package, 80)[0], zlib.crc32(plain))
        for chunk in list(range(1, 180)) + [511, 512, 513, len(package)]:
            self.run_package(package, plain, chunk=chunk)

    def test_encrypted_multiple_files(self):
        for index in range(3):
            files = [("config", "1.0.0", bytes(35)), ("radio", "2.0.0", bytes(67))]
            files.insert(index, ("ota.bin", "3.0.0", self.payload))
            package = ota_pack.pack_files(files, "4.0.0", TEST_KEY)
            for chunk in [1, 79, 511, len(package)]:
                self.run_package(package, self.payload, chunk=chunk)

    def test_encrypted_tamper_and_wrong_key(self):
        plain = self.payload
        package = ota_pack.pack_files([("ota.bin", "1.0.0", plain)], "1.0.0", TEST_KEY)
        for position in [100, 111, 112, 176, len(package) - 17, len(package) - 16, len(package) - 1]:
            changed = bytearray(package)
            changed[position] ^= 0x80
            self.run_package(changed)
        changed = bytearray(package)
        changed[11] = 0x42
        self.run_package(repair_metadata(changed))
        changed = bytearray(package)
        changed[88] = 0x42
        self.run_package(changed)
        self.run_package(ota_pack.pack_files([("ota.bin", "1.0.0", plain)], "1.0.0", bytes(32)))
        for value in [0, 2, 255]:
            changed = bytearray(package)
            changed[10] = value
            self.run_package(repair_metadata(changed))

    def test_encrypted_plaintext_crc_required(self):
        plain = self.payload
        package = bytearray(ota_pack.pack_files([("ota.bin", "1.0.0", plain)], "1.0.0", TEST_KEY))
        # 生成认证有效但明文CRC错误的包，确认认证通过仍不能代替CRC校验。
        struct.pack_into(">I", package, 80, zlib.crc32(plain) ^ 1)
        package = bytearray(repair_metadata(package, 0))
        iv = bytes(package[100:112])
        package[112:] = AESGCM(TEST_KEY).encrypt(iv, plain, bytes(package[:100]))
        result = self.run_package(package)
        self.assertIn(b"error=7 ", result.stderr)

    def test_encrypted_truncation_and_flash_failure(self):
        package = ota_pack.pack_files([("ota.bin", "1.0.0", self.payload)], "1.0.0", TEST_KEY)
        for length in [99, 100, 101, 111, 112, 113, 175, len(package) - 17,
                       len(package) - 16, len(package) - 1]:
            self.run_package(package[:length], total=len(package))
        for failure in ["erase", "write"]:
            self.run_package(package, failure=failure)

    def test_fixed_key_header_and_random_iv(self):
        original = self.key_header.read_bytes()
        timestamp = self.key_header.stat().st_mtime_ns
        key = ota_pack.read_key(self.key_header)
        self.assertEqual(key, TEST_KEY)
        files = [("ota.bin", "1.0.0", self.payload)]
        first_pack = ota_pack.pack_files(files, "1.0.0", key)
        second_pack = ota_pack.pack_files(files, "1.0.0", ota_pack.read_key(self.key_header))
        self.assertNotEqual(first_pack[100:112], second_pack[100:112])
        self.assertEqual(self.key_header.read_bytes(), original)
        self.assertEqual(self.key_header.stat().st_mtime_ns, timestamp)

    def test_key_header_validation(self):
        source = self.key_header.read_text()
        header = self.work / "key_validation.h"
        header.write_text(source.replace("0x00,", "/* 0xff */ 0x00, // 0xaa\n"))
        self.assertEqual(ota_pack.read_key(header), TEST_KEY)
        invalid = [source.replace("0x00, ", ""), source.replace("0x00,", "0x00, 0x00,"),
                   source.replace("[32]", "[31]"), source.replace("0x00", "0x100"),
                   source.replace("0x00", "INVALID_KEY_VALUE"),
                   source.replace("ota_aes_key", "other_key"), source + source]
        for changed in invalid:
            header.write_text(changed)
            with self.assertRaises(ValueError):
                ota_pack.read_key(header)
        header.unlink()
        with self.assertRaises(FileNotFoundError):
            ota_pack.read_key(header)
        self.assertFalse(header.exists())

    def test_pack_command_uses_key_header(self):
        source = self.work / "cli_input.bin"
        output = self.work / "cli_output.bin"
        source.write_bytes(self.payload)
        original = self.key_header.read_bytes()
        timestamp = self.key_header.stat().st_mtime_ns
        command = ["python3", str(PACK_SCRIPT), "pack", "--input", str(source),
                   "--output", str(output), "--version", "1.0.0", "--key-header", str(self.key_header)]
        result = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn(TEST_KEY.hex(), result.stdout + result.stderr)
        self.assertNotIn("0x00", result.stdout + result.stderr)
        self.run_package(output.read_bytes(), self.payload)
        self.assertEqual(self.key_header.read_bytes(), original)
        self.assertEqual(self.key_header.stat().st_mtime_ns, timestamp)
        output.unlink()
        command[-1] = str(self.work / "missing_key.h")
        result = subprocess.run(command, capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(output.exists())
        self.assertFalse(Path(command[-1]).exists())

    def test_document_crc_vector(self):
        self.assertEqual(zlib.crc32(b"123456789"), 0xcbf43926)
        self.assertEqual(zlib.crc32(bytes.fromhex("41 50 50 31 88 33 02 4d")), 0x0794f7de)

    def test_rbl_plain_and_encrypted_splits(self):
        image = rbl_package(self.payload)
        packages = [wrapped_package([(b"ota.bin", image)]),
                    ota_pack.pack_files([("ota.bin", "1.0.0", image)], "1.0.0", TEST_KEY)]
        for packet in packages:
            for chunk in list(range(1, 125)) + [195, 196, 197, 511, 512, 513, len(packet)]:
                self.run_package(packet, image, chunk=chunk)

    def test_image_capacity(self):
        for key in [None, TEST_KEY]:
            for size in [1, 63, 64, FLASH_SIZE]:
                image = bytes(size)
                packet = ota_pack.pack_files([("ota.bin", "1.0.0", image)], "1.0.0", key)
                self.run_package(packet, image)
            packet = ota_pack.pack_files([("ota.bin", "1.0.0", bytes(FLASH_SIZE + 1))], "1.0.0", key)
            self.run_package(packet)
            self.assertEqual((self.work / "written.bin").read_bytes(), b"")
        self.run_package(wrapped_package([(b"ota.bin", b"")]))
        packet = ota_pack.pack_files([("ota.bin", "1.0.0", self.payload)], "1.0.0", TEST_KEY)
        for size in [0, 12, 28]:
            changed = bytearray(packet)
            struct.pack_into(">I", changed, 76, size)
            self.run_package(repair_metadata(changed, 0))
            self.assertEqual((self.work / "written.bin").read_bytes(), b"")

    def test_authentication_failure_does_not_apply(self):
        image = rbl_package(self.payload)
        packet = ota_pack.pack_files([("ota.bin", "1.0.0", image)], "1.0.0", TEST_KEY)
        for position in [112, 208, len(packet) - 17, len(packet) - 1]:
            changed = bytearray(packet)
            changed[position] ^= 1
            self.run_package(changed)
        for length in [107, 111, 112, 195, 207, len(packet) - 16, len(packet) - 1]:
            self.run_package(packet[:length], total=len(packet))
        self.run_package(packet, failure="apply")
        for index in range(3):
            files = [("radio", "1.0.0", bytes(37)), ("config", "1.0.0", bytes(71))]
            files.insert(index, ("ota.bin", "1.0.0", image))
            self.run_package(ota_pack.pack_files(files, "1.0.0", TEST_KEY), image, chunk=13)

    def test_existing_rbl_artifact(self):
        artifact = ROOT / "build/bk7239n/onoff_plug/package/app_pack.rbl"
        if not artifact.exists():
            self.skipTest("当前工作区没有已生成的app_pack.rbl")
        image = artifact.read_bytes()
        self.run_package(wrapped_package([(b"ota.bin", image)]), image)
        self.run_package(ota_pack.pack_files([("ota.bin", "1.0.0", image)], "1.0.0", TEST_KEY), image)

    def test_build_mode_selects_config_and_keeps_original_outputs(self):
        for secure in [False, True]:
            build_dir = self.work / ("secure_build" if secure else "normal_build")
            package_dir = build_dir / "bk7239n/onoff_plug/package"
            package_dir.mkdir(parents=True)
            bk = package_dir / "ota.bin"
            raw = package_dir / "ota_raw.bin"
            rbl = package_dir / "app_pack.rbl"
            bk_image = bk_package(self.payload)
            rbl_image = rbl_package(self.payload)
            bk.write_bytes(bk_image)
            raw.write_bytes(self.payload)
            rbl.write_bytes(rbl_image)
            command = ["make", "--no-print-directory", "-C", str(ROOT / "build_tool"),
                       "MODEL=onoff_plug", "BUILD_DIR=" + str(build_dir),
                       "OTA_KEY_HEADER=" + str(self.key_header)]
            if secure:
                command.append("SE=1")
            result = subprocess.run(command + ["prepare_project", "ota_package"],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            config_dir = build_dir / "project_tree/onoff_plug/config/bk7239n"
            config = (config_dir / "config").read_text()
            self.assertIn("CONFIG_SECURITY_FIRMWARE=" + ("y" if secure else "n"), config)
            self.assertEqual((config_dir / "security.csv").exists(), secure)
            self.assertEqual((config_dir / "auto_partitions.csv").exists(), not secure)
            output = package_dir / "ota_encrypted.bin"
            self.run_package(output.read_bytes(), self.payload if secure else rbl_image)
            self.assertEqual(bk.read_bytes(), bk_image)
            self.assertEqual(raw.read_bytes(), self.payload)
            self.assertEqual(rbl.read_bytes(), rbl_image)
            output.unlink()
            (raw if secure else rbl).unlink()
            result = subprocess.run(command + ["ota_package"], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main(verbosity=2)
