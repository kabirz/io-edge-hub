# io-edge-hub 上位机

io-edge-hub 工业边缘节点的 Windows 调试工具。原生 Win32 C GUI, 3 个 tab:

- **Tab1 UDP 参数配置**: 发现设备, 配置网络/Modbus(RS485)/CAN 参数, 查询版本, 重启, 出厂重置.
- **Tab2 固件升级**: 通过 UDP 或 PCAN-USB(CAN) 升级 MCUboot 签名镜像.
- **Tab3 Modbus 调试**: 以 Modbus 主机 (TCP/RTU) 操作每个寄存器, 每路 DI/DO/AI 单独查看与控制.

## 构建

需要 CMake ≥ 3.25。两个工具链可选:

### MSVC (Windows 原生)

前置: 已安装 Visual Studio (含 C++ 工具集)。

```
cmake --preset vs
cmake --build out --config Release
```

产物: `out/bin/Release/io-edge-hub.exe`

### MinGW (Linux 交叉编译)

```
cmake --preset mingw
cmake --build build --config Release
```

产物: `build/bin/io-edge-hub.exe`

或用 `tools/build.bat` 一键 MSVC 构建:

```
tools\build.bat
```

## 协议参考

固件权威源: `app/apps/applications/io-edge-hub/` 与 `app/apps/libs/udp_fw_upgrade|can_fw_upgrade`。
设计文档: `docs/superpowers/specs/2026-08-11-host-tool-design.md`。

## CAN 升级依赖

需安装 PCAN-Basic 驱动 (PCANBasic.dll 运行时动态加载)。
