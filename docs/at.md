产测 AT 指令

串口发送以 `\r\n` 结尾。平台 CLI 按空格、`=`、`,` 和引号切分参数。查询类命令注册名带 `?`；成功/失败应答里的命令名不含 `?`。

通用应答：
● 成功：`AT+CMD=<value>\r\n`
● 失败：`AT+CMD=ERROR\r\n`（License 写入另有错误标识行，见 [license.md](license.md)）

发送 `AT`（无参数）会逐行列出已注册命令名。

指令一览

指令	功能
AT+MASTER_CHIP_ID?	查询主芯片唯一标识
AT+FW_VER?	查询固件版本字符串
AT+MT_SERIAL_NUM?	查询产品识别码
AT+MT_SERIAL_NUM_SET	写入产品识别码
AT+MT_FACTORY_DATA_WRITE	写入 Matter 工厂数据
AT+MT_FACTORY_DATA_READ	读取 Matter 工厂数据
AT+MT_CD_WRITE	写入 Matter CD
AT+MT_CD_READ	读取 Matter CD
AT+MT_CD_DELETE	删除 Matter CD
AT+MT_PUB_KEY_GET	获取 Matter 加密公钥
AT+MT_PUB_KEY_SET	设置服务端加密公钥
AT+MT_SECURE_CERT_WRITE	写入 Matter 安全证书
AT+MT_SECURE_CERT_READ	读取 Matter 安全证书状态
AT+MT_SECURE_CERT_DELETE	删除 Matter 安全证书
AT+ACTIVE_CODE	写入授权码
AT+ACTIVE_CODE?	查询授权状态
AT+LICENSE_WRITE	写入 License
AT+LICENSE_READ?	查询 License 状态
AT+LICENSE_DELETE	删除 License

查询主芯片 ID — AT+MASTER_CHIP_ID?

指令格式：
AT+MASTER_CHIP_ID?\r\n

功能描述：
读取本机 BASE MAC，拼成 16 字节 UID（前 6 字节为 MAC，后 10 字节补 0），以大写十六进制输出。前缀固定为 `BK723x`。授权码明文与该 UID 相同。

返回参数：
● 1、正常返回：AT+MASTER_CHIP_ID=BK723x-<32位大写HEX>\r\n
● 2、异常返回：AT+MASTER_CHIP_ID=ERROR\r\n

示例
发送: AT+MASTER_CHIP_ID?\r\n
响应: AT+MASTER_CHIP_ID=BK723x-D02700FFED2A00000000000000000000\r\n
（对应 BASE MAC `d0:27:00:ff:ed:2a`）

查询固件版本 — AT+FW_VER?

指令格式：
AT+FW_VER?\r\n

功能描述：
按项目配置拼装版本字符串：
FW{CLASS}-{SERIAL}-{FUNCTION}-{CHIP}-v{VERSION}

onoff_plug 当前为 `FWSW-SERIAL-SWITCH-BK7239N-v1.1.2`。

返回参数：
● 1、正常返回：AT+FW_VER=<版本字符串>\r\n
● 2、异常返回：AT+FW_VER=ERROR\r\n

示例
发送: AT+FW_VER?\r\n
响应: AT+FW_VER=FWSW-SERIAL-SWITCH-BK7239N-v1.1.2\r\n

查询产品识别码 — AT+MT_SERIAL_NUM?

指令格式：
AT+MT_SERIAL_NUM?\r\n

功能描述：
读取已写入的 14 位十进制产品识别码。未写入或格式非法时失败。

返回参数：
● 1、正常返回：AT+MT_SERIAL_NUM=<14位数字>\r\n
● 2、异常返回：AT+MT_SERIAL_NUM=ERROR\r\n

示例
发送: AT+MT_SERIAL_NUM?\r\n
响应: AT+MT_SERIAL_NUM=12345678901234\r\n

写入产品识别码 — AT+MT_SERIAL_NUM_SET

指令格式：
AT+MT_SERIAL_NUM_SET=<14位数字>\r\n

功能描述：
写入产品识别码。必须恰好 14 位、且每位为 `0`–`9`。此指令操作 FLASH。

