# 私有项目说明

私有项目位于 `project/<MODEL>/`，用于存放各产品的业务代码、配置和 Matter 适配。公共能力放在 `sonoff/`，编译时通过 `MODEL` 选择一个项目，当前以 `onoff_plug` 为例。

## 项目入口

系统从 [app_main.c](../sonoff/entry/app_main.c) 的 `main()` 创建应用任务，执行 `user_app_main()`，再进入 [sonoffEntry()](../sonoff/entry/sonoff_entry.c)。公共入口初始化 NVDM、应用 BASE MAC、初始化 CLI，再根据 License 和产测握手结果选择运行模式。

| 模式 | 私有项目入口 | 当前作用 |
| --- | --- | --- |
| 正常运行 | [sonoff_private_device.c](../project/onoff_plug/src/sonoff_private_device.c) 中的 `snfPrivateDeviceStart()` | 初始化插座 GPIO，并恢复保存的开关状态 |
| 产测 | [sonoff_private_factory.c](../project/onoff_plug/src/sonoff_private_factory.c) 中的 `snfPrivateFactoryStart()` | 输出产测模式提示并创建产测任务，当前任务主体预留待实现 |

正常运行的调用顺序为：`snfMainInit()` → `snfPrivateDeviceStart()` → `ChipTest()`（启用 Matter 时）→ `bk_openthread_init()`。其中 `ChipTest()` 位于项目的 `matter/src/chipinterface.cpp`，负责 Matter 初始化。产测分支不执行这条正常启动流程。

私有启动入口应在完成初始化或创建任务后返回，持续运行的业务放到任务中，避免阻塞后续启动。`snfPrivateDeviceStop()` 提供业务反初始化接口，当前启动流程未调用它。

入口声明统一放在公共的 [sonoff_private_device.h](../sonoff/private/sonoff_private_device.h) 和 [sonoff_private_factory.h](../sonoff/private/sonoff_private_factory.h)，具体实现由所选项目提供。

## 目录职责

以下路径均相对于 `project/onoff_plug/`：

| 目录 | 内容与用途 |
| --- | --- |
| `src/` | 产品 C 业务实现，包括正常/产测入口、硬件控制和私有配置读写。当前 `sonoff_plug_handle.c` 控制插座，`sonoff_private_item.c` 读写开关状态 |
| `inc/` | 产品头文件与配置。`sonoff_project_config.h` 定义型号、固件命名、版本、Matter 标识及功能开关；`sonoff_private_item.h` 定义私有 NVDM 项及默认值；业务头文件声明产品接口 |
| `matter/` | 产品专属的 Matter 适配与设备模型，按源码、头文件和模型配置继续划分 |
| `matter/src/` | Matter C++ 实现。`chipinterface.cpp` 初始化 Matter 服务；`DeviceCallbacks.cpp` 将属性变化转为产品业务调用，并提供属性上报接口 |
| `matter/inc/` | Matter 适配头文件，当前 `DeviceCallbacks.h` 声明设备事件和属性回调 |
| `matter/zap/` | Matter 端点、设备类型、Cluster 和属性配置。`BUILD.gn` 使用 `matter_dev.zap` 生成数据模型代码，目录中同时保存 `matter_dev.matter` 模型描述 |

产品硬件控制和状态存储放在 `src/`；Matter 回调通过产品接口调用这些能力。例如当前 OnOff 属性变化通过 `snfPlugOnOffRawSet()` 控制 GPIO 并保存状态。私有存储接入见 [NVDM 配置持久化](nvdm.md)。

## 编译接入

在项目根目录进入 `build_tool` 后选择项目：

```bash
cd build_tool
make MODEL=onoff_plug
```

[build_tool/Makefile](../build_tool/Makefile) 将所选项目的 `src/`、`inc/` 和 `matter/` 接入构建目录：

- `src/` 当前层的 `*.c` 与公共 `sonoff/` 源码一起编译，`inc/` 加入头文件搜索路径。
- `matter/src/` 当前层的 `*.cpp`、`*.cc` 由 GN 收集，结合 `matter/inc/` 和 `matter/zap/` 构建 Matter 应用库。

这两个项目源码目录目前不递归收集子目录文件。新增源码放在对应目录当前层；如需继续分层，应同步调整构建规则。普通/安全编译和产物说明见 [编译说明](build.md)。
