# 项目说明与文档导航

本目录说明 BK7239N 项目的构建、OTA、安全启动、产测接口和配置存储。建议先阅读编译说明，再按关注的功能查阅详细文档。

## 根目录结构

| 文件夹 | 用途 |
| --- | --- |
| `bk_openthread/` | 原厂 SDK 子仓库，包含 BK IDK、Matter、OpenThread 等依赖 |
| `sonoff/` | 公共功能模块，包括启动、CLI、Wi-Fi、HTTP、OTA 和配置存储 |
| `project/` | 各产品的业务代码与配置，当前包含 `onoff_plug` 项目 |
| `sonoff_modify/` | SDK 适配修改的镜像目录，编译时覆盖到对应 SDK 路径，结束后恢复 SDK |
| `build_tool/` | 编译入口、普通/安全配置、固件打包及签名脚本；测试放在 `tests/` 子目录 |
| `build/` | 编译生成的文件，`secure/` 存放安全构建，`out/` 汇总烧录档和升级包 |
| `thirty_part/` | 第三方组件，当前包含 LVGL |
| `docs/` | 编译、升级、产测和模块使用说明 |
| `.vscode/` | VS Code 工作区设置 |

## 文档索引

| 文档 | 内容 |
| --- | --- |
| [编译说明](build.md) | 环境准备、普通/安全编译、`release` 与导出文件命名 |
| [私有项目说明](private_project.md) | 正常/产测入口、`src/inc/matter` 目录职责与编译接入 |
| [OTA 升级](ota.md) | HTTP/Matter 打包顺序、封装与解密、接收和安装流程 |
| [安全启动](secure_boot.md) | AWS KMS 签名、设备验签、密钥关系与验证范围 |
| [产测 AT 手册](factory_at_commands.md) | 产测进入、生产数据写入、校验及重试规则 |
| [AT 指令速查](at.md) | 命令格式、参数和应答 |
| [Matter 生产数据](matter_factory_data.md) | Matter 接口与生产数据的来源映射 |
| [NVDM 配置持久化](nvdm.md) | 配置存储、私有项目扩展与清理范围 |
| [网络与 Wi-Fi](network_wifi.md) | STA/AP、HTTP 服务启动、扫描与状态通知 |

文档按当前代码描述已实现功能和限制。主机回归覆盖 OTA 解析、加解密、打包导出及签名适配；HTTP/Matter 实际传输、重启安装和硬件安全启动仍需板端验证。`release` 只控制导出文件的 `FACTORY` 命名标记。
