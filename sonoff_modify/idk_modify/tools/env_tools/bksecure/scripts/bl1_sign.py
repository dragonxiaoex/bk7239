#!/usr/bin/env python3

import os
import json
import logging
from .common import *
from .genbl1 import *

def bl1_sign_hash(privkey_pem_file, manifest_hash, outfile):
    script_dir = get_script_dir()
    sh_sec_tools = f'{script_dir}/../tools/sh_sec_tools/secure_boot_tool'

    digest_file = '_bl1_manifest_digest.txt'
    with open(digest_file, 'w') as f:
        f.write(manifest_hash)

    cmd = f'{sh_sec_tools} sign -k {privkey_pem_file} -d {digest_file}'
    run_cmd(cmd)

    signature_dict = {
        'bl1_sig_s': '',
        'bl1_sig_r': '',
    }

    with open('bl1_signature.txt', 'r') as f:
        signature_dict['bl1_sig_s'] = f.readline().strip();
        signature_dict['bl1_sig_r'] = f.readline();

    with open(outfile, 'w') as f:
        json.dump(signature_dict, f, indent=4)

def gen_manifest_bin(action_type, manifest_json_file, outfile):
    script_dir = get_script_dir()
    sh_sec_tools = f'{script_dir}/../tools/sh_sec_tools/secure_boot_tool'
    pwd = os.getcwd()
    logging.debug(f'generate manifest.bin')

    if (action_type == 'hash'):
        logging.debug(f'generate manifest hash only')
        cmd = f'{sh_sec_tools} manifest_digest -k key_desc.json -m {manifest_json_file} -o {pwd}/'
    elif (action_type == 'sign_from_sig'):
        logging.debug(f'generate manifest.bin from signature')
        cmd = f'{sh_sec_tools} gen_manifest_with_signature -k key_desc.json -m {manifest_json_file} -o {pwd}/'
    elif (action_type == 'sign'):
        logging.debug(f'generate manifest.bin from private key')
        cmd = f'{sh_sec_tools} -k key_desc.json -m {manifest_json_file} -o {pwd}/'
    else:
        logging.error(f'invalid bl1_sign action {action_type}')
        exit(1)

    run_cmd(cmd)
    cmd = f'cp manifest.bin {outfile}'
    run_cmd(cmd)

    if (action_type == 'hash'):
        data = {"hash":""}
        with open('bl1_manifest_digest.txt', 'r') as f:
            h = f.read()
            data['hash'] = h
            if (manifest_json_file == 'secondary_manifest.json'):
                with open('secondary_manifest_hash.json', 'w') as hf:
                    json.dump(data, hf, indent=4)
            else:
                with open('primary_manifest_hash.json', 'w') as hf:
                    json.dump(data, hf, indent=4)

        os.remove('bl1_manifest_digest.txt')

def bl1_sign(action_type, key_type, privkey_pem_file, pubkey_pem_file, signature, bin_file, static_addr, load_addr, outfile):
    # sonoff modify start
    # 支持由 AWS KMS 签署 BL1 manifest 摘要，避免在本地保存签名私钥。
    if action_type == 'sign' and str(privkey_pem_file).endswith('.json'):
        import sys
        from pathlib import Path
        from cryptography.hazmat.primitives import serialization
        from cryptography.hazmat.primitives.asymmetric.utils import decode_dss_signature, encode_dss_signature

        sys.path.insert(0, str(Path(get_script_dir()).parent / 'tools/mcuboot_tools'))
        from imgtool import keys

        kms_key = keys.load(privkey_pem_file)
        public_key = serialization.load_pem_public_key(Path(pubkey_pem_file).read_bytes())
        public_der = public_key.public_bytes(serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo)
        if key_type != 'ec256' or public_der != kms_key.get_public_bytes():
            raise ValueError('BL1 public key does not match the KMS signer')
        bl1_sign('hash', key_type, '', pubkey_pem_file, None,
                 bin_file, static_addr, load_addr, outfile)
        hash_file = 'secondary_manifest_hash.json' if outfile == 'secondary_manifest.bin' else 'primary_manifest_hash.json'
        digest = bytes.fromhex(json.loads(Path(hash_file).read_text())['hash'])
        r, s = decode_dss_signature(kms_key.sign_digest(digest))
        # This secure_boot_tool reads r first, then s (despite the legacy steps.py labels).
        Path('bl1_signature.txt').write_text(f'{r:064x}\n{s:064x}\n', encoding='ascii')
        bl1_sign('sign_from_sig', key_type, '', pubkey_pem_file, None,
                 bin_file, static_addr, load_addr, outfile)
        manifest = Path(outfile).read_bytes()
        r = int.from_bytes(manifest[-64:-32], 'big')
        s = int.from_bytes(manifest[-32:], 'big')
        kms_key.verify(encode_dss_signature(r, s), manifest[:-64])
        return
    # sonoff modify end
    if outfile == "secondary_manifest.bin" :
        manifest_json_file = 'secondary_manifest.json'
    else :
        manifest_json_file = 'primary_manifest.json'
    logging.debug(f"bl1 sign bin_file = {bin_file} static_addr {static_addr}")
    g = Genbl1(action_type, True, key_type, privkey_pem_file, pubkey_pem_file, outfile)
    g.gen_key_desc()
    g.gen_manifest(5, static_addr, load_addr, bin_file, manifest_json_file)
    gen_manifest_bin(action_type, manifest_json_file, outfile)
