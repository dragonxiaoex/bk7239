# Matter 生产数据未适配项

BK 工厂分区分两段：`+0KB` 证书区、`+4KB` 设备信息区。设备信息区已迁到 NVDM/工程宏。下面只列证书区还没接的项。

证书实际地址：`item.off_set + header_length`。Header 校验：`magic_code >= 0xF5F50000`。

| Matter 接口 | BK Flash 字段 | 编码 | 说明 |
|---|---|---|---|
| `GetCertificationDeclaration` | `CertificationDeclaration` | 二进制 CD | 当前空实现，直接返回成功 |
| `GetDeviceAttestationCert` | `DeviceAttestationCert` | 二进制 DAC | 仍读 `BK_PARTITION_MATTER_FACTORY` |
| `GetProductAttestationIntermediateCert` | `ProductAttestationIntermediateCert` | 二进制 PAI | 仍读 BK Flash |
| `SignWithDeviceAttestationKey` | `DacPublicKey` + `DacPrivateKey` | 原始密钥 | 用 DAC 私钥对消息做 ECDSA 签名 |

DAC 密钥约定：

- 公钥最长 70 字节，私钥最长 40 字节
- `CONFIG_BEKEN_DAC_PRIV_ENCRYPT=1` 时，私钥为 `13 字节 nonce + AES-CTR 密文`，AES-128 密钥写在 `FactoryDataProvider.h`
- 签名时把公私钥拼成 `P256Keypair`，再 `ECDSA_sign_msg`

`GetFirmwareInformation` 在 BK 原实现里就是空的，不必迁。

接入点仍是 `FactoryDataProvider` 里上述 4 个接口。NVDM 侧还没有对应条目，需要先补存储和 get 接口，再替换 `bk_flash_read`。
