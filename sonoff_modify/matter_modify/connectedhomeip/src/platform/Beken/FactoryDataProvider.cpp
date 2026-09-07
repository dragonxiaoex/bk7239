/*
 *
 *    Copyright (c) 2022 Project CHIP Authors
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

#include <credentials/CHIPCert.h>
#include <crypto/CHIPCryptoPAL.h>
#include <crypto/DefaultSessionKeystore.h>
#include <lib/core/CHIPError.h>
#include <lib/support/Base64.h>
#include <lib/support/BytesToHex.h>
#include <lib/support/Span.h>
#include <platform/Beken/CHIPDevicePlatformConfig.h>
#include <platform/CHIPDeviceConfig.h>
#include <platform/CHIPDeviceLayer.h>
#include <platform/ConnectivityManager.h>
#include <platform/internal/GenericConfigurationManagerImpl.ipp>

#include <platform/Beken/BekenConfig.h>
#include <platform/Beken/FactoryDataProvider.h>
#include <matter_pal.h>

/* sonoff modify start */
#include "sonoff_nvdm_config.h"
#include "sonoff_project_config.h"
/* sonoff modify end */

using namespace ::chip::DeviceLayer::Internal;

namespace chip {
namespace DeviceLayer {

// TODO: This should be moved to a method of P256Keypair
CHIP_ERROR LoadKeypairFromRaw(ByteSpan private_key, ByteSpan public_key, Crypto::P256Keypair & keypair)
{
    Crypto::P256SerializedKeypair serialized_keypair;
    ReturnErrorOnFailure(serialized_keypair.SetLength(private_key.size() + public_key.size()));
    memcpy(serialized_keypair.Bytes(), public_key.data(), public_key.size());
    memcpy(serialized_keypair.Bytes() + public_key.size(), private_key.data(), private_key.size());
    return keypair.Deserialize(serialized_keypair);
}

static uint8_t bk_flash_read(bk_partition_t inPartition, uint32_t offset, uint8_t *value, uint32_t length)
{
    bk_logic_partition_t *partition_info =  bk_flash_partition_get_info((bk_partition_t)inPartition);//BK_PARTITION_MATTER_FACTORY
    uint32_t base_addr = partition_info->partition_start_addr;
    uint32_t total_length = partition_info->partition_length;
    if(offset + length > total_length) {
        return -1;
    }

    flash_protect_type_t protect_type;
    protect_type = bk_flash_get_protect_type();
    bk_flash_set_protect_type(FLASH_PROTECT_NONE);
    bk_flash_read_bytes(base_addr + offset,  value, length);
    bk_flash_set_protect_type(protect_type);
    return 0;
}

CHIP_ERROR FactoryDataProvider::ReadCertDataHeader()
{
    if(mCertDataHeader.magic_code != 0)
        return CHIP_NO_ERROR;
    VerifyOrReturnError(
            kNoErr == bk_flash_read(
                BK_PARTITION_MATTER_FACTORY,
                certDataHeaderOffset,
                (uint8_t*)&mCertDataHeader,
                8),
            CHIP_ERROR_READ_FAILED);
    uint32_t header_length = mCertDataHeader.header_length;
    if(header_length > sizeof(mCertDataHeader))
    {
        header_length = sizeof(mCertDataHeader);
    }
    if(mCertDataHeader.magic_code < 0xF5F50000)
    {
        mCertDataHeader.magic_code = 0;
        return CHIP_ERROR_READ_FAILED;
    }
    if(kNoErr != bk_flash_read(
                BK_PARTITION_MATTER_FACTORY,
                certDataHeaderOffset,
                (uint8_t*)&mCertDataHeader,
                header_length))
    {
        mCertDataHeader.magic_code = 0;
        return CHIP_ERROR_READ_FAILED;
    }
    return CHIP_NO_ERROR;
}
/* sonoff modify start */
#if 0
CHIP_ERROR FactoryDataProvider::ReadFlashDataHeader()
{
    if(mFlashDataHeader.magic_code != 0)
        return CHIP_NO_ERROR;
    VerifyOrReturnError(
            kNoErr == bk_flash_read(
                BK_PARTITION_MATTER_FACTORY,
                flashDataHeaderOffset,
                (uint8_t*)&mFlashDataHeader,
                8),
            CHIP_ERROR_READ_FAILED);
    uint32_t header_length = mFlashDataHeader.header_length;
    if(header_length > sizeof(mFlashDataHeader))
    {
        header_length = sizeof(mFlashDataHeader);
    }
    if(mFlashDataHeader.magic_code < 0xF5F50000)
    {
        mFlashDataHeader.magic_code = 0;
        return CHIP_ERROR_READ_FAILED;
    }
    if(kNoErr != bk_flash_read(
                BK_PARTITION_MATTER_FACTORY,
                flashDataHeaderOffset,
                (uint8_t*)&mFlashDataHeader,
                header_length))
    {
        mFlashDataHeader.magic_code = 0;
        return CHIP_ERROR_READ_FAILED;
    }
    return CHIP_NO_ERROR;
}
#endif
/* sonoff modify end */

CHIP_ERROR FactoryDataProvider::GetCertificationDeclaration(MutableByteSpan & outBuffer)
{
    /* sonoff modify start */
    uint8_t cd[NVDM_MATTER_CD_BIN_MAX_LEN];
    uint16_t cd_len = 0;

    VerifyOrReturnError(0 == snfMatterCDGet(cd, sizeof(cd), &cd_len), CHIP_ERROR_READ_FAILED);
    VerifyOrReturnError(outBuffer.size() >= cd_len, CHIP_ERROR_BUFFER_TOO_SMALL);
    memcpy(outBuffer.data(), cd, cd_len);
    outBuffer.reduce_size(cd_len);
    #if 0
    ReturnErrorOnFailure(ReadCertDataHeader());
    VerifyOrReturnError(outBuffer.size() >= mCertDataHeader.CertificationDeclaration.length, CHIP_ERROR_BUFFER_TOO_SMALL);
    VerifyOrReturnError(
            kNoErr == bk_flash_read(
                BK_PARTITION_MATTER_FACTORY,
                certDataHeaderOffset + mCertDataHeader.CertificationDeclaration.off_set + mCertDataHeader.header_length,
                outBuffer.data(),
                mCertDataHeader.CertificationDeclaration.length),
            CHIP_ERROR_READ_FAILED);
    outBuffer.reduce_size(mCertDataHeader.CertificationDeclaration.length);
    #endif
    /* sonoff modify end */
    return CHIP_NO_ERROR;
}

CHIP_ERROR FactoryDataProvider::GetFirmwareInformation(MutableByteSpan & out_firmware_info_buffer)
{
    // TODO: We need a real example FirmwareInformation to be populated.
    out_firmware_info_buffer.reduce_size(0);

    return CHIP_NO_ERROR;
}

CHIP_ERROR FactoryDataProvider::GetDeviceAttestationCert(MutableByteSpan & outBuffer)
{
    /* sonoff modify start */
    uint8_t dac_cert[NVDM_MATTER_DAC_CERT_BIN_MAX_LEN];
    uint16_t dac_cert_len = 0;

    VerifyOrReturnError(0 == snfMatterDacCertGet(dac_cert, sizeof(dac_cert), &dac_cert_len), CHIP_ERROR_READ_FAILED);
    VerifyOrReturnError(outBuffer.size() >= dac_cert_len, CHIP_ERROR_BUFFER_TOO_SMALL);
    memcpy(outBuffer.data(), dac_cert, dac_cert_len);
    outBuffer.reduce_size(dac_cert_len);
    #if 0
    ReturnErrorOnFailure(ReadCertDataHeader());
    VerifyOrReturnError(outBuffer.size() >= mCertDataHeader.DeviceAttestationCert.length, CHIP_ERROR_BUFFER_TOO_SMALL);
    VerifyOrReturnError(
            kNoErr == bk_flash_read(
                BK_PARTITION_MATTER_FACTORY,
                certDataHeaderOffset + mCertDataHeader.DeviceAttestationCert.off_set + mCertDataHeader.header_length,
                outBuffer.data(),
                mCertDataHeader.DeviceAttestationCert.length
                ),
            CHIP_ERROR_READ_FAILED);
    outBuffer.reduce_size(mCertDataHeader.DeviceAttestationCert.length);
    #endif
    /* sonoff modify end */
    return CHIP_NO_ERROR;
}

CHIP_ERROR FactoryDataProvider::GetProductAttestationIntermediateCert(MutableByteSpan & outBuffer)
{
    /* sonoff modify start */
    uint8_t pai_cert[NVDM_MATTER_PAI_CERT_BIN_MAX_LEN];
    uint16_t pai_cert_len = 0;

    VerifyOrReturnError(0 == snfMatterPaiCertGet(pai_cert, sizeof(pai_cert), &pai_cert_len), CHIP_ERROR_READ_FAILED);
    VerifyOrReturnError(outBuffer.size() >= pai_cert_len, CHIP_ERROR_BUFFER_TOO_SMALL);
    memcpy(outBuffer.data(), pai_cert, pai_cert_len);
    outBuffer.reduce_size(pai_cert_len);
#if 0
    ReturnErrorOnFailure(ReadCertDataHeader());
    VerifyOrReturnError(outBuffer.size() >= mCertDataHeader.ProductAttestationIntermediateCert.length, CHIP_ERROR_BUFFER_TOO_SMALL);
    VerifyOrReturnError(
            kNoErr == bk_flash_read(
                BK_PARTITION_MATTER_FACTORY,
                certDataHeaderOffset + mCertDataHeader.ProductAttestationIntermediateCert.off_set + mCertDataHeader.header_length,
                outBuffer.data(),
                mCertDataHeader.ProductAttestationIntermediateCert.length
                ),
            CHIP_ERROR_READ_FAILED);
    outBuffer.reduce_size(mCertDataHeader.ProductAttestationIntermediateCert.length);
#endif
    /* sonoff modify end */
    return CHIP_NO_ERROR;
}

CHIP_ERROR FactoryDataProvider::SignWithDeviceAttestationKey(const ByteSpan & messageToSign, MutableByteSpan & outSignBuffer)
{
    /* sonoff modify start */
    Crypto::P256ECDSASignature signature;
    Crypto::P256Keypair keypair;
    Crypto::P256PublicKey dacPublicKey;
    uint8_t dac_cert[NVDM_MATTER_DAC_CERT_BIN_MAX_LEN];
    uint8_t dac_key[NVDM_MATTER_DAC_KEY_BIN_LEN];
    uint16_t dac_cert_len = 0;
    uint16_t dac_key_len  = 0;

    VerifyOrReturnError(IsSpanUsable(outSignBuffer), CHIP_ERROR_INVALID_ARGUMENT);
    VerifyOrReturnError(IsSpanUsable(messageToSign), CHIP_ERROR_INVALID_ARGUMENT);
    VerifyOrReturnError(outSignBuffer.size() >= signature.Capacity(), CHIP_ERROR_BUFFER_TOO_SMALL);
    VerifyOrReturnError(0 == snfMatterDacCertGet(dac_cert, sizeof(dac_cert), &dac_cert_len), CHIP_ERROR_READ_FAILED);
    VerifyOrReturnError(0 == snfMatterDacKeyGet(dac_key, sizeof(dac_key), &dac_key_len), CHIP_ERROR_READ_FAILED);
    VerifyOrReturnError(dac_key_len == NVDM_MATTER_DAC_KEY_BIN_LEN, CHIP_ERROR_INTERNAL);
    ReturnErrorOnFailure(chip::Crypto::ExtractPubkeyFromX509Cert(ByteSpan(dac_cert, dac_cert_len), dacPublicKey));
    ReturnErrorOnFailure(LoadKeypairFromRaw(ByteSpan(dac_key, dac_key_len),
                                            ByteSpan(dacPublicKey.Bytes(), dacPublicKey.Length()), keypair));
    ReturnErrorOnFailure(keypair.ECDSA_sign_msg(messageToSign.data(), messageToSign.size(), signature));
    ReturnErrorOnFailure(CopySpanToMutableSpan(ByteSpan{ signature.ConstBytes(), signature.Length() }, outSignBuffer));
#if 0
#if CONFIG_BEKEN_DAC_PRIV_ENCRYPT
    size_t kDacPublicKeyLen = 0, kDacPrivateKeyLen = 0;
    uint8_t kEncryptDacPrivateKey[53] = { 0 };// AESNonceLen + 40,
    uint8_t kDacPublicKey[70] = { 0 }, kDacPrivateKey[40] = { 0 };
    ReturnErrorOnFailure(ReadCertDataHeader());
    VerifyOrReturnError((40 + AESNonceLen) >= mCertDataHeader.DacPrivateKey.length, CHIP_ERROR_BUFFER_TOO_SMALL);
    VerifyOrReturnError(70 >= mCertDataHeader.DacPublicKey.length, CHIP_ERROR_BUFFER_TOO_SMALL);
    VerifyOrReturnError(
            kNoErr == bk_flash_read(
                BK_PARTITION_MATTER_FACTORY,
                certDataHeaderOffset + mCertDataHeader.DacPrivateKey.off_set + mCertDataHeader.header_length,
                kEncryptDacPrivateKey,
                mCertDataHeader.DacPrivateKey.length
                ),
            CHIP_ERROR_READ_FAILED);
    kDacPrivateKeyLen = mCertDataHeader.DacPrivateKey.length - AESNonceLen;//the real DacPrivateKey length.
    VerifyOrReturnError(
            kNoErr == bk_flash_read(
                BK_PARTITION_MATTER_FACTORY,
                certDataHeaderOffset + mCertDataHeader.DacPublicKey.off_set + mCertDataHeader.header_length,
                kDacPublicKey,
                mCertDataHeader.DacPublicKey.length
                ),
            CHIP_ERROR_READ_FAILED);
    kDacPublicKeyLen = mCertDataHeader.DacPublicKey.length;

    Crypto::Aes128KeyHandle key;
    Crypto::DefaultSessionKeystore keystore;
    Crypto::Symmetric128BitsKeyByteArray keyMaterial;
    memcpy(&keyMaterial, AESKey, AESKeyLen);
    keystore.CreateKey(keyMaterial, key);
    ReturnErrorOnFailure(Crypto::AES_CTR_crypt(kEncryptDacPrivateKey+AESNonceLen, kDacPrivateKeyLen, key, kEncryptDacPrivateKey, AESNonceLen, kDacPrivateKey));
    keystore.DestroyKey(key);
#else
    size_t kDacPublicKeyLen = 0, kDacPrivateKeyLen = 0;
    uint8_t kEncryptDacPrivateKey[40] = { 0 }, kDacPublicKey[70] = { 0 }, kDacPrivateKey[40] = { 0 };
    ReturnErrorOnFailure(ReadCertDataHeader());
    VerifyOrReturnError(40 >= mCertDataHeader.DacPrivateKey.length, CHIP_ERROR_BUFFER_TOO_SMALL);
    VerifyOrReturnError(70 >= mCertDataHeader.DacPublicKey.length, CHIP_ERROR_BUFFER_TOO_SMALL);
    VerifyOrReturnError(
            kNoErr == bk_flash_read(
                BK_PARTITION_MATTER_FACTORY,
                certDataHeaderOffset + mCertDataHeader.DacPrivateKey.off_set + mCertDataHeader.header_length,
                kEncryptDacPrivateKey,
                mCertDataHeader.DacPrivateKey.length
                ),
            CHIP_ERROR_READ_FAILED);
    kDacPrivateKeyLen = mCertDataHeader.DacPrivateKey.length;
    VerifyOrReturnError(
            kNoErr == bk_flash_read(
                BK_PARTITION_MATTER_FACTORY,
                certDataHeaderOffset + mCertDataHeader.DacPublicKey.off_set + mCertDataHeader.header_length,
                kDacPublicKey,
                mCertDataHeader.DacPublicKey.length
                ),
            CHIP_ERROR_READ_FAILED);
    kDacPublicKeyLen = mCertDataHeader.DacPublicKey.length;

    memcpy(kDacPrivateKey, kEncryptDacPrivateKey, kDacPrivateKeyLen);
#endif
    VerifyOrReturnError(70 != kDacPublicKeyLen, CHIP_ERROR_BUFFER_TOO_SMALL);

    Crypto::P256ECDSASignature signature;
    Crypto::P256Keypair keypair;

    VerifyOrReturnError(IsSpanUsable(outSignBuffer), CHIP_ERROR_INVALID_ARGUMENT);
    VerifyOrReturnError(IsSpanUsable(messageToSign), CHIP_ERROR_INVALID_ARGUMENT);
    VerifyOrReturnError(outSignBuffer.size() >= signature.Capacity(), CHIP_ERROR_BUFFER_TOO_SMALL);

    // In a non-exemplary implementation, the public key is not needed here. It is used here merely because
    // Crypto::P256Keypair is only (currently) constructable from raw keys if both private/public keys are present.
    ReturnErrorOnFailure( LoadKeypairFromRaw(ByteSpan(kDacPrivateKey, kDacPrivateKeyLen), ByteSpan(kDacPublicKey, kDacPublicKeyLen), keypair));
    ReturnErrorOnFailure(keypair.ECDSA_sign_msg(messageToSign.data(), messageToSign.size(), signature));

    return CopySpanToMutableSpan(ByteSpan{ signature.ConstBytes(), signature.Length() }, outSignBuffer);
#endif
    /* sonoff modify end */
    return CHIP_NO_ERROR;
}

CHIP_ERROR FactoryDataProvider::GetSetupDiscriminator(uint16_t & setupDiscriminator)
{
    /* sonoff modify start */
    VerifyOrReturnError(0 == snfMatterDiscriminatorGet(&setupDiscriminator), CHIP_ERROR_READ_FAILED);
#if 0
    ReturnErrorOnFailure(ReadFlashDataHeader());
    VerifyOrReturnError(sizeof(setupDiscriminator) == mFlashDataHeader.SetupDiscriminator.length, CHIP_ERROR_BUFFER_TOO_SMALL);
    VerifyOrReturnError(
            kNoErr == bk_flash_read(
                BK_PARTITION_MATTER_FACTORY,
                flashDataHeaderOffset + mFlashDataHeader.SetupDiscriminator.off_set + mFlashDataHeader.header_length,
                (uint8_t *)&setupDiscriminator,
                mFlashDataHeader.SetupDiscriminator.length
                ),
            CHIP_ERROR_READ_FAILED);
#endif
    /* sonoff modify end */
    return CHIP_NO_ERROR;
}

CHIP_ERROR FactoryDataProvider::SetSetupDiscriminator(uint16_t setupDiscriminator)
{
    //ReturnErrorOnFailure(BekenConfig::WriteConfigValue(BekenConfig::kConfigKey_SetupDiscriminator, setupDiscriminator));
    return CHIP_ERROR_NOT_IMPLEMENTED;
}

CHIP_ERROR FactoryDataProvider::GetSpake2pIterationCount(uint32_t & iterationCount)
{
    /* sonoff modify start */
    VerifyOrReturnError(0 == snfMatterIterationCountGet(&iterationCount), CHIP_ERROR_READ_FAILED);
#if 0
    ReturnErrorOnFailure(ReadFlashDataHeader());
    VerifyOrReturnError(sizeof(iterationCount) == mFlashDataHeader.Spake2pIterationCount.length, CHIP_ERROR_BUFFER_TOO_SMALL);
    VerifyOrReturnError(
            kNoErr == bk_flash_read(
                BK_PARTITION_MATTER_FACTORY,
                flashDataHeaderOffset + mFlashDataHeader.Spake2pIterationCount.off_set + mFlashDataHeader.header_length,
                (uint8_t *)&iterationCount,
                mFlashDataHeader.Spake2pIterationCount.length
                ),
            CHIP_ERROR_READ_FAILED);
#endif
    /* sonoff modify end */
    return CHIP_NO_ERROR;
}

CHIP_ERROR FactoryDataProvider::GetSpake2pSalt(MutableByteSpan & saltBuf)
{
    /* sonoff modify start */
    char saltB64[NVDM_MATTER_SALT_STR_MAX_LEN + 1] = { 0 };
    size_t saltB64Len                              = 0;
    size_t saltLen                                 = 0;

    VerifyOrReturnError(0 == snfMatterSaltGet(saltB64, sizeof(saltB64)), CHIP_ERROR_READ_FAILED);
    saltB64Len = strlen(saltB64);
    saltLen    = chip::Base64Decode32(saltB64, saltB64Len, reinterpret_cast<uint8_t *>(saltB64));
    VerifyOrReturnError(saltLen != 0, CHIP_ERROR_READ_FAILED);
    VerifyOrReturnError(saltLen <= saltBuf.size(), CHIP_ERROR_BUFFER_TOO_SMALL);
    memcpy(saltBuf.data(), saltB64, saltLen);
    saltBuf.reduce_size(saltLen);
#if 0
    static constexpr size_t kSpake2pSalt_MaxBase64Len = BASE64_ENCODED_LEN(chip::Crypto::kSpake2p_Max_PBKDF_Salt_Length) + 1;

    char saltB64[kSpake2pSalt_MaxBase64Len] = { 0 };
    size_t saltB64Len                       = 0;

    ReturnErrorOnFailure(ReadFlashDataHeader());
    VerifyOrReturnError(kSpake2pSalt_MaxBase64Len >= mFlashDataHeader.Spake2pSalt.length, CHIP_ERROR_BUFFER_TOO_SMALL);
    VerifyOrReturnError(
            kNoErr == bk_flash_read(
                BK_PARTITION_MATTER_FACTORY,
                flashDataHeaderOffset + mFlashDataHeader.Spake2pSalt.off_set + mFlashDataHeader.header_length,
                (uint8_t *)saltB64,
                mFlashDataHeader.Spake2pSalt.length
                ),
            CHIP_ERROR_READ_FAILED);
    saltB64Len = mFlashDataHeader.Spake2pSalt.length;
    size_t saltLen = chip::Base64Decode32(saltB64, saltB64Len, reinterpret_cast<uint8_t *>(saltB64));
    VerifyOrReturnError(saltLen <= saltBuf.size(), CHIP_ERROR_BUFFER_TOO_SMALL);
    memcpy(saltBuf.data(), saltB64, saltLen);
    saltBuf.reduce_size(saltLen);
#endif
    /* sonoff modify end */
    return CHIP_NO_ERROR;
}

CHIP_ERROR FactoryDataProvider::GetSpake2pVerifier(MutableByteSpan & verifierBuf, size_t & verifierLen)
{
    /* sonoff modify start */
    char verifierB64[NVDM_MATTER_VERIFIER_STR_MAX_LEN + 1] = { 0 };
    size_t verifierB64Len                                  = 0;

    VerifyOrReturnError(0 == snfMatterVerifierGet(verifierB64, sizeof(verifierB64)), CHIP_ERROR_READ_FAILED);
    verifierB64Len = strlen(verifierB64);
    verifierLen    = chip::Base64Decode32(verifierB64, verifierB64Len, reinterpret_cast<uint8_t *>(verifierB64));
    VerifyOrReturnError(verifierLen != 0, CHIP_ERROR_READ_FAILED);
    VerifyOrReturnError(verifierLen <= verifierBuf.size(), CHIP_ERROR_BUFFER_TOO_SMALL);
    memcpy(verifierBuf.data(), verifierB64, verifierLen);
    verifierBuf.reduce_size(verifierLen);
#if 0
    static constexpr size_t kSpake2pSerializedVerifier_MaxBase64Len =
        BASE64_ENCODED_LEN(chip::Crypto::kSpake2p_VerifierSerialized_Length) + 1;

    char verifierB64[kSpake2pSerializedVerifier_MaxBase64Len] = { 0 };
    size_t verifierB64Len                                     = 0;

    ReturnErrorOnFailure(ReadFlashDataHeader());
    VerifyOrReturnError(kSpake2pSerializedVerifier_MaxBase64Len >= mFlashDataHeader.Spake2pVerifier.length, CHIP_ERROR_BUFFER_TOO_SMALL);
    VerifyOrReturnError(
            kNoErr == bk_flash_read(
                BK_PARTITION_MATTER_FACTORY,
                flashDataHeaderOffset + mFlashDataHeader.Spake2pVerifier.off_set + mFlashDataHeader.header_length,
                (uint8_t *)verifierB64,
                mFlashDataHeader.Spake2pVerifier.length
                ),
            CHIP_ERROR_READ_FAILED);
    verifierB64Len = mFlashDataHeader.Spake2pVerifier.length;
    verifierLen = chip::Base64Decode32(verifierB64, verifierB64Len, reinterpret_cast<uint8_t *>(verifierB64));
    VerifyOrReturnError(verifierLen <= verifierBuf.size(), CHIP_ERROR_BUFFER_TOO_SMALL);
    memcpy(verifierBuf.data(), verifierB64, verifierLen);
    verifierBuf.reduce_size(verifierLen);
#endif
    /* sonoff modify end */
    return CHIP_NO_ERROR;
}

CHIP_ERROR FactoryDataProvider::GetSetupPasscode(uint32_t & setupPasscode)
{
    /* sonoff modify start */
    VerifyOrReturnError(0 == snfMatterPasscodeGet(&setupPasscode), CHIP_ERROR_READ_FAILED);
#if 0
    ReturnErrorOnFailure(ReadFlashDataHeader());
    VerifyOrReturnError(sizeof(setupPasscode) == mFlashDataHeader.SetupPasscode.length, CHIP_ERROR_BUFFER_TOO_SMALL);
    VerifyOrReturnError(
            kNoErr == bk_flash_read(
                BK_PARTITION_MATTER_FACTORY,
                flashDataHeaderOffset + mFlashDataHeader.SetupPasscode.off_set + mFlashDataHeader.header_length,
                (uint8_t *)&setupPasscode,
                mFlashDataHeader.SetupPasscode.length
                ),
            CHIP_ERROR_READ_FAILED);
#endif
    /* sonoff modify end */
    return CHIP_NO_ERROR;
}

CHIP_ERROR FactoryDataProvider::SetSetupPasscode(uint32_t setupPasscode)
{
    return CHIP_ERROR_NOT_IMPLEMENTED;
}

CHIP_ERROR FactoryDataProvider::GetVendorName(char * buf, size_t bufSize)
{
    /* sonoff modify start */
    char vendorName[NVDM_MATTER_NAME_MAX_LEN + 1] = { 0 };
    size_t vendorNameLen                          = 0;

    VerifyOrReturnError(0 == snfMatterVendorNameGet(vendorName, sizeof(vendorName)), CHIP_ERROR_READ_FAILED);
    vendorNameLen = strlen(vendorName);
    VerifyOrReturnError(bufSize > vendorNameLen, CHIP_ERROR_BUFFER_TOO_SMALL);
    memcpy(buf, vendorName, vendorNameLen + 1);
#if 0
    ReturnErrorOnFailure(ReadFlashDataHeader());
    VerifyOrReturnError(bufSize >= mFlashDataHeader.VendorName.length, CHIP_ERROR_BUFFER_TOO_SMALL);
    VerifyOrReturnError(
            kNoErr == bk_flash_read(
                BK_PARTITION_MATTER_FACTORY,
                flashDataHeaderOffset + mFlashDataHeader.VendorName.off_set + mFlashDataHeader.header_length,
                (uint8_t*)buf,
                mFlashDataHeader.VendorName.length
                ),
            CHIP_ERROR_READ_FAILED);
#endif
    /* sonoff modify end */
    return CHIP_NO_ERROR;
}

CHIP_ERROR FactoryDataProvider::GetVendorId(uint16_t & vendorId)
{
    /* sonoff modify start */
    VerifyOrReturnError(0 == snfMatterVendorIdGet(&vendorId), CHIP_ERROR_READ_FAILED);
#if 0
    ReturnErrorOnFailure(ReadFlashDataHeader());
    VerifyOrReturnError(sizeof(vendorId) == mFlashDataHeader.VendorId.length, CHIP_ERROR_BUFFER_TOO_SMALL);
    VerifyOrReturnError(
            kNoErr == bk_flash_read(
                BK_PARTITION_MATTER_FACTORY,
                flashDataHeaderOffset + mFlashDataHeader.VendorId.off_set + mFlashDataHeader.header_length,
                (uint8_t*)&vendorId,
                mFlashDataHeader.VendorId.length
                ),
            CHIP_ERROR_READ_FAILED);
#endif
    /* sonoff modify end */
    return CHIP_NO_ERROR;
}

CHIP_ERROR FactoryDataProvider::GetProductName(char * buf, size_t bufSize)
{
    /* sonoff modify start */
    char productName[NVDM_MATTER_NAME_MAX_LEN + 1] = { 0 };
    size_t productNameLen                          = 0;

    VerifyOrReturnError(0 == snfMatterProductNameGet(productName, sizeof(productName)), CHIP_ERROR_READ_FAILED);
    productNameLen = strlen(productName);
    VerifyOrReturnError(bufSize > productNameLen, CHIP_ERROR_BUFFER_TOO_SMALL);
    memcpy(buf, productName, productNameLen + 1);
#if 0
    ReturnErrorOnFailure(ReadFlashDataHeader());
    VerifyOrReturnError(bufSize >= mFlashDataHeader.ProductName.length, CHIP_ERROR_BUFFER_TOO_SMALL);
    VerifyOrReturnError(
            kNoErr == bk_flash_read(
                BK_PARTITION_MATTER_FACTORY,
                flashDataHeaderOffset + mFlashDataHeader.ProductName.off_set + mFlashDataHeader.header_length,
                (uint8_t*)buf,
                mFlashDataHeader.ProductName.length
                ),
            CHIP_ERROR_READ_FAILED);
#endif
    /* sonoff modify end */
    return CHIP_NO_ERROR;
}

CHIP_ERROR FactoryDataProvider::GetProductId(uint16_t & productId)
{
    /* sonoff modify start */
    VerifyOrReturnError(0 == snfMatterProductIdGet(&productId), CHIP_ERROR_READ_FAILED);
#if 0
    ReturnErrorOnFailure(ReadFlashDataHeader());
    VerifyOrReturnError(sizeof(productId) == mFlashDataHeader.ProductId.length, CHIP_ERROR_BUFFER_TOO_SMALL);
    VerifyOrReturnError(
            kNoErr == bk_flash_read(
                BK_PARTITION_MATTER_FACTORY,
                flashDataHeaderOffset + mFlashDataHeader.ProductId.off_set + mFlashDataHeader.header_length,
                (uint8_t*)&productId,
                mFlashDataHeader.ProductId.length
                ),
            CHIP_ERROR_READ_FAILED);
#endif
    /* sonoff modify end */
    return CHIP_NO_ERROR;
}

CHIP_ERROR FactoryDataProvider::GetSerialNumber(char * buf, size_t bufSize)
{
    /* sonoff modify start */
    char serialNumber[NVDM_FACTORY_SERIAL_NUMBER_LEN + 1] = { 0 };
    size_t serialNumberLen                                = 0;

    VerifyOrReturnError(0 == snfSerialNumberGet(serialNumber, sizeof(serialNumber)), CHIP_ERROR_READ_FAILED);
    serialNumberLen = strlen(serialNumber);
    VerifyOrReturnError(bufSize > serialNumberLen, CHIP_ERROR_BUFFER_TOO_SMALL);
    memcpy(buf, serialNumber, serialNumberLen + 1);
#if 0
    ReturnErrorOnFailure(ReadFlashDataHeader());
    VerifyOrReturnError(bufSize >= mFlashDataHeader.SerialNumber.length, CHIP_ERROR_BUFFER_TOO_SMALL);
    VerifyOrReturnError(
            kNoErr == bk_flash_read(
                BK_PARTITION_MATTER_FACTORY,
                flashDataHeaderOffset + mFlashDataHeader.SerialNumber.off_set + mFlashDataHeader.header_length,
                (uint8_t*)buf,
                mFlashDataHeader.SerialNumber.length
                ),
            CHIP_ERROR_READ_FAILED);
#endif
    /* sonoff modify end */
    return CHIP_NO_ERROR;
}

CHIP_ERROR FactoryDataProvider::GetManufacturingDate(uint16_t & year, uint8_t & month, uint8_t & day)
{
    /* sonoff modify start */
    return CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE;
#if 0
    uint32_t date;
    ReturnErrorOnFailure(ReadFlashDataHeader());
    VerifyOrReturnError(sizeof(date) >= mFlashDataHeader.ManufacturingDate.length, CHIP_ERROR_BUFFER_TOO_SMALL);
    VerifyOrReturnError(
            kNoErr == bk_flash_read(
                BK_PARTITION_MATTER_FACTORY,
                flashDataHeaderOffset + mFlashDataHeader.ManufacturingDate.off_set + mFlashDataHeader.header_length,
                (uint8_t*)&date,
                mFlashDataHeader.ManufacturingDate.length
                ),
            CHIP_ERROR_READ_FAILED);
    year = (date >> 16) & 0xFFFF;
    month = (date >> 8) & 0xFF;
    day = date & 0xFF;
    return CHIP_NO_ERROR;
#endif
    /* sonoff modify end */
}

CHIP_ERROR FactoryDataProvider::GetHardwareVersion(uint16_t & hardwareVersion)
{
    /* sonoff modify start */
    hardwareVersion = SONOFF_MATTER_HARDWARE_VERSION;
#if 0
    ReturnErrorOnFailure(ReadFlashDataHeader());
    VerifyOrReturnError(sizeof(hardwareVersion) >= mFlashDataHeader.HardwareVersion.length, CHIP_ERROR_BUFFER_TOO_SMALL);
    VerifyOrReturnError(
            kNoErr == bk_flash_read(
                BK_PARTITION_MATTER_FACTORY,
                flashDataHeaderOffset + mFlashDataHeader.HardwareVersion.off_set + mFlashDataHeader.header_length,
                (uint8_t*)&hardwareVersion,
                mFlashDataHeader.HardwareVersion.length
                ),
            CHIP_ERROR_READ_FAILED);
#endif
    /* sonoff modify end */

    return CHIP_NO_ERROR;
}

CHIP_ERROR FactoryDataProvider::GetHardwareVersionString(char * buf, size_t bufSize)
{
    /* sonoff modify start */
    strncpy(buf, SONOFF_MATTER_HARDWARE_VERSION_STRING, bufSize);
#if 0
    ReturnErrorOnFailure(ReadFlashDataHeader());
    VerifyOrReturnError(bufSize >= mFlashDataHeader.HardwareVersionString.length, CHIP_ERROR_BUFFER_TOO_SMALL);
    VerifyOrReturnError(
            kNoErr == bk_flash_read(
                BK_PARTITION_MATTER_FACTORY,
                flashDataHeaderOffset + mFlashDataHeader.HardwareVersionString.off_set + mFlashDataHeader.header_length,
                (uint8_t*)buf,
                mFlashDataHeader.HardwareVersionString.length
                ),
            CHIP_ERROR_READ_FAILED);
#endif
    /* sonoff modify end */
    return CHIP_NO_ERROR;
}

CHIP_ERROR FactoryDataProvider::GetRotatingDeviceIdUniqueId(MutableByteSpan & uniqueIdSpan)
{
    /* sonoff modify start */
    char uniqueIdHex[NVDM_MATTER_RD_ID_UID_HEX_LEN + 1] = { 0 };
    size_t uniqueIdLen                                  = 0;

    VerifyOrReturnError(0 == snfMatterRdIdUidGet(uniqueIdHex, sizeof(uniqueIdHex)), CHIP_ERROR_READ_FAILED);
    uniqueIdLen = chip::Encoding::HexToBytes(uniqueIdHex, NVDM_MATTER_RD_ID_UID_HEX_LEN, uniqueIdSpan.data(), uniqueIdSpan.size());
    VerifyOrReturnError(uniqueIdLen == (NVDM_MATTER_RD_ID_UID_HEX_LEN / 2), CHIP_ERROR_READ_FAILED);
    uniqueIdSpan.reduce_size(uniqueIdLen);
#if 0
    ReturnErrorOnFailure(ReadFlashDataHeader());
    VerifyOrReturnError(uniqueIdSpan.size() >= mFlashDataHeader.RotatingDeviceIdUniqueId.length, CHIP_ERROR_BUFFER_TOO_SMALL);
    VerifyOrReturnError(
            kNoErr == bk_flash_read(
                BK_PARTITION_MATTER_FACTORY,
                flashDataHeaderOffset + mFlashDataHeader.RotatingDeviceIdUniqueId.off_set + mFlashDataHeader.header_length,
                uniqueIdSpan.data(),
                mFlashDataHeader.RotatingDeviceIdUniqueId.length
                ),
            CHIP_ERROR_READ_FAILED);
#endif
    /* sonoff modify end */
    return CHIP_NO_ERROR;
}

CHIP_ERROR FactoryDataProvider::GetPartNumber(char * buf, size_t bufSize)
{
    /* sonoff modify start */
    return CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE;
#if 0
    ReturnErrorOnFailure(ReadFlashDataHeader());
    VerifyOrReturnError(bufSize >= mFlashDataHeader.PartNumber.length, CHIP_ERROR_BUFFER_TOO_SMALL);
    VerifyOrReturnError(
            kNoErr == bk_flash_read(
                BK_PARTITION_MATTER_FACTORY,
                flashDataHeaderOffset + mFlashDataHeader.PartNumber.off_set + mFlashDataHeader.header_length,
                (uint8_t*)buf,
                mFlashDataHeader.PartNumber.length
                ),
            CHIP_ERROR_READ_FAILED);
    return CHIP_NO_ERROR;
#endif
    /* sonoff modify end */
};
CHIP_ERROR FactoryDataProvider::GetProductURL(char * buf, size_t bufSize)
{
    /* sonoff modify start */
    return CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE;
#if 0
    ReturnErrorOnFailure(ReadFlashDataHeader());
    VerifyOrReturnError(bufSize >= mFlashDataHeader.ProductURL.length, CHIP_ERROR_BUFFER_TOO_SMALL);
    VerifyOrReturnError(
            kNoErr == bk_flash_read(
                BK_PARTITION_MATTER_FACTORY,
                flashDataHeaderOffset + mFlashDataHeader.ProductURL.off_set + mFlashDataHeader.header_length,
                (uint8_t*)buf,
                mFlashDataHeader.ProductURL.length
                ),
            CHIP_ERROR_READ_FAILED);
    return CHIP_NO_ERROR;
#endif
    /* sonoff modify end */
};
CHIP_ERROR FactoryDataProvider::GetProductLabel(char * buf, size_t bufSize)
{
    /* sonoff modify start */
    char productName[NVDM_MATTER_NAME_MAX_LEN + 1] = { 0 };
    size_t productNameLen                          = 0;

    VerifyOrReturnError(0 == snfMatterProductNameGet(productName, sizeof(productName)), CHIP_ERROR_READ_FAILED);
    productNameLen = strlen(productName);
    VerifyOrReturnError(bufSize > productNameLen, CHIP_ERROR_BUFFER_TOO_SMALL);
    memcpy(buf, productName, productNameLen + 1);
#if 0
    ReturnErrorOnFailure(ReadFlashDataHeader());
    VerifyOrReturnError(bufSize >= mFlashDataHeader.ProductLabel.length, CHIP_ERROR_BUFFER_TOO_SMALL);
    VerifyOrReturnError(
            kNoErr == bk_flash_read(
                BK_PARTITION_MATTER_FACTORY,
                flashDataHeaderOffset + mFlashDataHeader.ProductLabel.off_set + mFlashDataHeader.header_length,
                (uint8_t*)buf,
                mFlashDataHeader.ProductLabel.length
                ),
            CHIP_ERROR_READ_FAILED);
#endif
    /* sonoff modify end */
    return CHIP_NO_ERROR;
};

} // namespace DeviceLayer
} // namespace chip