返回参数：
● 1、正常返回：AT+MT_SERIAL_NUM_SET=OK\r\n
● 2、异常返回：AT+MT_SERIAL_NUM_SET=ERROR\r\n

示例
发送: AT+MT_SERIAL_NUM_SET=12345678901234\r\n
响应: AT+MT_SERIAL_NUM_SET=OK\r\n

写入 Matter 工厂数据 — AT+MT_FACTORY_DATA_WRITE

指令格式：
AT+MT_FACTORY_DATA_WRITE=<discriminator>,<iteration.count>,<salt>,<verifier>,<vendor.id>,<vendor.name>,<product.id>,<product.name>,<rd.id.uid>,<passcode>,<sha256>\r\n

功能描述：
一次性写入 10 个 Matter 工厂字段。先按字段值做 SHA256 校验，再逐项写入 FLASH。字段内不能含逗号（名称类字段也不允许逗号）。

字段说明：
字段	约束
discriminator	十进制，0–4095
iteration.count	十进制无符号 32 位
salt	Base64，最长 44
verifier	Base64，最长 132
vendor.id	十六进制，不含 0x，1–4 位
vendor.name	非空可打印 ASCII，不含逗号，最长 32
product.id	十六进制，不含 0x，1–4 位
product.name	非空可打印 ASCII，不含逗号，最长 32
rd.id.uid	32 位十六进制
passcode	十进制无符号 32 位
sha256	64 位十六进制

SHA256 计算方式：
对上述 10 个字段值按顺序拼接，每个字段后都带一个逗号（含最后一个字段）：
SHA256(discriminator + "," + iteration.count + "," + salt + "," + verifier + "," + vendor.id + "," + vendor.name + "," + product.id + "," + product.name + "," + rd.id.uid + "," + passcode + ",")

返回参数：
● 1、正常返回：AT+MT_FACTORY_DATA_WRITE=OK\r\n
● 2、异常返回：AT+MT_FACTORY_DATA_WRITE=ERROR\r\n（参数个数不对、SHA256 不匹配、字段格式非法或写入失败）

读取 Matter 工厂数据 — AT+MT_FACTORY_DATA_READ

指令格式：
AT+MT_FACTORY_DATA_READ\r\n

功能描述：
从 FLASH 读回已写入的工厂数据。当前拼装不含 passcode：
<discriminator>,<iteration.count>,<salt>,<verifier>,<vendor.id>,<vendor.name>,<product.id>,<product.name>,<rd.id.uid>,
其中 vendor.id、product.id 以 4 位大写十六进制输出。再对该整段（含末尾逗号）计算 SHA256，紧接在后面输出。

返回参数：
● 1、正常返回：AT+MT_FACTORY_DATA_READ=<9字段+末尾逗号><64位HEX>\r\n
● 2、异常返回：AT+MT_FACTORY_DATA_READ=ERROR\r\n（数据未写全、格式非法或读失败）

说明：WRITE 的 SHA256 覆盖 10 个字段（含 passcode）；READ 的 SHA256 覆盖上述 9 个字段拼装串。两者摘要不同。

写入 Matter CD — AT+MT_CD_WRITE

指令格式：
AT+MT_CD_WRITE=<len>,<CD base64>,<SHA256 64位hex>\r\n

其中 `<len>` 为 CD Base64 文本的字节数, 须与 `<CD base64>` 实际长度一致. SHA256 只对解码后的二进制 CD 计算, 不对 Base64 文本计算.

功能描述：
校验 `<len>` 与 Base64 长度一致后, 解码得到二进制 CD, 再校验 SHA256, 写入 `NVDM_MATTER_ITEM_CD`. 此指令操作 FLASH. Base64 可含填充 `=`.

返回参数：
● 1、正常返回：AT+MT_CD_WRITE=OK\r\n
● 2、异常返回：AT+MT_CD_WRITE=ERROR\r\n（参数个数不对、长度不匹配、Base64 非法、SHA256 不匹配或写入失败）

读取 Matter CD — AT+MT_CD_READ

指令格式：
AT+MT_CD_READ\r\n

返回参数：
● 1、正常返回：AT+MT_CD_READ=<len>\r\n<CD文件base64>\r\nSHA256=<64位HEX>\r\n
● 2、异常返回：AT+MT_CD_READ=ERROR\r\n（未写入 CD 文件或数据非法）

