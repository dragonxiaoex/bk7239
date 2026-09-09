# onoff_plug 安全签名配置与 BL2 公钥绑定操作说明

适用范围：BK7239N、不引入 TF-M、独立 BL2、覆盖式 OTA。分区基于 `build_tool/config/bk7239n/partitions.csv`。第 6 节的固定公钥修改已应用到 `sonoff_modify/idk_modify`。当前已切换到 AWS KMS 签名配置，操作见第 8 节；前面的本地开发密钥命令保留作为流程说明，不要重新执行它们覆盖 KMS 配置。板端启动与 OTA 尚未完成验证。

本文保留的已完成验证及公钥指纹属于此前环境的记录。当前正在重新配置环境，需重新核对实际密钥、配置和构建结果，不能直接沿用历史验证结论。

开发阶段先采用 ECDSA P-256 + SHA-256、本地开发密钥、固定发布公钥；暂不启用 Flash AES、密钥轮换和防回退。生产私钥最终留在 KMS。若已有生产 KMS 密钥，量产 BL2 应绑定它导出的公钥，不能把下述临时开发公钥作为量产信任根。

## 0. 先准备签名与 Matter 共用的 Python 环境

此前环境检查时，默认 `/usr/bin/python3` 是 Python 3.8.10，`cryptography` 是 2.8。SDK `Security` 读取真实公钥时出现 `load_pem_public_key() missing 1 required positional argument: 'backend'`。当前 [bksecure 依赖文件](../bk_openthread/bk_idk/tools/env_tools/bksecure/requirements.txt) 则明确要求 `cryptography==43.0.0`。

完整产品构建不能只安装签名工具依赖。Matter 的 [构建依赖文件](../bk_openthread/components/matter/connectedhomeip/scripts/setup/requirements.build.txt) 还包含 `python-path`、`lark`、`jinja2` 等；`from python_path import PythonPath` 中的模块来自 `python-path` 包，并不是遗漏的 SDK 源文件。当前 `codegen_paths.py` 使用了 `list[str]` 注解，Python 3.8 即使补装依赖也无法直接执行。

此前环境的 `/usr/bin/python` 是 Python 3.10.13，而 `/usr/bin/python3` 是 Python 3.8.10。原 `/home/xt/sonoff-signing/venv` 基于 3.8 创建，只安装了签名依赖。激活它会让 GN 调用该环境中的 `python`，导致当时的 `ModuleNotFoundError: No module named 'python_path'`。这些版本和验证记录属于旧环境，当前环境需要重新核对。

以下环境配置和第 7 节的构建命令均在 `build_tool` 目录执行；如果当前位于项目根目录，先执行 `cd build_tool`。使用已安装的 Python 3.10 创建新环境，并在后续构建的同一终端激活。应将 `/usr/bin/python` 换成当前机器实际的 Python 3.10 可执行文件：

```bash
/usr/bin/python --version
/usr/bin/python -m venv ./python-env
source python-env/bin/activate
python -m pip install \
    -r ../bk_openthread/bk_idk/tools/env_tools/bksecure/requirements.txt \
    -r ../bk_openthread/components/matter/connectedhomeip/scripts/setup/requirements.build.txt
python -c 'import sys, cryptography, python_path, lark, jinja2; print(sys.executable); print(sys.version); print(cryptography.__version__)'
python3 --version
python -m pip check
```

已有同时满足两套依赖、Python 版本兼容的环境时可直接复用。上述新环境位于 `build_tool/python-env`，与构建输出目录分开；删除构建输出后无需重建此环境。不要通过修改 Matter 的导入语句或补写 `python_path.py` 来绕过缺失依赖。完成后续配置及公钥核对后，再按第 7 节执行构建。

旧环境验证记录：此前曾创建 `build_secure_boot/python-env` 并安装上述两份依赖，`pip check` 通过。使用该环境重放报错的 `codegen_paths.py` 命令成功；`codegen.py --generator cpp-app` 在临时目录生成了产品的 27 个 C++ 文件，并通过预期输出清单校验。生成器因未找到合适的 `clang-format` 输出了非致命提示，退出码为 0。当时的 `security.csv` 及公钥 PEM 也可正常解析；这些检查不代表当前环境、完整固件构建或板端验签通过。

## 1. 生成独立开发密钥

以下 Bash 命令由开发者执行，密钥目录位于项目仓库外。已有密钥时不重新生成，也不覆盖。

