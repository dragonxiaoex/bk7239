"""Offline checks for the KMS signer. Run with python3 build_tool/ota/test_aws_kms_sign.py."""

import base64
import hashlib
import importlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

from click.testing import CliRunner
from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, utils
from cryptography.hazmat.primitives.ciphers.aead import AESGCM


class KmsSigningTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        root = Path(__file__).resolve().parents[2]
        cls.root = root
        cls.scripts_dir = str(root / 'build_tool/ota')
        cls.stage = tempfile.TemporaryDirectory(prefix='sonoff-kms-tests-')
        relative = Path('tools/env_tools/bksecure/tools/mcuboot_tools/imgtool')
        target = Path(cls.stage.name) / 'imgtool'
        shutil.copytree(root / 'bk_openthread/bk_idk' / relative, target,
                        ignore=shutil.ignore_patterns('__pycache__', 'aws_kms.py'))
        overlay = root / 'sonoff_modify/idk_modify' / relative
        for source in overlay.rglob('*.py'):
            shutil.copy2(source, target / source.relative_to(overlay))
        scripts = Path('tools/env_tools/bksecure/scripts')
        scripts_target = Path(cls.stage.name) / 'scripts'
        scripts_target.mkdir()
        (scripts_target / '__init__.py').touch()
        for name in ['common.py', 'genbl1.py', 'security.py', 'parse_csv.py']:
            shutil.copy2(root / 'bk_openthread/bk_idk' / scripts / name, scripts_target / name)
        shutil.copy2(root / 'sonoff_modify/idk_modify' / scripts / 'bl1_sign.py',
                     scripts_target / 'bl1_sign.py')
        sys.path[:0] = [cls.stage.name, cls.scripts_dir]
        from aws_kms import AwsKmsKey
        from imgtool import keys
        cls.signer_type = AwsKmsKey
        cls.load_key = staticmethod(keys.load)

    @classmethod
    def tearDownClass(cls):
        sys.path.remove(cls.stage.name)
        sys.path.remove(cls.scripts_dir)
        cls.stage.cleanup()

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='sonoff-kms-key-')
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        self.private = ec.generate_private_key(ec.SECP256R1())
        (self.directory / 'public.pem').write_bytes(self.private.public_key().public_bytes(
            serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo))
        self.config = {
            'provider': 'aws-kms', 'region': 'ap-southeast-2', 'profile': 'offline-test',
            'key_arn': 'arn:aws:kms:ap-southeast-2:123456789012:key/test-key',
            'public_key': 'public.pem',
        }
        self.config_path = self.directory / 'signer.json'
        self.config_path.write_text(json.dumps(self.config))
        self.signer = self.load_key(self.config_path)

    def response(self, signature):
        return subprocess.CompletedProcess([], 0, json.dumps({
            'KeyId': self.config['key_arn'], 'SigningAlgorithm': 'ECDSA_SHA_256',
            'Signature': base64.b64encode(signature).decode('ascii'),
        }), '')

    def sign_with_test_key(self, command, **kwargs):
        self.assertEqual(command[command.index('--message-type') + 1], 'DIGEST')
        self.assertEqual(command[command.index('--key-id') + 1], self.config['key_arn'])
        self.assertEqual(command[command.index('--profile') + 1], 'offline-test')
        path = command[command.index('--message') + 1].removeprefix('fileb://')
        digest = Path(path).read_bytes()
        self.assertEqual(len(digest), 32)
        return self.response(self.private.sign(digest, ec.ECDSA(utils.Prehashed(hashes.SHA256()))))

    def test_payload_is_hashed_once_and_verified(self):
        payload = b'firmware payload\x00\xff'

        with patch('aws_kms.subprocess.run', side_effect=self.sign_with_test_key):
            signature = self.signer.sign(payload)
        self.private.public_key().verify(signature, payload, ec.ECDSA(hashes.SHA256()))
        with self.assertRaises(InvalidSignature):
            self.private.public_key().verify(signature, payload + b'changed', ec.ECDSA(hashes.SHA256()))

    def test_aws_failure_does_not_produce_signature(self):
        failure = subprocess.CompletedProcess([], 255, '', 'AccessDeniedException')
        with patch('aws_kms.subprocess.run', return_value=failure):
            with self.assertRaisesRegex(RuntimeError, 'AccessDeniedException'):
                self.signer.sign(b'firmware')

    def test_signature_from_another_key_is_rejected(self):
        other = ec.generate_private_key(ec.SECP256R1())
        signature = other.sign(b'firmware', ec.ECDSA(hashes.SHA256()))
        with patch('aws_kms.subprocess.run', return_value=self.response(signature)):
            with self.assertRaises(InvalidSignature):
                self.signer.sign(b'firmware')

    def test_response_must_match_requested_key_and_algorithm(self):
        for field in ['KeyId', 'SigningAlgorithm']:
            with self.subTest(field=field):
                response = self.response(b'irrelevant')
                body = json.loads(response.stdout)
                body[field] = 'unexpected'
                response.stdout = json.dumps(body)
                with patch('aws_kms.subprocess.run', return_value=response):
                    with self.assertRaisesRegex(ValueError, 'unexpected key or signing algorithm'):
                        self.signer.sign(b'firmware')

    def test_wrong_digest_length_is_rejected_before_network(self):
        with patch('aws_kms.subprocess.run') as call:
            with self.assertRaises(ValueError):
                self.signer.sign_digest(b'too short')
            call.assert_not_called()

    def test_wrong_public_key_curve_is_rejected(self):
        key = ec.generate_private_key(ec.SECP384R1()).public_key()
        (self.directory / 'public.pem').write_bytes(key.public_bytes(
            serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo))
        with self.assertRaisesRegex(ValueError, 'P-256'):
            self.signer_type(self.config_path)

    def test_pem_key_loading_is_preserved(self):
        key = self.load_key(self.directory / 'public.pem')
        self.assertEqual(key.get_public_bytes(), self.signer.get_public_bytes())
        self.assertNotIsInstance(key, self.signer_type)

    def test_make_exports_signer_path_from_another_directory(self):
        probe = (
            'import sys; from imgtool import keys; import aws_kms; '
            'assert isinstance(keys.load(sys.argv[1]), aws_kms.AwsKmsKey); '
            'print(aws_kms.__file__)'
        )
        recipe = 'check_signer_import:\n\t@' + shlex.join(
            [sys.executable, '-c', probe, str(self.config_path)]) + '\n'
        environment = os.environ.copy()
        environment['PYTHONPATH'] = self.stage.name
        environment['PYTHONDONTWRITEBYTECODE'] = '1'
        for secure in ['0', '1']:
            with self.subTest(SECURE=secure):
                result = subprocess.run([
                    'make', '--no-print-directory', '-f', str(self.root / 'build_tool/Makefile'),
                    '-f', '-', 'MODEL=onoff_plug', f'SECURE={secure}', 'check_signer_import',
                ], input=recipe, cwd=self.directory, env=environment,
                    text=True, capture_output=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertEqual(Path(result.stdout.strip()), Path(self.scripts_dir) / 'aws_kms.py')

    def test_imgtool_signs_and_verifies_with_external_signer(self):
        from imgtool.main import imgtool

        source = self.directory / 'app.bin'
        output = self.directory / 'app_signed.bin'
        source.write_bytes(b'firmware payload\x00\xff' * 32)
        runner = CliRunner()
        with patch('aws_kms.subprocess.run', side_effect=self.sign_with_test_key):
            result = runner.invoke(imgtool, [
                'sign', '-k', str(self.config_path), '--align', '4', '--version', '1.2.3',
                '--header-size', '0x1000', '--pad-header', '--slot-size', '0x8000',
                str(source), str(output),
            ])
        self.assertEqual(result.exit_code, 0, result.output + str(result.exception))
        result = runner.invoke(imgtool, [
            'verify', '-k', str(self.directory / 'public.pem'), str(output),
        ])
        self.assertEqual(result.exit_code, 0, result.output + str(result.exception))

    def test_signing_and_ota_paths_after_project_relocation(self):
        relocated = self.directory / 'relocated-project'
        for directory in ['build_tool/ota', 'build_tool/sign', 'build_tool/config',
                          'build_tool/matter', 'build_tool/product', 'project/onoff_plug']:
            shutil.copytree(self.root / directory, relocated / directory,
                            ignore=shutil.ignore_patterns('__pycache__'))
        for name in ['Makefile', 'CMakeLists.txt']:
            shutil.copy2(self.root / 'build_tool' / name, relocated / 'build_tool' / name)

        signing = relocated / 'build_tool/sign'
        shutil.copy2(self.directory / 'public.pem', signing / 'aws_kms_public.pem')
        (signing / 'aws_kms_signer.json').write_text(json.dumps(
            dict(self.config, public_key='aws_kms_public.pem')))
        test_key = bytes(range(32))
        key_header = relocated / 'sonoff/ota/sonoff_ota_key.h'
        key_header.parent.mkdir(parents=True)
        key_header.write_text('static const uint8_t ota_aes_key[32] = {'
                              + ', '.join(f'0x{value:02x}' for value in test_key) + '};\n')

        matter = Path('bk_openthread/components/matter/connectedhomeip/src/controller/python/matter')
        shutil.copytree(self.root / matter / 'tlv', relocated / matter / 'tlv',
                        ignore=shutil.ignore_patterns('__pycache__'))
        shutil.copy2(self.root / matter / '__init__.py', relocated / matter / '__init__.py')
        environment = os.environ.copy()
        for name in ['PYTHONPATH', 'SONOFF_ROOT']:
            environment.pop(name, None)
        environment['PYTHONDONTWRITEBYTECODE'] = '1'

        def run(command):
            result = subprocess.run(command, cwd=self.directory, env=environment,
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

        run(['make', '--no-print-directory', '-f', str(relocated / 'build_tool/Makefile'),
             'MODEL=onoff_plug', 'SECURE=1', 'prepare_project'])
        prepared = relocated / 'build/secure/project_tree/onoff_plug/config/bk7239n'
        for name in ['aws_kms_public.pem', 'aws_kms_signer.json']:
            self.assertEqual((prepared / name).read_bytes(), (signing / name).read_bytes())

        spec = importlib.util.spec_from_file_location(
            'sdk_secure_pack', self.root / 'bk_openthread/bk_idk/tools/build_tools/secure_pack.py')
        sdk_pack = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(sdk_pack)
        from scripts.security import Security
        with CliRunner().isolated_filesystem(temp_dir=self.directory):
            sdk_pack.install_configs(str(prepared), str(Path.cwd()))
            security = Security('security.csv')
            self.assertEqual(security.img_sign_pubkey, 'aws_kms_public.pem')
            self.assertEqual(security.img_sign_privkey, 'aws_kms_signer.json')
            self.assertEqual(security.img_sign_pubkey_bytes, self.signer.get_public_bytes())
            self.assertEqual(self.load_key(security.img_sign_privkey).get_public_bytes(),
                             self.signer.get_public_bytes())

        run([sys.executable, str(relocated / 'build_tool/ota/convert_sign.py')])
        header = relocated / ('sonoff_modify/idk_modify/components/bk_mcuboot/bl2/'
                               'components/mcuboot/src/sonoff_trusted_pubkey.h')
        generated = bytes(int(value, 16) for value in re.findall(r'0x([0-9a-f]{2})', header.read_text()))
        self.assertEqual(generated, self.signer.get_public_bytes())

        payload = b'relocated firmware payload\x00\xff'
        (self.directory / 'payload.bin').write_bytes(payload)
        run([sys.executable, str(relocated / 'build_tool/ota/pack_ota.py'), 'pack',
             '--input', 'payload.bin', '--output', 'wrapped.bin', '--version', '1.2.3'])
        wrapped = (self.directory / 'wrapped.bin').read_bytes()
        self.assertEqual(AESGCM(test_key).decrypt(wrapped[100:112], wrapped[112:], wrapped[:100]), payload)
        matter_tool = str(relocated / 'build_tool/ota/pack_matter_ota.py')
        run([sys.executable, matter_tool, 'create', '-v', '0xDEAD', '-p', '0xBEEF',
             '-vn', '2', '-vs', '1.2.3', '-da', 'sha256', 'wrapped.bin', 'firmware.ota'])
        run([sys.executable, matter_tool, 'extract', 'firmware.ota', 'extracted.bin'])
        self.assertEqual((self.directory / 'extracted.bin').read_bytes(), wrapped)

    def test_bl1_manifests_use_external_signer(self):
        module = importlib.import_module('scripts.bl1_sign')
        sign_manifest = module.bl1_sign
        manifest_data = b'offline BL1 manifest' * 16

        def package_manifest(action, key_type, private, public, signature,
                             binary, static_address, load_address, output):
            if action == 'hash':
                hash_file = ('secondary_manifest_hash.json' if output == 'secondary_manifest.bin'
                             else 'primary_manifest_hash.json')
                Path(hash_file).write_text(json.dumps({'hash': hashlib.sha256(manifest_data).hexdigest()}))
            else:
                self.assertEqual(action, 'sign_from_sig')
                signature = b''.join(int(value, 16).to_bytes(32, 'big')
                                     for value in Path('bl1_signature.txt').read_text().splitlines())
                Path(output).write_bytes(manifest_data + signature)

        with CliRunner().isolated_filesystem(temp_dir=self.directory):
            with patch.object(module, 'bl1_sign', side_effect=package_manifest):
                with patch('aws_kms.subprocess.run', side_effect=self.sign_with_test_key):
                    for output in ['primary_manifest.bin', 'secondary_manifest.bin']:
                        with self.subTest(output=output):
                            sign_manifest('sign', 'ec256', str(self.config_path),
                                          str(self.directory / 'public.pem'), None,
                                          'bl2.bin', '0x02024000', '0x28040000', output)
                            self.assertEqual(Path(output).stat().st_size, len(manifest_data) + 64)


if __name__ == '__main__':
    unittest.main()