删除 Matter CD — AT+MT_CD_DELETE

指令格式：
AT+MT_CD_DELETE\r\n

功能描述：
删除设备中的 CD 文件。原本未写入时仍清空存储。

返回参数：
● 1、正常返回：AT+MT_CD_DELETE=OK\r\n
● 2、异常返回：AT+MT_CD_DELETE=ERROR\r\n

示例
发送: AT+MT_CD_DELETE\r\n
响应: AT+MT_CD_DELETE=OK\r\n

获取 Matter 加密公钥 — AT+MT_PUB_KEY_GET

指令格式：
AT+MT_PUB_KEY_GET\r\n

功能描述：
获取设备临时生成的 ECDH 公钥（曲线为 secp256r1，未压缩 65 字节），用于产测工具加密 secure_cert 数据。公钥为设备临时随机生成，不存储；设备上电或重启后需重新获取。此公钥仅用于后续 `AT+MT_SECURE_CERT_WRITE` 中的数据加密。重复获取会丢弃上一组本地密钥及已设置的服务端公钥。

返回参数：
● 1、正常返回：AT+MT_PUB_KEY_GET=<pub_key hex>,<sha256 hex>\r\n
● 2、异常返回：AT+MT_PUB_KEY_GET=ERROR\r\n

返回参数说明：
● pub_key：ECDH 公钥，小写十六进制字符串（130 字符）
● sha256：对 `<pub_key hex>` 文本计算的 SHA256

设置服务端加密公钥 — AT+MT_PUB_KEY_SET

指令格式：
AT+MT_PUB_KEY_SET=<pub_key hex>,<iv hex>,<tag hex>,<sha256 hex>\r\n

功能描述：
设置服务端的 ECDH 公钥及 AES-GCM 解密参数，用于设备解密后续 `AT+MT_SECURE_CERT_WRITE` 发送的加密数据。设备不存储服务端公钥，重启后需重新设置。

输入参数：

参数	说明
pub_key	服务端 ECDH 公钥，未压缩十六进制，130 字符
iv	AES-GCM IV，12 字节，24 位十六进制
tag	AES-GCM TAG，16 字节，32 位十六进制
sha256	对 `<pub_key hex>,<iv hex>,<tag hex>` 计算的 SHA256（不含末尾逗号）

返回参数：
● 1、正常返回：AT+MT_PUB_KEY_SET=OK\r\n
● 2、异常返回：AT+MT_PUB_KEY_SET=ERROR\r\n

示例
发送: AT+MT_PUB_KEY_SET=<pub_key hex>,<iv hex>,<tag hex>,<sha256 hex>\r\n
响应: AT+MT_PUB_KEY_SET=OK\r\n

写入 Matter 安全证书 — AT+MT_SECURE_CERT_WRITE

指令格式：
AT+MT_SECURE_CERT_WRITE=<len>,<安全证书加密数据 base64>,<sha256 hex>\r\n

其中 `<len>` 为 `<安全证书加密数据 base64>` 文本长度。产测工具先 AES-256-GCM 加密再 Base64。AES 参数：
● Key：ECDH secp256r1 共享密钥 32 字节
● IV：12 字节，来自 `AT+MT_PUB_KEY_SET`
● TAG：16 字节，来自 `AT+MT_PUB_KEY_SET`
● AAD：空

明文为小端数据头加 DAC 证书(DER)、DAC 密钥(RAW 32B)、PAI 证书(DER)：
dac_cert_len(4) + dac_key_len(4) + pai_cert_len(4) + DAC + DAC_KEY + PAI

功能描述：
用于写入 Matter 设备的 DAC 密钥、DAC 证书、PAI 证书。须先执行 `AT+MT_PUB_KEY_GET` 和 `AT+MT_PUB_KEY_SET`。密钥单次有效，写入后（含失败）自动失效，重复写入需重新交换密钥。设备支持重复烧录。

返回参数：
● 1、正常返回：AT+MT_SECURE_CERT_WRITE=OK\r\nSHA256=<64位HEX>\r\n
● 2、异常返回：AT+MT_SECURE_CERT_WRITE=ERROR\r\n