```bash
(
    set -eu
    umask 077
    SIGN_KEY_DIR=/home/xt/sonoff-signing/onoff_plug
    mkdir -p "$SIGN_KEY_DIR"
    if [ -e "$SIGN_KEY_DIR/dev_sign_private.pem" ]; then
        echo '开发私钥已存在，请复用并核对对应公钥。' >&2
        exit 1
    fi
    if [ -e "$SIGN_KEY_DIR/dev_sign_public.pem" ]; then
        echo '开发公钥已存在，请核对对应私钥。' >&2
        exit 1
    fi
    openssl genpkey -algorithm EC -pkeyopt ec_paramgen_curve:P-256 \
        -pkeyopt ec_param_enc:named_curve \
        -out "$SIGN_KEY_DIR/dev_sign_private.pem"
    openssl pkey -in "$SIGN_KEY_DIR/dev_sign_private.pem" -pubout \
        -out "$SIGN_KEY_DIR/dev_sign_public.pem"
    openssl pkey -pubin -in "$SIGN_KEY_DIR/dev_sign_public.pem" \
        -outform DER -out "$SIGN_KEY_DIR/dev_sign_public.der"
)
```

这里公钥 DER 使用 SubjectPublicKeyInfo 编码，与 SDK `Pubkey.key_bytes()` 生成的镜像公钥一致；不能将 PEM 文本或单独的 X/Y 坐标直接用于下面的字节比较。OpenSSL 的 EC 曲线参数见 [官方 genpkey 文档](https://docs.openssl.org/3.0/man1/openssl-genpkey/)。

私钥只参与发布端签名，不加入 BL2 C 数组，也不提交到仓库。密钥更换后，需要重新生成可信公钥头文件、重新构建 BL2，并核对相应硬件信任根。

## 2. security.csv

新建 `build_tool/config/bk7239n/security.csv`：

```csv
Field,Value
bl1_secureboot_en,TRUE
flash_crc_en,TRUE
flash_aes_type,NONE
flash_aes_key,
img_sign_key_type,ec256
img_sign_pubkey,/home/xt/sonoff-signing/onoff_plug/dev_sign_public.pem
img_sign_privkey,/home/xt/sonoff-signing/onoff_plug/dev_sign_private.pem
new_img_sign_pubkey,
new_img_sign_privkey,
update_img_sign_key_en,FALSE
anti_rollback,FALSE
```

路径应填写实际绝对路径；CSV 不会展开 `$HOME` 或 `~`。空的 AES 和新密钥字段适用于这里的 `NONE`、不轮换设置，不能照搬到 `FIXED` 或开启轮换的配置。

`flash_aes_type=NONE` 表示本阶段不加密 Flash 内容，镜像仍然签名。必须与测试设备已有的 Flash AES/CRC 硬件状态匹配。`bl1_secureboot_en=TRUE` 使工具生成安全启动相关内容，不会自动烧录 OTP/eFuse。

当前 `PackAll` 将同一组 `img_sign_*` 参数传给 BL1 manifest 签名、应用签名和 OTA 外层签名，因此此开发配置使用同一组密钥跑通链路。若要分开管理 BL1 与应用发布密钥，需要进一步改造工具参数，不能只在 CSV 中添加未被读取的新字段。

参考：[Security 解析器](../bk_openthread/bk_idk/tools/env_tools/bksecure/scripts/security.py)、[PackAll](../bk_openthread/bk_idk/tools/env_tools/bksecure/scripts/pack_all.py)。

## 3. bin.csv 与 ota.csv

新建 `build_tool/config/bk7239n/bin.csv`：

```csv
Name,Type,Partition,Version,Security_counter
bl2.bin,BL2,bl2,1.0.0,1
cpu0_app.bin,CPU0_APP,primary_cpu0_app,1.0.0,1
```

构建工具会将产品 `app.bin` 复制为 `cpu0_app.bin`，不是要求你修改应用输出文件名。无 TF-M 时仅列上述两个构建输入。

同时新建 `build_tool/config/bk7239n/ota.csv`：

```csv
Field,Value
strategy,OVERWRITE
encrypt,FALSE
anti_rollback,FALSE
app_security_counter,1
app_version,1.0.0
bootloader_ota,FALSE
bootloader_version,1.0.0
```

当前 `Partitions.__parse_bin_csv()` 会使用 `ota.csv` 的 `app_version` 和 `bootloader_version` 覆盖 `bin.csv` 的 `Version`，因此两处保持一致。板级默认 `ota.csv` 的 `strategy` 是 `NONE`，也没有显式应用与 BL2 版本，不应依赖该默认文件。

上述解析路径的安全计数器仍来自 `bin.csv` 的 `Security_counter`；将 `ota.csv` 的 `app_security_counter` 同步设为 1，避免其他步骤读到不一致的值。此时各处防回退开关关闭，写入数字 1 不代表已启用 OTP 防回退。`bootloader_ota=FALSE` 表示本阶段不通过 OTA 更新 BL2。

参考：[版本覆盖实现](../bk_openthread/bk_idk/tools/env_tools/bksecure/scripts/partitions.py)、[板级默认 ota.csv](../bk_openthread/bk_idk/middleware/boards/bk7239n/csv/ota.csv)。

## 4. pack.json

新建 `build_tool/config/bk7239n/pack.json`，采用当前 overwrite 示例的结构：

```json
{
    "primary_manifest.bin": {
        "bin": ["bl2.bin"],
        "action": "BL1_SIGN"
    },
    "secondary_manifest.bin": {
        "bin": ["bl2.bin"],
        "action": "BL1_SIGN"
    },
    "bootloader.bin": {
        "bin": ["bl1_control.bin", "boot_flag.bin", "primary_manifest.bin", "secondary_manifest.bin", "bl2.bin"],
        "action": "PACK_BL1_DOWNLOAD_BIN"
    },
    "app_signed.bin": {
        "bin": ["overwrite.bin"],
        "action": "BL2_SIGN"
    },
    "all-app.bin": {
        "bin": ["partition.bin", "app_signed.bin"],
        "action": "PACK_BL1_DOWNLOAD_BIN"
    },
    "ota.bin": {
        "bin": ["app_signed.bin"],
        "action": "PACK_OTA_BIN"
    }
}
```

`overwrite.bin` 由工具根据 `ow_active` 分区和 `cpu0_app.bin` 生成。`app_signed.bin` 保护最终执行的应用，`ota.bin` 的生成还包括压缩和外层签名。这里 `all-app.bin` 不包含 `bootloader.bin` 的全部内容，首次烧录需要配套启动包；下载地址以包内描述和分区配置为准，不自行拼接二进制。

本地签名阶段不需要使用 `steps_pack.json`；后续接入 KMS 分步摘要导出、签名回填时再配置它。

## 5. 产品 config 的配套选项

在产品 `config` 中显式配置独立 BL2 验签及 OTA 选项；从普通固件迁移时，不能假定 BL2 已启用：

```ini
CONFIG_SECURITY_FIRMWARE=y
CONFIG_TFM=n
CONFIG_BL2=y
CONFIG_BL2_UPGRADE_STRATEGY="OVERWRITE_ONLY"
CONFIG_VALIDATE_IMAGE=y
CONFIG_VALIDATE_IMAGE_HASH_ONLY=n
CONFIG_BL2_SKIP_VALIDATE=n
CONFIG_BL2_VALIDATE_ENABLED_BY_EFUSE=n
CONFIG_BK_OTA=y
CONFIG_BK_OTA_NEW_PACK_TOOL=y
CONFIG_OTA_HTTP=n
CONFIG_OTA_CONFIRM_UPDATE=y
CONFIG_OTA_UPDATE_PUBKEY=n
CONFIG_OTA_BACKUP_PUBKEY=n
CONFIG_BL2_UPGRADE_WITH_APP=n
CONFIG_ANTI_ROLLBACK=n
```

`CONFIG_SECURITY_FIRMWARE=y` 是当前 SDK Makefile 选择安全构建流程的入口。[parse_build_type.py](../bk_openthread/bk_idk/tools/build_tools/parse_build_type.py) 直接读取产品 `config` 中此项，再由 `bk_prebuild.py` 选择 `bksecure`。仅设置 `CONFIG_BK_OTA_NEW_PACK_TOOL=y` 不会切换此预构建入口；缺少前者时，旧 `beken_utils` 会读取新格式 `security.csv`，报 `Field bl1_secureboot_en of security.csv not supported` 和 `KeyError: 'secureboot_en'`。应补齐构建开关，保留 CSV 的 `bl1_secureboot_en` 字段；此错误与是否使用 Python 虚拟环境无关。

此前在当前环境补齐安全构建开关后，临时目录中的预构建已通过公钥和 `security.csv` 解析，生成的 `security.h` 包含 `CONFIG_BL1_SECUREBOOT=1` 和 `MCUBOOT_SIGN_EC256=1`；当时因缺少产品 `partitions.csv` 停止。现在用户已补齐该文件，构建进入应用编译阶段，完整固件构建仍未通过。

`CONFIG_OTA_CONFIRM_UPDATE` 位于 [BL2 Kconfig](../bk_openthread/bk_idk/components/bk_mcuboot/Kconfig) 的 `depends on BL2` 菜单下。若缺少 `CONFIG_BL2=y`，即使产品配置写了 `CONFIG_OTA_CONFIRM_UPDATE=y`，它也不会进入生成的 `sdkconfig.h`。此时 `bk_init.c` 调用 `bk_ota_cancel_update(0)` 会报隐式声明错误，因为该函数的声明和实现都受 `CONFIG_OTA_CONFIRM_UPDATE` 控制。本方案应补齐 BL2 配置，不应手写 `extern`、替换为 `bk_ota_cancel()` 或关闭编译告警来绕过。重新生成配置后应确认 `CONFIG_BL2=1`、`CONFIG_OTA_CONFIRM_UPDATE=1` 以及 `CONFIG_BL2_UPGRADE_STRATEGY="OVERWRITE_ONLY"` 生效。

本次补齐独立 BL2 配置后，使用实际 SDK Kconfig 和当前构建环境在临时目录生成配置，确认上述开关生效；采用现有编译数据库中的 ARM 编译参数和新配置头文件，`bk_init.c` 单文件编译通过，保留 `-Wall -Werror`，SDK 源文件未修改。此检查不代表完整 BL2 构建或固件链接通过。

`CONFIG_VALIDATE_IMAGE=y`、`CONFIG_VALIDATE_IMAGE_HASH_ONLY=n`、`CONFIG_BL2_SKIP_VALIDATE=n` 仍需保持。`CONFIG_OTA_UPDATE_PUBKEY=n` 必须配合下一节的固定公钥绑定，不能继续使用原来的直接信任镜像公钥分支。

`CONFIG_OTA_HTTP=n` 关闭 SDK 旧 HTTP OTA 路径。`CONFIG_BK_OTA=y` 时，`modules/ota.h` 切换到新接口，不再声明旧函数 `bk_http_ota_download()`；若还保留 `CONFIG_OTA_HTTP=y`，AT OTA 代码会调用该旧接口并报隐式声明错误。Sonoff HTTP OTA 使用自己的实现，不依赖这个旧 OTA 开关；无需因此关闭 `CONFIG_HTTP` 或 AT 的其他功能。

当前 PSA mbedTLS 的 `asn1.h` 在 `CONFIG_OPENTHREAD=y` 时使用公开的 `mbedtls_asn1_buf.len/p` 字段，未启用 OpenThread 时使用私有成员名。SDK `ota_verify.c` 原来统一使用 `MBEDTLS_CONTEXT_MEMBER`，在本产品中会报 `private_len/private_p` 不存在。此前文档记录的覆盖文件在当前环境中缺失，本次已从 SDK 原文件创建 [OTA 验证覆盖文件](../sonoff_modify/idk_modify/components/bk_ota/ota_verify.c)，增加局部宏 `OTA_ASN1_MEMBER`，仅调整 `alg`、`param` 的四处 ASN.1 字段访问，保留 ECDSA 上下文的原访问方式。修改用 `sonoff modify start/end` 标记，Makefile 会在构建时自动应用。

当前环境验证：使用现有编译数据库中的 ARM 参数，原文件可重现上述报错；覆盖文件在 OpenThread 开启、以及临时配置关闭 OpenThread 两种情况下均完成单文件编译，保留 `-Wall -Werror`。SDK 原文件 SHA-256 前后一致。此检查不代表完整固件构建或镜像验签通过；可在 `build_tool` 中继续执行 `make MODEL=onoff_plug BUILD_DIR="$PWD/../build_secure_boot"`，本次源码修复无需清理构建目录。

Matter 使用独立的 GN 构建，需要单独传入安全预构建头文件目录。安全分区头 `partitions_gen.h` 会包含 `security.h`，后者生成在 `$(PROJECT_BUILD_DIR)/security`。若 Matter 编译报 `fatal error: security.h: No such file or directory`，即使该文件已生成，也应检查其编译参数。已在 [libCHIP.mk 覆盖文件](../sonoff_modify/matter_modify/libCHIP.mk) 的现有 `INCLUDES` 列表中加入 `-I$(PROJECT_BUILD_DIR)/security`，同时传给 C 和 C++ 编译。当前环境已使用 Ninja 导出的 `DeviceInfoProviderImpl.cpp` 实际 ARM 编译命令复现错误，补齐路径后单文件编译通过，依赖文件确认使用了生成的 `security/security.h`。重新执行上述构建命令会更新 GN 参数，无需手工复制头文件或清理整个构建目录；完整 Matter 库及固件链接仍待验证。

这些开关仅启用 SDK 能力，HTTP/Matter 的 Sonoff OTA 接收、写入、提交链仍需适配安全包；暂缓应用层验签不等于不需要适配下载链路。Flash CBUS 和应用 RAM 配置也需在实际构建时核对。

## 6. 将可信公钥固定在 BL2 中

此方案的信任关系为：OTP 中的 BL1 信任根保护 BL2；BL2 的已签名代码中包含固定应用发布公钥；BL2 只接受与固定公钥匹配的镜像签名。MCUboot 采用 bootloader 中的可信公钥验证签名镜像的基本方式见 [官方说明](https://docs.mcuboot.com/signed_images.html)，本项目仍应以 Beken 修改后的实现为准。

### 6.1 修改位置

实际独立 BL2 使用：

```text
bk_openthread/bk_idk/components/bk_mcuboot/bl2/components/mcuboot/src/ow_pubkey.c
```

按当前 `build_tool/Makefile` 已有的 `idk_modify → bk_idk` 映射，修改文件应维护在：

```text
sonoff_modify/idk_modify/components/bk_mcuboot/bl2/components/mcuboot/src/ow_pubkey.c
sonoff_modify/idk_modify/components/bk_mcuboot/bl2/components/mcuboot/src/sonoff_trusted_pubkey.h
```

如果覆盖目录中的 `ow_pubkey.c` 不存在，先从 SDK 原文件复制；存在则在现有修改上继续，不能覆盖它。各处改动用 `sonoff modify start/end` 标记。SDK 原文件保持不变。

不要改另一份 `components/mcuboot/boot/` 下的实现，也不要仅在 Sonoff 应用源码中放公钥；这里公钥必须进入独立 BL2 的构建。

### 6.2 生成可信公钥头文件

在项目根目录运行下面的 Python 命令，将 `security.csv` 实际配置的公钥转换成 C 常量。该命令只读公钥，不读取私钥；目标存在时拒绝覆盖，换密钥需要显式维护并重新验证。目前头文件已经生成，不需要再次执行。

```python
from pathlib import Path
from datetime import date
import csv
from cryptography.hazmat.backends import default_backend
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import ec

with Path('build_tool/config/bk7239n/security.csv').open() as config:
    settings = dict(list(csv.reader(config))[1:])
source = Path(settings['img_sign_pubkey'])
target = Path('sonoff_modify/idk_modify/components/bk_mcuboot/bl2/components/mcuboot/src/sonoff_trusted_pubkey.h')
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
with target.open('x', encoding='utf-8') as output:
    output.write('\n'.join(lines))
print(f'Generated {target}, public key DER length: {len(data)} bytes')
```

该头文件应作为经确认的发布公钥维护。不要在每次收到任意升级包后用包内公钥重新生成它；也不要将它设计为缺失时自动退回 SDK 测试公钥。

### 6.3 修改 bk_find_key()

在覆盖文件的 SDK include 列表之后增加：

```c
/* sonoff modify start */
#include "sonoff_trusted_pubkey.h"

#if CONFIG_OTA_UPDATE_PUBKEY
#error "Fixed BL2 signing key requires CONFIG_OTA_UPDATE_PUBKEY=n"
#endif
/* sonoff modify end */
```

将该文件原 `bk_find_key()` 整个函数替换为下面的固定公钥实现。保留 SDK 接口签名；不是在原有无条件成功分支之后追加检查。

```c
/* sonoff modify start */
int bk_find_key(uint8_t image_index, uint8_t *key, uint16_t key_len, uint32_t current_slot_addr)
{
    struct bootutil_key *selected_key = &bootutil_keys[0];
    FIH_DECLARE(fih_rc, FIH_FAILURE);

    selected_key->key = NULL;
    pub_key_len = 0;

    if ((key == NULL) || (key_len != sizeof(TRUSTED_PUBKEY_DER)))
    {
        BOOT_LOG_ERR("invalid signing public key length");
        return -1;
    }

    FIH_CALL(boot_fih_memequal, fih_rc, key, TRUSTED_PUBKEY_DER, sizeof(TRUSTED_PUBKEY_DER));
    if (FIH_NOT_EQ(fih_rc, FIH_SUCCESS))
    {
        BOOT_LOG_ERR("untrusted signing public key");
        return -1;
    }

    selected_key->key = key;
    pub_key_len = key_len;

    return 0;
}
/* sonoff modify end */
```

返回 0 仅表示选择可信 key slot 0；真正的 ECDSA 镜像验签仍由后续 `bootutil_verify_sig()` 完成。两项检查都必须成功。SDK 的结构体在 `MCUBOOT_HW_KEY` 分支中使用可写指针，因此这里在比较后保存原调用者的公钥缓冲区，不强制去掉可信常量的 `const`。

此改法让调用该函数的应用内层和 OTA 外层使用同一固定公钥约束。后续若要轮换应用发布密钥，需要另行设计 BL2 更新或受信任的密钥授权链，不能直接将编译检查删除后恢复信任包内公钥。

参考：[原公钥选择函数](../bk_openthread/bk_idk/components/bk_mcuboot/bl2/components/mcuboot/src/ow_pubkey.c)、[后续镜像验签](../bk_openthread/bk_idk/components/bk_mcuboot/bl2/components/mcuboot/bootutil/src/image_validate.c)。

当前修改已落到 [ow_pubkey.c 覆盖文件](../sonoff_modify/idk_modify/components/bk_mcuboot/bl2/components/mcuboot/src/ow_pubkey.c) 和 [可信公钥头文件](../sonoff_modify/idk_modify/components/bk_mcuboot/bl2/components/mcuboot/src/sonoff_trusted_pubkey.h)。绑定的是产品 `security.csv` 指定的开发公钥，DER 长度 91 B，SHA-256 为 `d6f5462a8791757aa69bd9de0ee69c23497f0b094b4692777d0ff20c9ae0085c`。SDK 原文件保持字节一致，现有 Makefile 会在构建时应用覆盖文件。

验证通过：100 项公钥选择检查，涵盖应用/OTA 地址调用、另一把有效 P-256 公钥、逐字节篡改、空指针、错误长度和失败后的状态清理；误开公钥更新宏时编译拒绝；主机及 ARM 单文件编译通过 `-Wall -Werror`。测试使用实际 SDK 的 key/FIH 头文件及默认 FIH OFF 支持代码，对未使用的平台接口提供桩头文件，因此不能替代完整 BL2 编译、镜像验签或板端测试。

## 7. 构建、验签与板端验证顺序

1. 核对 `security.csv` 公钥、私钥是否成对，以及可信头文件是否来自同一个公钥 DER。私钥不存在、公钥格式错误或签名缺失，都应停止发布。
2. 完成覆盖文件后使用独立构建目录，检查实际 BL2 编译参数、生成的 `security.h` 和 `partitions_gen.h`。当前 Makefile 退出时会恢复 SDK，已有 SDK 本地修改应先按项目覆盖机制保存。
3. 检查生成的 BL2、manifest、应用签名包和 OTA 包。`cpu0_app.bin`、`overwrite.bin` 是中间产物，不能把它们直接当作已签名发布包。
4. 在本地检查镜像摘要、签名、公钥及两层 OTA 结构，再上开发板。正例包括正常启动和升级；负例包括错误签名、缺失签名、错误发布公钥重新签出的完整有效包，以及修改后的 BL2。负例需保持非密码学 CRC 正确，才能确认失败来自安全校验。
5. 用生成的 OTP/eFuse 配置作为部署参考，按开发板当前状态核对 BL1 信任根、启用位和保护策略，再验证完整 BL1 → BL2 → 应用链。

在 `build_tool` 目录中构建，并在同一终端激活第 0 节准备的 Python 环境。首次从普通固件切换到安全固件，使用项目根目录下独立的 `build_secure_boot` 输出目录，避免复用原 `build` 目录的构建缓存：

```bash
source python-env/bin/activate
make MODEL=onoff_plug BUILD_DIR="$PWD/../build_secure_boot"
```

后续编译继续使用同一条命令。`SOC` 默认就是 `bk7239n`，可省略；`BUILD_DIR` 只指定输出目录，不是安全功能开关。安全功能由产品 `config`、签名打包配置和 BL2 覆盖文件决定，Makefile 会自动应用 `sonoff_modify` 中的覆盖文件。

原来的 `make MODEL=onoff_plug` 同样适用，但默认输出到项目根目录下的 `build`。如果选择复用该目录，从普通固件切换时先清理该产品的旧输出，再构建：

```bash
make MODEL=onoff_plug clean
make MODEL=onoff_plug
```

当前 `gen_otp.py` 将安全启动场景下的公钥摘要条目生成为 `status=false`，权限字段也含开发默认值；因此拿到 `otp_efuse_config.json` 不表示根公钥已写入或已锁定。BL1 摘要还涉及平台专用格式，不能直接把普通 `sha256sum public.der` 的文本填入 OTP。实际 OTP/eFuse 烧录需结合 BKFIL 和板端读回结果确认。

已核对 `FactoryDataProvider` 覆盖文件：旧 `BK_PARTITION_MATTER_FACTORY` 访问代码位于 `#if 0` 中，实际代码使用 Sonoff NVDM 接口（如 `snfMatterDacCertGet()`）。因此当前产品不需要为了这些失效的文本引用新增 factory 分区。此前将其列为构建阻塞项的判断已更正。

此前环境的验证范围：使用临时开发密钥检查 OpenSSL 生成与 DER 导出、可信头文件生成和字节一致性，检查 CSV 字段及 JSON 结构。SDK 带真实公钥的配置加载最初因系统 Python 的版本问题失败，之后在第 0 节记录的新环境中通过；尚未完成完整签名打包、BL2 编译或板端验签。当前环境需要重新验证，示例不是已经验证可烧录的产物。

## 8. AWS KMS 签名

若 KMS 公钥与 BL2 固定公钥相同，设备端无需理解 KMS API；变化在发布端签名流程。生产 KMS 密钥与开发密钥不同时，应在生产 BL2 和硬件部署前完成公钥切换。

不要只清空 `img_sign_privkey` 就当作完成 KMS 接入：当前工具某些缺失私钥分支会继续处理甚至生成 `NoManifest`。应接入明确的摘要导出、KMS 签名、回填及发布前验证，并在缺少有效签名时失败。

SDK 现有分步摘要导出和 `sign_hash()` 存在哈希次数不一致，不能直接提交现有导出值给 KMS。当前接入复用正常 `PackAll` / imgtool 签名路径，由 KMS 密钥适配器对完整待签内容计算一次 SHA-256，再使用 `MessageType=DIGEST` 调用 KMS，不使用旧 `steps.py` 的应用摘要导出流程。相关背景见 [迁移说明第 4.4 节](secure_boot_ota_migration.md#44-外部签名与设备验签的哈希语义存在差异)。

### 8.1 当前 AWS KMS 公钥与登录准备

2026-09-08 已收到以下单区域 KMS 密钥信息：

- 区域：`ap-southeast-2`。
- Key ARN：`arn:aws:kms:ap-southeast-2:274270970002:key/8bd54461-738c-49e2-a2a5-7af9e7b7df9f`。
- 下载的公钥：`/home/xt/aws_kms/sign/sonoff_bk7239n_public_key.pem`。
- 项目公钥副本：[aws_kms_public.pem](../build_tool/sign/aws_kms_public.pem)。

本地检查确认这是 EC P-256（`secp256r1`）公钥，SubjectPublicKeyInfo DER 长度为 91 字节，与原来的开发公钥不同。该 DER 的 SHA-256 指纹为：

```text
829cc9e6cd97319299731dbc40f06cc7aafbf0fa9253f913ace3d1dfd9b0fafd
```

此指纹用于核对公钥文件，不是可直接烧录的 `bl1_rotpk_hash`。已使用 `firmware` profile 调用该 ARN 的 KMS Sign，并用此公钥在本地验证返回签名。`security.csv` 和 BL2 可信公钥头文件现已切换到 KMS 配置。

本地开发可使用 AWS CLI v2 的控制台登录方式（需要 2.32.0 或更新版本）。在用户自己的终端执行，浏览器登录及授权码输入由用户完成：

```bash
~/.local/bin/aws login --profile firmware --region ap-southeast-2 --remote
~/.local/bin/aws sts get-caller-identity --profile firmware --region ap-southeast-2 --no-cli-pager
```

该方式适用于 AWS 根用户、IAM 用户或 IAM 联合身份；IAM Identity Center 用户使用 `aws configure sso`。IAM 用户或角色需要控制台登录 CLI 所需的 `SignInLocalDevelopmentAccess` 权限；访问签名密钥还需要相应的 `kms:DescribeKey`、`kms:GetPublicKey` 和 `kms:Sign` 权限。无需把密码、访问密钥或登录授权码写进项目或发送到聊天中。

已确认远端密钥的 `KeySpec=ECC_NIST_P256`、`KeyUsage=SIGN_VERIFY`、状态为 `Enabled`。已有可用的 `firmware` profile 时无需重新登录；后续会话过期时沿用原身份认证方式续期。

参考：[AWS CLI 控制台登录](https://docs.aws.amazon.com/cli/latest/userguide/cli-configure-sign-in.html)、[AWS KMS GetPublicKey](https://docs.aws.amazon.com/kms/latest/APIReference/API_GetPublicKey.html)。

### 8.2 从 build_tool 编译 KMS 签名固件

当前 `security.csv` 的两个签名字段为：

```csv
img_sign_pubkey,/home/xt/sonoff/bk7239n/sonoff_openthread/build_tool/sign/aws_kms_public.pem
img_sign_privkey,/home/xt/sonoff/bk7239n/sonoff_openthread/build_tool/sign/aws_kms_signer.json
```

这里的 JSON 是本项目新增的签名器描述文件，包含 provider、Key ARN、区域、profile 和公钥文件路径，不含私钥或 AWS 访问凭据。SDK 镜像补丁使 `img_sign_privkey` 支持此描述文件；原始 SDK 不支持把这个字段直接写成 ARN。相关补丁位于 `sonoff_modify/idk_modify/tools/env_tools/bksecure/`，由 `build_tool/Makefile` 在构建时应用。

BL2 公钥头文件已更新。以后确实更换构建公钥时，先更新配置和描述文件，再显式重新生成头文件（此操作不更改已烧录的 OTP）：

```bash
python3 build_tool/sign/convert_sign.py --replace
```

正常重新编译不需要重新生成密钥或头文件。在 `build_tool` 中执行：

```bash
make MODEL=onoff_plug BUILD_DIR="$PWD/../build_secure_boot"
```

打包阶段通过 `firmware` profile 调用 KMS，分别签署主/备 BL1 manifest、应用内层镜像和压缩 OTA 外层镜像。需要能够访问 AWS 的网络环境。每次返回的签名都使用固定本地公钥校验；KMS 拒绝调用、网络失败、公钥不匹配或签名无效会停止打包，不回退到开发私钥。

BL1 回填复用 `secure_boot_tool`：先生成 manifest 摘要，再将 KMS DER 签名解码为 `r`、`s`。本版本工具的输入文本要求先 `r` 后 `s`，与旧 `steps.py` 的变量标注不同；生成的 manifest 会再次在本地验签。

产物目录为 `build_secure_boot/bk7239n/onoff_plug/package/`。`bootloader.bin` 包含绑定 KMS 公钥的 BL2 与配套 manifest；`all-app.bin` 为应用烧录包；`ota.bin` 为应用升级包，不包含 BL2。

离线回归检查（不会调用 AWS）：

```bash
python3 build_tool/sign/test_aws_kms_sign.py
```

OTP/eFuse 仍需单独核对。生成 JSON 中公钥摘要和 LCS 的 `status` 默认仍为 `false`；本次 KMS 接入没有启用这些写入项，也未解决前文记录的芯片 eFuse 位定义差异。编译与签名验证成功不等于硬件安全启动已经部署或板端 OTA 已通过验证。

### 8.3 本次验证记录（2026-09-08）

- 从 `build_tool` 执行上述 `make` 命令，完整构建和真实 KMS 签名打包通过。
- 6 项离线测试通过，覆盖单次 SHA-256、AWS 调用失败、错误签名公钥、响应 Key ARN/算法不符、摘要长度错误和曲线错误。
- 独立解析最终主/备 manifest，验证 ECDSA 签名、公钥以及其中的 BL2 摘要；检查编译后的 BL2 包含 KMS 公钥。
- 独立解析应用镜像和压缩 OTA 外层镜像，核对受保护的公钥、KEYHASH、镜像 SHA-256 和 ECDSA 签名。
- 核对 `bootloader.bin`、`all-app.bin` 中相应分区的实际载荷与对应中间产物一致，`ota.bin` 中的载荷与已验签的外层镜像一致。
- SDK 镜像目标文件已恢复为构建前的内容；持久源码修改保存在 `sonoff_modify` 中。

当前生成 JSON 中的摘要如下（保留 SDK 输出字节序）：

```text
bl1_rotpk_hash: e8858d902a3a89063be26775d8879b192f628e03bd40944c0a5b108550847cc4
bl2_rotpk_hash: e6c99c82923197cdbc1d7399c76cf040faf0fbaa13f95392dfd1e3acfdfab0d9
```

二者及 LCS 的 `status` 仍为 `false`。本次没有烧录 Flash、OTP 或 eFuse；实际设备启动、升级与失败恢复仍待验证。
