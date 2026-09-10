# 编译说明

以 `onoff_plug` 为例，默认芯片为 `bk7239n`。编译时先进入 **`build_tool` 目录**，再执行 `make`。

前置条件：

- Ubuntu 22.04 或兼容 Linux，已安装 Git、Make、GCC/G++、CMake、Ninja、GN 和 Python 3.10。系统依赖见 [SDK 环境说明](../bk_openthread/docs/OpenThread/zh_CN/get-started/index.rst)。
- ARM 工具链 `gcc-arm-none-eabi-10.3-2021.10`，默认安装目录为 `/opt/gcc-arm-none-eabi-10.3-2021.10/bin`。
- 保留仓库中的 `sonoff/ota/sonoff_ota_key.h`，普通和安全构建均用它生成带封装的 OTA 包。

首次从项目根目录准备源码，然后进入 `build_tool` 配置 Python 环境：

```bash
git submodule update --init --recursive
cd build_tool
python3.10 -m venv python-env
source python-env/bin/activate
python -m pip install \
    -r ../bk_openthread/bk_idk/tools/env_tools/bksecure/requirements.txt \
    -r ../bk_openthread/components/matter/connectedhomeip/scripts/setup/requirements.build.txt \
    future PyYAML stringcase
```

以后打开新终端时，先进入 `build_tool`，再执行 `source python-env/bin/activate` 激活环境。

普通编译（默认 `SECURE=0`）：

```bash
make MODEL=onoff_plug
```

安全编译还需安装支持 `aws login` 的 AWS CLI v2（2.32.0 或以上），准备具有目标密钥 `kms:Sign` 权限的 `firmware` profile，并保留 `build_tool/sign/` 中的签名配置和公钥。首次登录或会话过期时执行前两条命令，身份验证成功后再编译：

```bash
aws login --profile firmware --region ap-southeast-2 --remote
aws sts get-caller-identity --profile firmware --region ap-southeast-2 --no-cli-pager
make MODEL=onoff_plug SECURE=1
```

登录时按提示打开浏览器，将授权码填回终端。遇到 `Your session has expired` 时重新登录即可。

发布用途在编译命令后加 `release`，仅将导出文件名的用途改为 `FACTORY`；默认是 `TEST`，用于测试，不用于生产：

```bash
make MODEL=onoff_plug release
make MODEL=onoff_plug SECURE=1 release
```

| 编译方式 | 产物目录（相对项目根目录） | 外层 OTA 打包输入 |
| --- | --- | --- |
| 普通 | `build/bk7239n/onoff_plug/package/` | `app_pack.rbl` |
| 安全 | `build/secure/bk7239n/onoff_plug/package/` | `ota_raw.bin` |

编译后自动按 `pack_ota.py → pack_matter_ota.py` 顺序打包：`ota_encrypted.bin` 用于 HTTP 等升级方式，`ota_matter.ota` 用于 Matter 升级。Matter 包的 VID/PID、数字版本和版本字符串读取项目头文件中的 `SONOFF_MATTER_*` 配置，VID/PID 必须与设备一致。

烧录档和两种升级包另行复制到 `build/out/flash/`、`build/out/http/`、`build/out/matter/`，保留原始后缀。文件名取自项目头文件，例如：

```text
FWSW-01-SWITCH-BK7239N-1.1.2-20260910.164353.911-TEST.bin
```

格式为 `FW<类别>-<编号>-<功能摘要>-<主控芯片>-<版本号>-<时间戳>-<用途>.<后缀>`。同次构建共用开始编译时的本地时间戳，精确到毫秒；原始 `package/` 产物保留。

安全固件首次烧录还需配套的 `package/bootloader.bin`；`build/out/flash/` 复制的是 `all-app.bin` 应用烧录包，不包含完整启动包。部署说明见 [安全启动](secure_boot.md)。

清理命令同样在 `build_tool` 目录执行：

```bash
make MODEL=onoff_plug clean           # 清理该项目的普通构建
make MODEL=onoff_plug SECURE=1 clean  # 清理该项目的安全构建
make all clean                       # 执行 clean 后删除根目录整个 build/，包括安全构建和 build/out/
```

更换源码目录或编译环境后重新构建，避免沿用旧构建缓存。更多说明见 [OTA](ota.md) 和 [安全启动](secure_boot.md)。
