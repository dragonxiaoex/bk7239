"""Offline checks for the KMS signer. Run with python3 build_tool/sign/test_aws_kms_sign.py."""

import base64
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, utils


class KmsSigningTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        root = Path(__file__).resolve().parents[2]
        cls.stage = tempfile.TemporaryDirectory(prefix='sonoff-kms-tests-')
        relative = Path('tools/env_tools/bksecure/tools/mcuboot_tools/imgtool')
        target = Path(cls.stage.name) / 'imgtool'
        shutil.copytree(root / 'bk_openthread/bk_idk' / relative, target,
                        ignore=shutil.ignore_patterns('__pycache__'))
        overlay = root / 'sonoff_modify/idk_modify' / relative
        for source in overlay.rglob('*.py'):
            shutil.copy2(source, target / source.relative_to(overlay))
        sys.path.insert(0, cls.stage.name)
        from imgtool.keys.aws_kms import AwsKmsKey
        cls.signer_type = AwsKmsKey

    @classmethod
    def tearDownClass(cls):
        sys.path.remove(cls.stage.name)
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
        self.signer = self.signer_type(self.config_path)

    def response(self, signature):
        return subprocess.CompletedProcess([], 0, json.dumps({
            'KeyId': self.config['key_arn'], 'SigningAlgorithm': 'ECDSA_SHA_256',
            'Signature': base64.b64encode(signature).decode('ascii'),
        }), '')

    def test_payload_is_hashed_once_and_verified(self):
        payload = b'firmware payload\x00\xff'

        def sign(command, **kwargs):
            self.assertEqual(command[command.index('--message-type') + 1], 'DIGEST')
            self.assertEqual(command[command.index('--key-id') + 1], self.config['key_arn'])
            self.assertEqual(command[command.index('--profile') + 1], 'offline-test')
            path = command[command.index('--message') + 1].removeprefix('fileb://')
            digest = Path(path).read_bytes()
            self.assertEqual(digest, hashlib.sha256(payload).digest())
            return self.response(self.private.sign(digest, ec.ECDSA(utils.Prehashed(hashes.SHA256()))))

        with patch('imgtool.keys.aws_kms.subprocess.run', side_effect=sign):
            signature = self.signer.sign(payload)
        self.private.public_key().verify(signature, payload, ec.ECDSA(hashes.SHA256()))
        with self.assertRaises(InvalidSignature):
            self.private.public_key().verify(signature, payload + b'changed', ec.ECDSA(hashes.SHA256()))

    def test_aws_failure_does_not_produce_signature(self):
        failure = subprocess.CompletedProcess([], 255, '', 'AccessDeniedException')
        with patch('imgtool.keys.aws_kms.subprocess.run', return_value=failure):
            with self.assertRaisesRegex(RuntimeError, 'AccessDeniedException'):
                self.signer.sign(b'firmware')

    def test_signature_from_another_key_is_rejected(self):
        other = ec.generate_private_key(ec.SECP256R1())
        signature = other.sign(b'firmware', ec.ECDSA(hashes.SHA256()))
        with patch('imgtool.keys.aws_kms.subprocess.run', return_value=self.response(signature)):
            with self.assertRaises(InvalidSignature):
                self.signer.sign(b'firmware')

    def test_response_must_match_requested_key_and_algorithm(self):
        for field in ['KeyId', 'SigningAlgorithm']:
            with self.subTest(field=field):
                response = self.response(b'irrelevant')
                body = json.loads(response.stdout)
                body[field] = 'unexpected'
                response.stdout = json.dumps(body)
                with patch('imgtool.keys.aws_kms.subprocess.run', return_value=response):
                    with self.assertRaisesRegex(ValueError, 'unexpected key or signing algorithm'):
                        self.signer.sign(b'firmware')

    def test_wrong_digest_length_is_rejected_before_network(self):
        with patch('imgtool.keys.aws_kms.subprocess.run') as call:
            with self.assertRaises(ValueError):
                self.signer.sign_digest(b'too short')
            call.assert_not_called()

    def test_wrong_public_key_curve_is_rejected(self):
        key = ec.generate_private_key(ec.SECP384R1()).public_key()
        (self.directory / 'public.pem').write_bytes(key.public_bytes(
            serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo))
        with self.assertRaisesRegex(ValueError, 'P-256'):
            self.signer_type(self.config_path)


if __name__ == '__main__':
    unittest.main()
