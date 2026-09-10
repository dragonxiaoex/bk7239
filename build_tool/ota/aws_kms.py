"""ECDSA P-256 signing through AWS CLI; no local private key is required."""

import base64
import hashlib
import json
import shutil
import subprocess
import tempfile
from pathlib import Path

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, utils

from imgtool.keys.ecdsa import ECDSA256P1Public


class AwsKmsKey(ECDSA256P1Public):
    def __init__(self, config_path):
        config_path = Path(config_path).resolve()
        config = json.loads(config_path.read_text(encoding='utf-8'))
        if config.get('provider') != 'aws-kms':
            raise ValueError('Expected an aws-kms signer configuration')
        self.key_arn = config['key_arn']
        self.region = config['region']
        self.profile = config['profile']
        arn = self.key_arn.split(':', 5)
        if (len(arn) != 6 or arn[0] != 'arn' or arn[2] != 'kms'
                or arn[3] != self.region or not arn[5].startswith('key/')):
            raise ValueError('KMS signing requires a key ARN matching the region')
        if not self.profile:
            raise ValueError('AWS profile must be specified')
        public_path = config_path.parent / config['public_key']
        key = serialization.load_pem_public_key(public_path.read_bytes())
        if not isinstance(key, ec.EllipticCurvePublicKey) or not isinstance(key.curve, ec.SECP256R1):
            raise ValueError('KMS signing requires an ECDSA P-256 public key')
        super().__init__(key)
        self.aws = shutil.which('aws') or str(Path.home() / '.local/bin/aws')

    def sign_digest(self, digest):
        if len(digest) != 32:
            raise ValueError('ECDSA_SHA_256 requires a 32-byte digest')
        with tempfile.TemporaryDirectory(prefix='sonoff-kms-') as temp_dir:
            digest_path = Path(temp_dir) / 'digest.bin'
            digest_path.write_bytes(digest)
            result = subprocess.run([
                self.aws, 'kms', 'sign', '--key-id', self.key_arn,
                '--profile', self.profile, '--region', self.region,
                '--signing-algorithm', 'ECDSA_SHA_256', '--message-type', 'DIGEST',
                '--message', 'fileb://' + str(digest_path),
                '--output', 'json', '--no-cli-pager',
                '--cli-connect-timeout', '10', '--cli-read-timeout', '60',
            ], capture_output=True, text=True, timeout=120)
        if result.returncode:
            raise RuntimeError('AWS KMS Sign failed: ' + result.stderr.strip())
        response = json.loads(result.stdout)
        if response['KeyId'] != self.key_arn or response['SigningAlgorithm'] != 'ECDSA_SHA_256':
            raise ValueError('KMS returned an unexpected key or signing algorithm')
        signature = base64.b64decode(response['Signature'], validate=True)
        # Verify every returned signature with the pinned local public key.
        self.key.verify(signature, digest, ec.ECDSA(utils.Prehashed(hashes.SHA256())))
        return signature

    def sign(self, payload):
        return self.sign_digest(hashlib.sha256(payload).digest())
