from pathlib import Path
from datetime import date
import csv
import argparse
from cryptography.hazmat.backends import default_backend
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import ec

parser = argparse.ArgumentParser(description='Generate the BL2 trusted public key from security.csv')
parser.add_argument('--replace', action='store_true', help='Replace the existing trusted public key header')
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
with (root / 'build_tool/config/bk7239n/security.csv').open() as config:
    settings = dict(list(csv.reader(config))[1:])
source = Path(settings['img_sign_pubkey'])
target = root / 'sonoff_modify/idk_modify/components/bk_mcuboot/bl2/components/mcuboot/src/sonoff_trusted_pubkey.h'
key = serialization.load_pem_public_key(source.read_bytes(), backend=default_backend())
if not isinstance(key, ec.EllipticCurvePublicKey) or not isinstance(key.curve, ec.SECP256R1):
    raise ValueError('Expected an ECDSA P-256 public key')
data = key.public_bytes(serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo)
today = date.today()
lines = [
    '/* sonoff modify start */',
    '/**',
    ' * @file    sonoff_trusted_pubkey.h',
    ' * @brief   BL2 固定发布公钥',
    ' *',
    ' * @author  yifei wang (yifei.wang@itead.cc)',
    f' * @date    {today.isoformat()}',
    ' *',
    f' * @copyright Copyright (c) {today.year}  深圳松诺技术有限公司',
    ' *',
    ' */',
    '#ifndef __SONOFF_TRUSTED_PUBKEY_H__',
    '#define __SONOFF_TRUSTED_PUBKEY_H__',
    '',
    '#include <stdint.h>',
    '',
    'static const uint8_t TRUSTED_PUBKEY_DER[] =',
    '{',
]
for offset in range(0, len(data), 12):
    lines.append('    ' + ', '.join(f'0x{value:02x}' for value in data[offset:offset + 12]) + ',')
lines.extend([
    '};',
    '',
    '#endif /* #ifndef __SONOFF_TRUSTED_PUBKEY_H__ */',
    '/* sonoff modify end */',
    '',
])
target.parent.mkdir(parents=True, exist_ok=True)
with target.open('w' if args.replace else 'x', encoding='utf-8') as output:
    output.write('\n'.join(lines))
print(f'Generated {target}, public key DER length: {len(data)} bytes')
