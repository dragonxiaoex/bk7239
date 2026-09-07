# Matter 生产数据

运行时不再读 `BK_PARTITION_MATTER_FACTORY`。证书、配对参数和设备信息都从 NVDM（EasyFlash）或工程宏取。接入点仍是 overlay 里的 `FactoryDataProvider`。

NVDM 组名为 `matter`。CD / DAC / PAI / DAC 密钥以 Base64 字符串存储，读取时解码为二进制。

| Matter 接口 | 来源 | 说明 |
|---|---|---|
| `GetCertificationDeclaration` | `matter.CD` | `snfMatterCDGet` |
| `GetDeviceAttestationCert` | `matter.DAC.CERT` | `snfMatterDacCertGet`，DER |
| `GetProductAttestationIntermediateCert` | `matter.PAI.CERT` | `snfMatterPaiCertGet`，DER |
| `SignWithDeviceAttestationKey` | `matter.DAC.CERT` + `matter.DAC.KEY` | DAC 证书提取公钥，私钥 RAW 32 字节，拼成 `P256Keypair` 后 `ECDSA_sign_msg` |
| `GetFirmwareInformation` | 空 | BK 原实现就是空的 |
| `GetSetupDiscriminator` | `matter.discriminator` | |
| `GetSpake2pIterationCount` | `matter.iteration.count` | |
| `GetSpake2pSalt` | `matter.salt` | Base64 |
| `GetSpake2pVerifier` | `matter.verifier` | Base64 |
| `GetSetupPasscode` | `matter.passcode` | |
| `GetVendorName` | `matter.vendor.name` | |
| `GetVendorId` | `matter.vendor.id` | |
| `GetProductName` | `matter.product.name` | |
| `GetProductId` | `matter.product.id` | |
| `GetSerialNumber` | `factory.serial.number` | 14 位产品识别码 |
| `GetRotatingDeviceIdUniqueId` | `matter.rd.id.uid` | 32 位 hex 解码为 16 字节 |
| `GetProductLabel` | `matter.product.name` | 与产品名称相同 |
| `GetHardwareVersion` | `SONOFF_MATTER_HARDWARE_VERSION` | 工程宏 |
| `GetHardwareVersionString` | `SONOFF_MATTER_HARDWARE_VERSION_STRING` | 工程宏 |
| `GetManufacturingDate` | 未提供 | `CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE` |
| `GetPartNumber` | 未提供 | 同上 |
| `GetProductURL` | 未提供 | 同上 |

产测写入：

- 工厂数据：`AT+MT_FACTORY_DATA_WRITE` / `READ`
- CD：`AT+MT_CD_WRITE` / `READ` / `DELETE`
- DAC / DAC 密钥 / PAI：先 `AT+MT_PUB_KEY_GET` + `AT+MT_PUB_KEY_SET`，再 `AT+MT_SECURE_CERT_WRITE`；查询 `AT+MT_SECURE_CERT_READ`，删除 `AT+MT_SECURE_CERT_DELETE`

overlay 里对 `BK_PARTITION_MATTER_FACTORY` 的旧读路径保留在 `#if 0` 中，不参与编译。
