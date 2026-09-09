# Secure Boot + OTA 验签迁移说明

记录日期：2026-09-08  
适用项目：`onoff_plug / bk7239n`

本文记录当前项目迁移到 Secure Boot + OTA 验签时需要调整的部分。当前采用 ECDSA P-256 + SHA-256、AWS KMS 和覆盖式 OTA；密钥轮换方案仍待设计。2026-09-08 已完成当前产品构建、真实 KMS 签名和主机端产物验签，记录见 [签名配置第 8 节](secure_boot_signing_setup.md#8-aws-kms-签名)。板端启动、OTA 和失败恢复仍未完成验证；下文保留的旧分步签名问题不代表当前正常打包路径仍使用该实现。

当前实施范围已进一步明确：不引入 TF-M，先建立独立 BL2 的启动验证链；应用层 OTA 验签可暂缓。第 2、3、6 节保留完整迁移目标，第 7 节给出当前阶段的具体步骤。

完成产品分区后，继续参考 [签名配置与 BL2 公钥绑定操作说明](secure_boot_signing_setup.md)，其中提供开发密钥命令、配置文件内容和固定公钥的修改示例。

## 1. 当前基础与目标

当前 HTTP 和 Matter OTA 统一经过 Sonoff OTA 模块，已有构建产物使用普通 bootloader 和 RBL 升级包。Sonoff OTA 尚未实现固件数字签名验证。产品 config 已加入独立 BL2 的安全构建开关，但尚未完成配套分区、签名配置及安全构建验证，不能据此认定设备已启用安全启动或防回退。

目标链路为：

```text
发布端：构建固件 → 生成待签数据 → KMS 签名 → 回填签名 → 发布安全升级包
设备端：接收升级包 → 验签及版本检查 → 提交升级 → BL2 验证并安装
启动链：ROM 中的 BL1 → 验证 BL2 → BL2 验证应用 → 执行应用
```

KMS 保管发布私钥，设备通过可信公钥离线验签。OTP 中的可信公钥摘要用于建立信任根；固件发布密钥与 Matter DAC 密钥应独立管理。

## 2. 当前项目主要需要调整的部分

| 当前部分 | 主要调整内容 | 对应文件或目录 |
| --- | --- | --- |
| 普通 bootloader | 迁移到安全 BL2/MCUboot，建立 ROM BL1 到 BL2、BL2 到应用的验证链；核实所有启动路径是否执行完整验签。 | [当前构建入口](../build_tool/Makefile)、[SDK 安全启动说明](../bk_openthread/bk_idk/docs/bk7239n/zh_CN/security/bk_security_boot.rst) |
| 普通分区表 | 增加 manifest、BL2、升级控制等安全分区；重新计算应用、OTA、Matter 和用户数据分区容量，满足 CRC 块和向量表对齐要求。 | [当前分区配置](../build_tool/config/bk7239n/auto_partitions.csv)、[安全示例分区](../bk_openthread/bk_idk/projects/security/overwrite/config/bk7239n/partitions.csv) |
| RBL 打包流程 | 改用 SDK 安全镜像格式，明确镜像头、受保护元数据、填充和载荷的签名范围；接入 KMS 外部签名及签名回填。 | [安全打包配置](../bk_openthread/bk_idk/projects/security/overwrite/config/bk7239n/pack.json)、[外部签名步骤](../bk_openthread/bk_idk/tools/env_tools/bksecure/scripts/steps.py)、[OTA 打包实现](../bk_openthread/bk_idk/tools/env_tools/bksecure/scripts/pack_raw_ota.py) |
| Sonoff OTA 核心及适配层 | 接入安全包解析、真实验签、版本检查、升级提交及取消流程；只有全部验证成功后才进入可应用状态。 | [OTA 核心](../sonoff/ota/sonoff_ota.c)、[OTA 接口](../sonoff/ota/sonoff_ota.h)、[Beken 适配层](../sonoff/ota/sonoff_ota_adapter.c) |
| HTTP OTA | 将收到的安全升级包交给统一处理链路，统一长度语义和失败处理。 | [HTTP OTA](../sonoff/ota/sonoff_ota_http.c) |
| Matter OTA | 保留 Matter 外层传输能力，将内部载荷从普通 RBL 改为安全升级包；同步调整包头、长度、版本解析及错误上报。 | [Matter OTA 适配](../sonoff/ota/sonoff_ota_matter.c)、[Matter 平台覆盖文件](../sonoff_modify/matter_modify/connectedhomeip/src/platform/Beken/OTAImageProcessorImpl.cpp) |
| 公钥和安全版本管理 | 明确 BL1、BL2 和 OTA 验签的密钥分工，设计公钥可信来源、轮换规则及安全计数器；将关键版本元数据纳入签名保护。 | [安全配置示例](../bk_openthread/bk_idk/projects/security/overwrite/config/bk7239n/security.csv)、[镜像版本配置示例](../bk_openthread/bk_idk/projects/security/overwrite/config/bk7239n/bin.csv) |
| 生产烧录 | 部署签名固件、可信公钥摘要和安全启动配置；在开发板验证完成后确定生产 OTP/eFuse 配置及锁定步骤。 | [SDK 安全启动部署说明](../bk_openthread/bk_idk/docs/bk7239n/zh_CN/security/bk_security_boot.rst) |

当前安全示例允许在 `CONFIG_TFM=n` 的情况下使用 BL2。迁移 Secure Boot 不要求同时引入完整 TF-M，可先参考 [overwrite 示例配置](../bk_openthread/bk_idk/projects/security/overwrite/config/bk7239n/config)跑通安全启动和升级。

涉及 SDK 或 Matter 子仓库的后续源码修改，应按项目现有覆盖机制维护在 `sonoff_modify` 对应目录中。上述 SDK 路径用于定位实现和示例，不表示直接修改子仓库原文件。

## 3. 安全升级包与 KMS 接入

SDK 压缩覆盖式 OTA 存在内外两层签名：

```text
应用镜像 → 内层签名 → 压缩已签名镜像 → 外层签名 → OTA 封装
```

内层签名保护最终安装和执行的镜像，外层签名保护升级时接收的压缩载荷。KMS 需要接入实际采用的签名步骤；不能只对原始 `app.bin` 计算摘要后，假定该签名同时满足两层验证。

对接前需确定：

- 签名算法和密钥类型，当前候选为 ECDSA P-256 + SHA-256。
- 待签字节范围，包括镜像头、受保护元数据及必要填充。
- 摘要的哈希次数，以及 KMS 接口使用原文模式还是摘要模式。
- 签名编码使用 DER 还是固定长度 `r || s`，公钥采用何种编码。
- 构建产物、公钥标识、安全计数器和签名结果的对应关系。

SDK 的 `steps.py` 已有摘要导出、签名服务和签名回填步骤，但当前产品构建未接入。`imgtool/main.py` 中的 `sign_hash()` 是候选接入位置，需与下述哈希语义问题一起处理。

## 4. 迁移前必须核实或修正的问题

### 4.1 当前应用层拒绝升级不能阻止后续重启安装

当前普通 bootloader 会在重启时检查 OTA 分区中的 RBL 包。Sonoff OTA 的取消或失败路径尚未建立可靠的镜像失效与提交机制，完整 RBL 留在暂存区时，后续重启仍可能触发安装。

迁移后应使用安全升级控制机制，并由 BL2 执行最终验证。需要验证未提交、验签失败和断电情况下的行为，不能仅依赖应用层不调用 `Apply`。

参考：[当前适配层](../sonoff/ota/sonoff_ota_adapter.c)、[普通 bootloader OTA 流程](../bk_openthread/bk_idk/docs/bk7239n/zh_CN/developer-guide/bootloader_ota/bootloader_and_ota/bk_up_bootloader_and_ota.rst)。

### 4.2 安全示例包含跳过验签配置

overwrite 示例设置了 `CONFIG_BL2_SKIP_VALIDATE=y`。以完整启动验签为目标时，需要检查并关闭相关跳过路径，同时核对 BL1 和 BL2 的实际构建配置及硬件配置。不能因为启用了 BL2 就认定每次启动都完成签名验证。

独立 BL2 还需要 `CONFIG_VALIDATE_IMAGE=y`、`CONFIG_VALIDATE_IMAGE_HASH_ONLY=n`。`CONFIG_VALIDATE_IMAGE` 同时参与部分应用层 OTA 代码的条件编译，暂缓应用层验签时不能关闭这个全局开关，应调整应用层调用链。

参考：[示例配置](../bk_openthread/bk_idk/projects/security/overwrite/config/bk7239n/config)、[独立 BL2 构建入口](../bk_openthread/bk_idk/components/bk_mcuboot/CMakeLists.txt)、[实际 BL2 镜像验证实现](../bk_openthread/bk_idk/components/bk_mcuboot/bl2/components/mcuboot/bootutil/src/image_validate.c)。

### 4.3 部分 OTA 验证接口仅返回成功

`components/bk_ota/ota_ow.c` 中的 `ow_ota_verify()` 当前直接返回 `BK_OK`。仅调用 `bk_ota_verify()` 不能证明覆盖式 OTA 已执行验签，需要确认包解析过程中真正的摘要、公钥和签名验证调用链。

参考：[覆盖式 OTA 实现](../bk_openthread/bk_idk/components/bk_ota/ota_ow.c)、[OTA 验证实现](../bk_openthread/bk_idk/components/bk_ota/ota_verify.c)。

### 4.4 外部签名与设备验签的哈希语义存在差异

当前 SDK 的摘要导出分支对镜像摘要再次 SHA-256，`sign_hash()` 又调用内部会执行哈希的签名接口，而普通签名分支直接对完整待签镜像执行带 SHA-256 的 ECDSA 签名。

源码定位更正：`CONFIG_BL2=y` 的独立构建使用 `components/bk_mcuboot/bl2/components/mcuboot/` 下的实现，其 `bootutil_img_validate()` 向验签函数传入镜像摘要。此前引用的 `components/mcuboot/boot/bootutil/src/image_validate.c` 是另一份实现，不能将其中使用二次摘要的逻辑作为当前独立 BL2 的结论。

需要通过同一份测试镜像和签名向量，逐项验证普通签名、外部签名、OTA 验签及 BL2 验签的输入是否一致。不能直接将摘要导出结果提交到 KMS 后，就认定链路已经兼容。

参考：[摘要导出与签名回填](../bk_openthread/bk_idk/tools/env_tools/bksecure/tools/mcuboot_tools/imgtool/image.py)、[sign_hash](../bk_openthread/bk_idk/tools/env_tools/bksecure/tools/mcuboot_tools/imgtool/main.py)、[ECDSA 签名实现](../bk_openthread/bk_idk/tools/env_tools/bksecure/tools/mcuboot_tools/imgtool/keys/ecdsa.py)、[实际 BL2 验签](../bk_openthread/bk_idk/components/bk_mcuboot/bl2/components/mcuboot/bootutil/src/image_validate.c)。

### 4.5 防回退计数器的更新时机需要修正

`ota_verify.c` 在 `CONFIG_ANTI_ROLLBACK` 分支中，存在解析 `IMAGE_TLV_SEC_CNT` 时就更新 OTP 的路径，早于后续签名检查。启用前必须修正更新时机，确保未认证数据不能改变设备最低安全版本，并与安装、恢复策略协调。

业务版本与安全计数器应分别管理。安全计数器不必随每次功能发布增加；需要禁止旧安全版本时再提升，并验证低计数器镜像即使签名有效也会被拒绝。

参考：[OTA 验证实现](../bk_openthread/bk_idk/components/bk_ota/ota_verify.c)。

### 4.6 覆盖式 BL2 的公钥信任约束需要补齐

实际独立 BL2 的 `ow_pubkey.c::bk_find_key()` 在 `CONFIG_OTA_UPDATE_PUBKEY` 关闭时，直接将包内公钥赋给 `bootutil_keys[0]` 并返回成功。这条路径没有将该公钥与设备可信公钥或 OTP 摘要进行匹配，仅验证签名数学关系不足以证明固件由厂商发布。

开启该配置后，函数会与备份公钥或主镜像公钥比较，但仍需追踪该参考公钥如何获得信任、如何防止被替换，不能将这个开关直接等同于 OTP 信任根验证。

第一阶段建议固定发布公钥，选择并实现以下一种信任约束：将候选公钥按平台规定编码计算摘要后与 OTP 中的 BL2 可信公钥摘要比较；或与编入 BL2 的可信公钥比较，并由 BL1 验证包含该公钥的 BL2。公钥不匹配必须终止验签路径。至少使用另一把私钥签出的完整合法包作为负例，确认设备会拒绝。

参考：[覆盖式公钥选择](../bk_openthread/bk_idk/components/bk_mcuboot/bl2/components/mcuboot/src/ow_pubkey.c)、[OTP 公钥摘要读取接口](../bk_openthread/bk_idk/components/bk_mcuboot/bl2/components/mcuboot/src/keys.c)。

## 5. 升级策略与容量

此前构建产物显示，设备使用 4 MiB Flash，原始应用约 2 MiB，当前普通 RBL 包约 1.28 MiB。安全格式、分区和算法配置改变后，需要重新构建并计算实际占用，不能沿用普通 RBL 的剩余空间作为安全方案的容量结论。

建议优先评估压缩覆盖式 OTA，但需要明确以下区别：

- 防回退：拒绝低于设备安全版本下限的旧固件。
- 故障回退：新固件无法正常运行时恢复旧固件。

覆盖式升级不能默认提供故障回退能力。若产品必须支持新固件试运行失败后自动恢复，需要重新评估双镜像布局、镜像大小或 Flash 容量。

## 6. 建议实施顺序

1. 确认 KMS 服务、算法、升级策略、公钥信任关系及安全计数器规则。
2. 以测试密钥和 SDK 安全示例建立最小工程，验证 BL1、BL2、应用的完整启动验签。
3. 重新规划产品分区，将 Sonoff 应用迁入安全构建流程。
4. 调整 HTTP、Matter 和统一 OTA 模块，接通安全包解析、验签、提交及取消流程。
5. 统一签名范围、哈希次数及编码后，将本地签名替换为 KMS 外部签名，验证两端结果。
6. 验证正确升级、错误签名、错误公钥、缺失签名、载荷及受保护元数据被篡改、低安全版本，以及接收、提交、安装阶段断电后的行为。
7. 明确生产密钥、恢复路径和轮换策略，最后确定生产 OTP/eFuse 烧录与锁定流程。

OTP/eFuse 涉及不可逆配置。示例中的密钥和开关仅用于参考，不能直接作为生产配置；生产部署应在完整启动及 OTA 链路通过板端验证后进行。

## 7. 不引入 TF-M 的启动链实施步骤

本阶段目标是 `BL1 验证 BL2 → BL2 验证应用镜像 → 运行应用`。先用开发测试密钥建立可验证的启动链，再接入 KMS 和网络升级。应用层不验签时，发布镜像仍必须签名。

### 7.1 切换到独立 BL2 安全构建

在 `build_tool/config/bk7239n/config` 中准备以下目标配置。它们需要与安全分区、公钥绑定和签名打包一起完成，单独添加不能视为可烧录版本。

```ini
CONFIG_TFM=n
CONFIG_BUILD_TFM=n
CONFIG_SECURITY_FIRMWARE=y
CONFIG_SPE=1
CONFIG_BL2=y
CONFIG_BL2_UPGRADE_STRATEGY="OVERWRITE_ONLY"
CONFIG_VALIDATE_IMAGE=y
CONFIG_VALIDATE_IMAGE_HASH_ONLY=n
CONFIG_BL2_SKIP_VALIDATE=n
CONFIG_BL2_VALIDATE_ENABLED_BY_EFUSE=n
```

`CONFIG_SECURITY_FIRMWARE=y` 由 SDK 构建脚本读取，选择安全预处理和 `secure_pack.py` 打包。`CONFIG_BL2=y` 则触发独立 BL2 的 CMake 构建。`CONFIG_SPE=1` 参考无 TF-M 安全示例，需同时核实应用 RAM 布局；它不等于启用 TF-M。

涉及安全 OTA 时，还需配套 `CONFIG_BK_OTA=y`、`CONFIG_BK_OTA_NEW_PACK_TOOL=y`、`CONFIG_OTA_CONFIRM_UPDATE=y`，并核实 `CONFIG_FLASH_CBUS`、分区访问及存储布局。保留产品所需的 Matter、OpenThread、Wi-Fi 和 mbedTLS 配置，不直接用安全示例的整个 config 覆盖产品配置。

BL2 的 bk7239n 配置当前固定使用 `MCUBOOT_SIGNATURE_TYPE="EC256"`，与 ECDSA P-256 方向匹配。运行于应用侧的 mbedTLS 配置和独立 BL2 的密码库配置需要分别检查。

参考：[安全构建选择](../bk_openthread/bk_idk/tools/build_tools/parse_build_type.py)、[独立 BL2 构建](../bk_openthread/bk_idk/components/bk_mcuboot/CMakeLists.txt)、[BL2 算法配置](../bk_openthread/bk_idk/components/bk_mcuboot/bl2/middleware/soc/bk7239n/config/config.cmake)。

### 7.2 补齐安全配置文件和产品分区

在 `build_tool/config/bk7239n/` 中以 overwrite 示例为参考准备：

| 文件 | 用途 |
| --- | --- |
| `partitions.csv` | 安全分区，包含 BL1 控制、manifest、BL2、应用、OTA 和产品数据分区。 |
| `security.csv` | 配置 BL1 安全启动、签名密钥类型、密钥文件及其他安全选项。 |
| `bin.csv` | 声明 `bl2.bin`、`cpu0_app.bin` 的分区、版本和安全计数器；本方案不加入 `tfm_s.bin`。 |
| `pack.json` | 定义 BL1 manifest、应用签名和烧录/OTA 包的生成关系。 |
| `steps_pack.json` | 后续外部签名回填所需的分步打包配置。 |

安全构建使用 `partitions.csv` 生成分区信息，当前普通构建的 `auto_partitions.csv` 不能直接作为安全分区表。以 `build_tool/config/bk7239n/auto_partitions.csv` 为迁移基准，保留其中的 `usr_config`、`matter`、`easyflash`、`ot_setting`、RF 和网络数据分区，重新计算安全启动区域的地址、容量及对齐。当前源配置没有单独的 `matter_factory` 分区，不应根据旧构建产物擅自添加。示例的 2000 KiB 应用分区和 1024 KiB OTA 分区不代表产品能够放下。

当前 `bksecure` 使用的是 `bl1_secureboot_en`、`img_sign_key_type`、`img_sign_pubkey`、`img_sign_privkey` 等字段。不要直接套用旧文档中另一套 `secureboot_en`、`root_pubkey` 字段。

### 7.3 建立公钥绑定并完成本地签名验证

先完成第 4.6 节的公钥信任约束，再用独立开发密钥测试普通本地签名路径。开发私钥不提交到仓库，生产发布私钥留在 KMS 中。

分别核对两类签名对象：BL1 验证的 BL2 manifest，以及 BL2 验证的应用安全镜像。镜像头、填充、CRC 地址换算和签名元数据由配套打包工具生成；不要向旧 `app.bin` 任意追加签名后直接烧录。

### 7.4 构建并核对产物

前述配置和源码适配完成后，可从项目根目录使用独立输出目录构建：

```sh
make -C build_tool MODEL=onoff_plug SOC=bk7239n BUILD_DIR=/home/xt/sonoff/bk7239/build_secure_boot
```

当前 `build_tool/Makefile` 会应用覆盖文件，并在退出时恢复 SDK；SDK 修改需先按现有机制维护到 `sonoff_modify`，避免直接修改子仓库后被构建流程恢复。

核对实际生成的应用 `sdkconfig`、独立 BL2 的 `CMakeCache.txt` 和编译参数，确保签名算法、验签开关及分区配置一致。应检查 `bl2.bin`、manifest、应用签名镜像、`bootloader.bin`、`all-app.bin` 和 `otp_efuse_config.json`；需要 OTA 时再检查 `ota.bin`。这些是预期产物，当前尚未执行上述安全构建。

### 7.5 在开发板验证启动链

根据生成的分区和烧录包确定下载地址，部署匹配的开发公钥摘要和安全启动设置。OTP/eFuse 的实际状态需通过板端工具读取；配置文件中填写 TRUE 不代表硬件已经启用。

至少验证：正确镜像启动；另一把私钥签出的镜像被拒绝；签名错误或缺失的镜像被拒绝；BL2 或应用内容被篡改后被拒绝；上电、软件重启和看门狗重启均执行预期验证。负例应按镜像格式重算非密码学 CRC，避免仅因 CRC 错误被拒绝而误判为验签有效。

使用可恢复的开发板验证流程后，再确定生产启用、调试锁定及下载恢复策略。启动链验证完成后，下一阶段再接 KMS 和安全 OTA；若启用防回退，还需单独验证计数器更新和失败恢复。

## 8. onoff_plug 分区改动最小化候选

### 8.1 保留数据地址，沿用 SDK overwrite 示例的分区顺序

建议保留 OTA 起始地址和全部尾部数据分区的地址、容量；将普通 bootloader 的 68 KiB 扩展为安全启动前部区域的 116 KiB，从应用头部让出 48 KiB；再从 OTA 尾部划出 4 KiB 作为独立的升级控制区。

候选文件：[partitions_onoff_plug_secure_boot.csv](partitions_onoff_plug_secure_boot.csv)。它目前放在 `docs/`，用于评审；与安全构建其他文件配套后，再作为 `build_tool/config/bk7239n/partitions.csv` 使用。

下表以用户指定的 [build_tool/config/bk7239n/auto_partitions.csv](../build_tool/config/bk7239n/auto_partitions.csv) 为基准，使用普通分区解析器展开其中省略的地址。大小均为 Flash 物理容量，`1 KiB = 1024 B`。

基准更正：此前误用了 `build/bk7239n/onoff_plug/partitions/partitions.csv`。核对发现该构建目录留存的 `security/auto_partitions.csv` 本身就写有 `matter_factory=8K` 和 `easyflash=8K`，与当前源文件的 `easyflash=16K` 不一致；不能由此推断当前构建会自动拆分 EasyFlash。本候选已改为保留源文件中的完整 16 KiB EasyFlash 分区。

| 区域 | 原起始地址 | 原大小 | 候选起始地址 | 候选大小 |
| --- | --- | --- | --- | --- |
| 普通 bootloader → 安全启动前部区域 | `0x000000` | 68 KiB | `0x000000` | 116 KiB |
| `primary_cpu0_app` | `0x011000` | 2380 KiB | `0x01D000` | 2332 KiB |
| `ota` | `0x264000` | 1496 KiB | `0x264000` | 1492 KiB |
| `ow_ota_control` | 无 | 无 | `0x3D9000` | 4 KiB |
| `usr_config` | `0x3DA000` | 28 KiB | `0x3DA000` | 28 KiB |
| `matter` | `0x3E1000` | 84 KiB | `0x3E1000` | 84 KiB |
| `easyflash` | `0x3F6000` | 16 KiB | `0x3F6000` | 16 KiB |
| `ot_setting` | `0x3FA000` | 16 KiB | `0x3FA000` | 16 KiB |
| `sys_rf` | `0x3FE000` | 4 KiB | `0x3FE000` | 4 KiB |
| `sys_net` | `0x3FF000` | 4 KiB | `0x3FF000` | 4 KiB |

前部 116 KiB 按示例保留以下结构：`bl1_control`、`boot_flag`、`partition`、`primary_manifest`、`secondary_manifest` 各 4 KiB，随后是从 `0x005000` 开始的 96 KiB `bl2`。这里两个 manifest 不表示引入 TF-M 或双应用分区。

本候选以沿用示例分区顺序、保留产品数据地址为优先条件，不以压缩 BL2 到尚未验证的最小体积为目标。总容量为 `116 + 2332 + 1492 + 4 + 152 = 4096 KiB`。

### 8.2 需要保留的几个细节

- 保留当前源配置的 `easyflash` 16 KiB，不另行划出 `matter_factory`。已核对 `FactoryDataProvider` 覆盖文件，其旧 `BK_PARTITION_MATTER_FACTORY` 访问位于 `#if 0` 中，实际实现已改用 Sonoff NVDM，不需要因这些文本引用增加分区。
- `ota` 改为安全格式的 `Type=app, SubType=ow_ota`，应用为 `SubType=ow_active`。OTA 起始地址相同不意味着普通 RBL 包还能沿用。
- `ow_ota_control` 用于升级提交及解压恢复，暂缓应用层验签也不能将它省略。将它放在 OTA 尾部可以沿用示例顺序，同时不移动后面的数据分区。
- 不建议将控制区塞到 BL2 与应用之间：当前解压恢复代码只在控制区地址不低于应用起始地址时擦除该区，前置会触发保护分支。参考 [decompress_bl2.c](../bk_openthread/bk_idk/components/bk_mcuboot/bl2/components/mcuboot/src/decompress_bl2.c)。
- 所有地址显式固定，应用结束地址仍为 `0x264000`；后续应用体积增长时，打包应报容量不足，不能让自动顺延悄悄移动数据分区。
- 安全生成器会重新分配分区编号。应用应使用生成的 `BK_PARTITION_*` 宏或分区名称，不能继续依赖旧表中的数字编号。

### 8.3 容量核查与当前验证范围

本候选已使用当前 SDK 的 `Partitions` 解析器及 `prebuild_process()` 在临时目录生成分区头文件。校验了所有物理分区连续、无重叠、按 4 KiB 对齐、总范围恰好为 `[0x000000, 0x400000)`；并与当前 `auto_partitions.csv` 解析结果逐项比对了 6 个尾部数据分区的地址和容量。

在 Flash CRC 开启、沿用示例空 Flags 的条件下，生成器得到：

| 项目 | 结果 |
| --- | --- |
| 应用物理分区 | 2332 KiB |
| 应用签名 slot 容量 | 2,244,608 B，即 2192 KiB |
| 应用虚拟代码起始偏移 | `0x1C600`，由工具计算 CRC 换算、签名头和向量对齐 |
| 已有普通构建 `app.bin` | 2,048,548 B |
| 新 OTA 物理容量 | 1,527,808 B，即 1492 KiB |
| 96 KiB BL2 分区对应的代码容量 | 92,320 B，尚未用新 BL2 产物验证 |

以已有 `app.bin` 估算，从应用签名 slot 扣除 4 KiB 镜像头、保守预留的 4 KiB 尾部空间、320 B 向量填充和 10 B 合并元数据，尚余约 183 KiB。该值仅用于判断候选布局是否值得继续验证；新配置、BL2 公钥绑定及安全 OTA 代码会改变产物大小，最终应以重新链接、签名打包后的结果为准。

旧 RBL 的 1,340,960 B 不能直接作为新安全 OTA 包大小。新流程使用不同的压缩和内外签名封装；同时要区分下载文件总长与实际写入 OTA 分区的镜像载荷长度。需要运行安全打包及下载路径检查后，才能确认 OTA 容量足够。

参考：[物理分区校验](../bk_openthread/bk_idk/tools/env_tools/bksecure/scripts/partition.py)、[CRC 与向量对齐计算](../bk_openthread/bk_idk/tools/env_tools/bksecure/scripts/size_crc.py)、[分区生成器](../bk_openthread/bk_idk/tools/env_tools/bksecure/scripts/partitions.py)。本次没有执行固件构建或板端升级测试。

### 8.4 首次切换与后续升级

保持地址可以避免因分区搬迁而迁移上述数据分区中的内容，但首次切换仍涉及 bootloader、manifest、分区表和应用链接地址的变化，不能把安全镜像当成普通 RBL 直接走现有 OTA。若板端实际采用了不同于当前源配置的旧布局，应另行核对数据迁移需求。

首次应使用匹配的新启动链和应用进行开发板烧录验证，初始化新的 OTA 控制区及暂存区，并确保烧录操作保留上述 6 个数据分区；保留分区地址本身不能保证整片擦除后数据仍在。后续安全 OTA 使用同一份冻结的分区布局。