其中应答 SHA256 为解密后「数据头+DAC证书+DAC密钥+PAI证书」整体摘要。输入 SHA256 对 Base64 文本计算。

读取 Matter 安全证书状态 — AT+MT_SECURE_CERT_READ

指令格式：
AT+MT_SECURE_CERT_READ\r\n

功能描述：
查询设备中安全证书的写入状态。未写入产品识别码或证书时失败。

返回参数：
● 1、正常返回：AT+MT_SECURE_CERT_READ=<serial_num>,<sha256 hex>\r\n
● 2、异常返回：AT+MT_SECURE_CERT_READ=ERROR\r\n

返回参数说明：
● serial_num：松诺产品识别码（14 位十进制数字）
● sha256：存储的安全证书明文（数据头+DAC+DAC密钥+PAI）的 SHA256

示例
发送: AT+MT_SECURE_CERT_READ\r\n
响应: AT+MT_SECURE_CERT_READ=12345678901234,<sha256 hex>\r\n

删除 Matter 安全证书 — AT+MT_SECURE_CERT_DELETE

指令格式：
AT+MT_SECURE_CERT_DELETE\r\n

功能描述：
删除设备中的安全证书。原本未写入时仍清空存储。

返回参数：
● 1、正常返回：AT+MT_SECURE_CERT_DELETE=OK\r\n
● 2、异常返回：AT+MT_SECURE_CERT_DELETE=ERROR\r\n

示例
发送: AT+MT_SECURE_CERT_DELETE\r\n
响应: AT+MT_SECURE_CERT_DELETE=OK\r\n

写入授权码 — AT+ACTIVE_CODE

指令格式：
AT+ACTIVE_CODE=<32位HEX>\r\n

功能描述：
写入本机授权码。设备用 AES-128-ECB 校验该码是否由本机 ChipID 算出，匹配才落盘。密钥为 `soNoFF22soNoFF22`。明文与 `AT+MASTER_CHIP_ID?` 的 16 字节 UID 相同：BASE MAC 6 字节 + 10 字节 0。密文 16 字节转成 32 位十六进制即为授权码。此指令操作 FLASH。

返回参数：
● 1、正常返回：AT+ACTIVE_CODE=OK\r\n
● 2、异常返回：AT+ACTIVE_CODE=ERROR\r\n（长度/格式非法、与本机不匹配或写入失败）

示例
发送: AT+ACTIVE_CODE=<32位HEX>\r\n
响应: AT+ACTIVE_CODE=OK\r\n

查询授权状态 — AT+ACTIVE_CODE?

指令格式：
AT+ACTIVE_CODE?\r\n

功能描述：
读取已存储授权码，并再次与本机 ChipID 比对。不回传授权码本身。

返回参数：
● 1、正常返回：AT+ACTIVE_CODE=OK\r\n（已授权）
● 2、异常返回：AT+ACTIVE_CODE=ERROR\r\n（未写入、格式非法或与本机不匹配）

License 相关指令

写入、查询、删除的完整约定（JSON 帧、C1–C5、错误码）见 [license.md](license.md)。摘要如下。

写入 License — AT+LICENSE_WRITE

指令格式：
AT+LICENSE_WRITE=<len>,<json>\r\n
`<json>` 必须是无空格、无换行的紧凑 JSON；`<len>` 必须等于该 JSON 的实际字节数。

正常返回：
AT+LICENSE_WRITE=OK\r\n
SHA256=<64位HEX>\r\n
SHA256=<64位HEX>\r\n
SHA256=<64位HEX>\r\n

异常返回：
AT+LICENSE_WRITE=ERROR\r\n
<错误标识>:<错误原因>\r\n

查询 License — AT+LICENSE_READ?

指令格式：
AT+LICENSE_READ?\r\n

正常返回：AT+LICENSE_READ=<uiid>,<device_model>,<C5 SHA256>\r\n
异常返回：AT+LICENSE_READ=ERROR\r\n

删除 License — AT+LICENSE_DELETE

指令格式：
AT+LICENSE_DELETE\r\n

正常返回：AT+LICENSE_DELETE=OK\r\n
异常返回：AT+LICENSE_DELETE=ERROR\r\n
设备中原本没有 License 时仍清空存储并返回 OK。
