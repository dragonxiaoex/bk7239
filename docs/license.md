完整产测 AT 列表见 [at.md](at.md)。本文只说明 License 相关指令。

新增AT指令
写入 License 数据 — AT+LICENSE_WRITE

指令格式：
AT+LICENSE_WRITE=<len>,<json>\r\n
其中 <len> 为紧凑 License JSON 的总字节数, <json> 为无空格、无换行的整包 JSON.

功能描述：
用于写入 License 数据文件。此指令操作 FLASH。

执行流程：
测试终端发送 AT+LICENSE_WRITE=<len>,<json>\r\n
设备校验、清空旧数据、存储、读回验证后应答最终结果

返回参数：
● 1、正常返回：见下文 C5
● 2、异常返回：AT+LICENSE_WRITE=ERROR\r\n<错误标识>:<错误原因>\r\n

错误码汇总：
错误标识	错误原因	触发条件
FAILED OPERATE	LICENSE ALREADY	设备已经烧录过 License
FAILED OPERATE	NON LICENSE FRAME	指令中没有 JSON 数据
FAILED PARAM	FRAMER LEN MISMATCHING	license_frame 数据长度不正确
FAILED PARAM	FRAMER SHA256 CHECK MISMATCHING	传输校验 SHA256 不匹配 / 读回校验不匹配
FAILED PARAM	NON COMPLIANCE WITH LICENSE RULES	字段长度/格式不符合规则要求, 或 <len> 与 JSON 实际长度不一致
FAILED PARAM	DEVICE MODEL MISMATCHING	device_model 与固件不匹配
FAILED HARDWARE	OTHER HARDWARE	存储空间写入失败
HARDWARE FAILED	STORAGE HARDWARE	存储硬件故障

License 数据帧格式（JSON, 线上传输为单行紧凑格式）
{"license_frame_len":181,"license_frame":{"deviceid":"10007ff7ed","factory_apikey":"9efbffd8-aad7-4816-8556-7711c94c5c1a","base_mac":"d0:27:00:ff:ed:2a","device_model":"uiid","uiid":77},"sha256_check":"<传输校验SHA256>"}

与NVDM ITEM对照
deviceid对应NVDM_FACTORY_ITEM_DEVICE_ID
factory_apikey对应NVDM_FACTORY_ITEM_FACTORY_APIKEY
base_mac对应NVDM_FACTORY_ITEM_BASE_MAC
device_model对应NVDM_FACTORY_ITEM_DEVICE_MODEL
uiid对应NVDM_FACTORY_ITEM_DEVICE_UIID

关键字段约束：
字段	长度	格式	说明
deviceid	10B	固定长度字符串	设备唯一标识
factory_apikey	36B	xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx	API 密钥
base_mac	17B	AA:BB:CC:DD:EE:FF	base MAC
device_model	可变    各项目长度不同，先不要校验长度   要和SONOFF_DEVICE_MODEL一致
uiid	可变	整数	固件标识，可为 77、65879 等

写入校验要求
C1 — 传输校验 SHA256
对 license_frame 中各字段值按以下顺序拼接后计算 SHA256 得到 sha256_check，仅用于传输校验，不存储。
sha256_check = SHA256(factory_apikey + base_mac + device_model + uiid + deviceid)
注意：是对各字段的值拼接后计算 SHA256，而非对 license_frame 的 JSON 文本计算。
示例：
sha256(9efbffd8-aad7-4816-8556-7711c94c5c1ad0:27:00:ff:ed:2aFWSN_SNZT04P7710007ff7ed)

C2 — 字段格式校验顺序
设备收到数据后按以下顺序校验：
1. license_frame_len 与实际 license_frame 长度是否一致 → 不一致返回 FRAMER LEN MISMATCHING
2. 解析各字段值，按 C1 拼接方式计算 SHA256 并与 sha256_check 比对 → 不正确返回 FRAMER SHA256 CHECK MISMATCHING
3. 各字段长度/格式检查（在步骤 2 解析过程中同步完成） → 不符合规则返回 NON COMPLIANCE WITH LICENSE RULES
4. device_model 与固件比对 → 不匹配返回 DEVICE MODEL MISMATCHING

C3 — 存储要求
● 收到完整指令后做好 License 存储空间准备（清空旧数据）
● 存储失败返回 HARDWARE FAILED: STORAGE HARDWARE

C4 — 读回校验
存储完成后，从 FLASH 读回 License 数据并按 C1 拼接方式计算 SHA256，与写入数据对比。不一致返回 FRAMER SHA256 CHECK MISMATCHING。

C5 — 最终应答
读回校验通过后，按以下顺序拼接并计算 SHA256：
deviceid + factory_apikey + base_mac + device_model + uiid
将计算好的 SHA256 值作为最终应答：
AT+LICENSE_WRITE=OK\r\n
SHA256=<64位HEX>\r\n
SHA256=<64位HEX>\r\n
SHA256=<64位HEX>\r\n
（重复三次，总长度一致便于产测软件解析）

查询 License 状态 — AT+LICENSE_READ?

指令格式：
AT+LICENSE_READ?\r\n

功能描述：
用于查询设备 License 状态，含是否写入 License 及相关信息。

返回参数：
● 1、正常返回：AT+LICENSE_READ=<UIID>,<模块名称>,<拼接SHA256>\r\n
● 2、异常返回：AT+LICENSE_READ=ERROR\r\n

返回参数说明：
● UIID：License 数据文件中的 UIID，标识固件
● 模块名称：License 数据文件中的 device_model，标识产品
● 拼接 SHA256：C5 拼接计算方式 SHA256(deviceid + factory_apikey + base_mac + device_model + uiid)

返回结果说明：
● A. 若设备未写入 License 或 License 校验异常或其他原因则返回 2
● B. 否则返回 1

示例
发送: AT+LICENSE_READ?\r\n
响应: AT+LICENSE_READ=77,onoff_plug,<64位HEX>\r\n

删除 License 数据 — AT+LICENSE_DELETE

指令格式：
AT+LICENSE_DELETE\r\n

功能描述：
用于删除设备 License 文件，包含所有已写入 License 及相关信息。

返回参数：
● 1、正常返回：AT+LICENSE_DELETE=OK\r\n
● 2、异常返回：AT+LICENSE_DELETE=ERROR\r\n

返回结果说明：
● A. 若设备删除 License 失败或清空 License 存储空间失败则返回 2
● B. 否则返回 1。若原设备中不存在 License，仍清空 License 存储空间

示例
发送: AT+LICENSE_DELETE\r\n
响应: AT+LICENSE_DELETE=OK\r\n
