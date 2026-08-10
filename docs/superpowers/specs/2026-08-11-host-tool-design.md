# io-edge-hub 上位机设计文档

- 日期：2026-08-11
- 作者：kabirz
- 状态：已确认（待 writing-plans 阶段细化）
- 上位机代码位置：`C:\Users\jxwaz\code\io-edge-hub`（独立 git 仓库，与 `~/code/app`、`~/code/handler-receiver` 同级）
- 固件代码位置：`C:\Users\jxwaz\code\app\apps\applications\io-edge-hub`（Zephyr RTOS app，设计文档 v3.3）
- 代码风格参考：`C:\Users\jxwaz\code\handler-receiver`（Win32 原生 C GUI 上位机）

## 1. 背景与目标

io-edge-hub 是运行在 STM32F407VET6 上的 Zephyr RTOS 工业 IO 边缘节点（16 路 DI / 8 路 DO / 4 路 AI），通过以太网提供 Modbus TCP 从机、通过 RS485 提供 Modbus RTU 从机，并具备 UDP（端口 8600）远程参数配置、UDP/CAN 双通道远程固件升级等运维能力。

本上位机是 Windows PC 端调试工具，供现场工程师：

1. **Tab1 UDP 参数配置**：发现设备、配置网络/Modbus/CAN 参数、查询版本、重启、出厂重置。
2. **Tab2 固件升级**：通过 UDP 或 CAN（PCAN-USB）对设备进行 MCUboot 镜像升级。
3. **Tab3 Modbus 调试**：以 Modbus 主机身份（TCP 或 RTU 二选一）逐个操作每个寄存器，每个 DI/DO/AI 都可单独查看与控制。

## 2. 约束与原则

- **语言/工具链**：C11 + CMake ≥ 3.25 + CMakePresets，双工具链（MSVC 原生 + MinGW 交叉），与 handler-receiver 完全一致。
- **UI 框架**：Win32 原生（comctl32 tab control + 子对话框），Windows only。
- **依赖**：零三方包管理。PCAN-USB 通过 `LoadLibrary` 动态加载 `PCANBasic.dll`；其余仅系统库（`comctl32 comdlg32 gdi32 ws2_32 iphlpapi`）。不引入 vcpkg/conan/FetchContent/find_package。
- **代码风格**：严格对齐 handler-receiver（详见 §4）。
- **不改固件**：所有协议按 io-edge-hub 固件 v3.3 现状实现，本工具不修改固件。
- **YAGNI**：不做多设备批量、固件签名验签、自动测试、CI/Release、i18n。

## 3. 项目骨架与构建

### 3.1 目录结构（flat，对齐 handler-receiver）

```
io-edge-hub/
├── CMakeLists.txt              # 单根 CMake, VERSION 来自 project()
├── CMakePresets.json           # 两个 configure preset: vs(MSVC) + mingw(Ninja 交叉)
├── README.md
├── .gitignore                  # build/, out/, .zcode/, .superpowers/
├── include/                    # .h, 1:1 对应 src
│   ├── udp_manager.h           # UDP 8600 配置 + UDP 固件升级
│   ├── can_manager.h           # PCAN-USB + CAN 固件升级
│   ├── fw_image.h              # MCUboot .bin 校验 + keyhash 提取 + CRC16-CCITT
│   ├── pcan_loader.h           # PCANBasic.dll 动态加载
│   ├── modbus_client.h         # 【新】Modbus TCP/RTU 主机
│   ├── config_tab.h            # tab1
│   ├── upgrade_tab.h           # tab2
│   ├── modbus_tab.h            # tab3
│   ├── resource.h              # 控件 ID + 字符串
│   └── app.h                   # 全局公共(MainWindow/Tab 框架、状态栏、日志)
├── src/
│   ├── main.c                  # WinMain + 主窗口 + tab control
│   ├── udp_manager.c
│   ├── can_manager.c
│   ├── fw_image.c
│   ├── pcan_loader.c
│   ├── modbus_client.c
│   ├── config_tab.c            # tab1 UI + 业务
│   ├── upgrade_tab.c           # tab2 UI + 业务
│   └── modbus_tab.c            # tab3 UI + 业务
├── resources/
│   ├── resource.rc             # Windows 版本信息 + 图标
│   └── app.ico
└── tools/
    └── build.bat               # 一键 MSVC 构建(可选)
```

### 3.2 构建（CMakePresets.json）

- `vs` preset：MSVC 原生，`binaryDir: out`
- `mingw` preset：`x86_64-w64-mingw32-gcc` 交叉 + Ninja，`binaryDir: build`
- `project(io-edge-hub VERSION 0.1.0 LANGUAGES C RC)`，`add_executable(... WIN32 ...)`（GUI 子系统）
- `target_compile_definitions`：`UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS`
- `target_link_libraries`：`comctl32 comdlg32 gdi32 ws2_32 iphlpapi`
- MinGW 额外：`-Os -finput-charset=UTF-8 -fexec-charset=GBK` + link `-mwindows -static-libgcc -s`
- MSVC 额外：`/utf-8` + `/SUBSYSTEM:WINDOWS`
- 输出：`bin/io-edge-hub.exe`
- 版本宏 `APP_VERSION_MAJOR/MINOR/PATCH` 注入 `resource.rc` 的 `VS_VERSION_INFO`

## 4. 代码风格（对齐 handler-receiver）

- C11、Tab 缩进、K&R 大括号（`if/for/while/函数` 左括号同行）；`switch` case 不相对 switch 缩进
- 公共 API：`Module_Action`（PascalCase 模块前缀）；静态 helper：`snake_case`
- 类型 PascalCase typedef；opaque struct 在 `.h` forward declare、body 在 `.c`（`typedef struct X X;`）
- 枚举：`enum lowercase { ALL_CAPS, };` 带 trailing comma；值风格 `MODULE_PURPOSE`
- 全局 `g_` 前缀；Win32 句柄匈牙利（`g_hMain`、`g_hInst`）
- 宏/常量：`ALL_CAPS_WITH_UNDERSCORES`（`#define` 或 enum）
- 注释：中文、`/* */`、`/* ===== banner ===== */` 分隔；**不加文件头 license 注释**（与 handler-receiver 一致）
- 错误处理：返回 `bool` + nullable out 参数；失败内部记录最近错误，`GetLastError()` 取中文；用户反馈用 `MessageBoxW(MB_OK | MB_ICONERROR/WARNING/INFORMATION)`
- 日志：底部多行只读编辑框 append 时间戳 + 中文消息；模块可选 `msg_cb` 回调
- 字符串：全 UNICODE 构建，UI 用 `L"..."` + `*W` API；协议字节流与 UI 间用 `swprintf`/`WideCharToMultiByte` 转换

## 5. 通信层模块（不依赖 UI）

5 个模块。复用关系：

| 模块 | 来源 | 适配说明 |
|---|---|---|
| `pcan_loader` | handler-receiver **原样复用** | PCANBasic.dll 动态加载，与协议无关 |
| `fw_image` | handler-receiver **原样复用** | MCUboot TLV 解析、keyhash 提取、CRC16-CCITT 全通用 |
| `udp_manager` | handler-receiver 复用 + 扩展 | 0x01-0x05 升级帧格式相同直接复用；新增 io-edge-hub 0x10-0x1B 配置命令 |
| `can_manager` | handler-receiver 复用 + 适配 | 帧 ID 改为 0x101-0x105（io-edge-hub 协议） |
| `modbus_client` | **全新** | Modbus TCP/RTU 主机 |

### 5.1 `udp_manager` — UDP 8600（配置 + UDP 固件升级）

同一 socket（本地 INADDR_ANY:随机源端口）承担两类功能：tab1 配置（cmd 0x10-0x1B）+ tab2 UDP 固件升级（cmd 0x01-0x05）。

```c
typedef struct UdpManager UdpManager;   /* opaque */
typedef void (*udp_msg_cb)(const char *msg, void *user);

UdpManager *UdpManager_Create(udp_msg_cb cb, void *user);
bool UdpManager_Open(UdpManager *m);
void UdpManager_Destroy(UdpManager *m);

/* tab1 配置命令 (0x10+, 大端) */
bool UdpManager_Discover(UdpManager *m, char *out_info, int out_len);   /* 0x18 广播, 监听 8601 收跨网段回复 */
bool UdpManager_SetIp(UdpManager *m, uint32_t ip_be, uint8_t *out_ok);  /* 0x10 */
bool UdpManager_GetNet(UdpManager *m, uint32_t *out_ip, uint8_t *out_slave, uint16_t *out_port); /* 0x11 */
bool UdpManager_SetModbus(UdpManager *m, uint8_t slave_id, uint16_t baud, uint8_t *out_ok);      /* 0x12 */
bool UdpManager_GetModbus(UdpManager *m, uint8_t *out_slave, uint16_t *out_baud);                /* 0x13 */
bool UdpManager_SetCan(UdpManager *m, uint16_t can_id, uint16_t baud_k, uint8_t *out_ok);        /* 0x16 */
bool UdpManager_GetCan(UdpManager *m, uint16_t *out_id, uint16_t *out_baud_k);                   /* 0x17 */
bool UdpManager_FactoryReset(UdpManager *m, uint8_t *out_ok);   /* 0x19 */
bool UdpManager_GetVersion(UdpManager *m, char *out_ver, int len); /* 0x04 */
bool UdpManager_Reboot(UdpManager *m);                          /* 0x05 */

/* tab2 UDP 固件升级 (0x01-0x05, 小端) */
bool UdpManager_FwStart(UdpManager *m, uint32_t img_size, const uint8_t keyhash[32],
                        uint8_t *out_status);  /* 0x01, status: 0=失败 1=已启动 2=keyhash不匹配 */
bool UdpManager_FwData(UdpManager *m, const uint8_t *data, int len, uint32_t *out_offset); /* 0x02, len<=511 */
bool UdpManager_FwEnd(UdpManager *m, uint8_t test, uint16_t crc16, uint8_t *out_result);   /* 0x03 */
```

实现要点：

- 每条命令内部 `sendto(目标 IP:8600)` + `recvfrom` 超时等待回复，默认超时 1s。DISCOVER 用广播发送 + 额外监听 8601 收跨网段回复。
- 回复校验：第 1 字节必须 == 发送的 cmd，否则视为无响应。
- **端序严格按协议**：0x10+ 大端；0x01-0x05 的 size/offset/crc 小端。模块内部封装好，UI 层只看 native int。
- `UdpManager_CRC16_CCITT(data, len)` 与 Zephyr `crc16_ccitt` 完全一致（poly 0x1021, init 0x0000），原样复用 handler-receiver。

### 5.2 `can_manager` — PCAN-USB + CAN 固件升级

```c
typedef struct CanManager CanManager;
typedef void (*can_progress_cb)(int percent, void *user);

bool CanManager_DetectDevice(CanManager *m, uint32_t *out_hw_type);
bool CanManager_Connect(CanManager *m, uint32_t channel, uint32_t bitrate); /* 默认 250000 */
bool CanManager_FirmwareUpgrade(CanManager *m, const uint8_t *img, uint32_t size,
                                const uint8_t keyhash[32], /* 可为 NULL 跳过 */
                                bool permanent,
                                can_progress_cb progress, void *user);
bool CanManager_GetVersion(CanManager *m, char *out_ver, int len);
bool CanManager_Reboot(CanManager *m);
const char *CanManager_GetLastError(CanManager *m);
```

io-edge-hub CAN 协议（全部 8B DLC，32 位字段小端）：

| 帧 ID | 方向 | 用途 |
|---|---|---|
| 0x101 | 主机→设备 | 命令：`data_32[0]`=cmd(0=START/1=CONFIRM/2=VERSION/3=REBOOT), `data_32[1]`=arg |
| 0x102 | 设备→主机 | 回复：code(0=OFFSET/1=UPDATE_SUCCESS/2=VERSION/3=CONFIRM/4=FLASH_ERROR/5=TRANSFER_ERROR/6=KEYHASH_ERROR), offset |
| 0x103 | 主机→设备 | 固件数据（最多 8B） |
| 0x104 | 主机→设备 | keyhash 分片：seq(0..4) + 7B，共 5 帧 35B 携带 32B SHA-256 |
| 0x105 | 设备→主机 | 版本字符串分片：seq + 7B ASCII，末帧 NUL 补齐 |

升级流程（worker 线程内顺序执行）：

1. （可选）发 5 帧 keyhash（0x104，seq 0..4）。
2. START（0x101 cmd=0，arg=image_size）→ 等 0x102 code=OFFSET(0) 或 FLASH_ERROR/KEYHASH_ERROR。
3. 流式 DATA（0x103，8B/帧）→ 每 64B 设备回一次 OFFSET 做流控；`fw_written==total` 时回 UPDATE_SUCCESS。
4. CONFIRM（0x101 cmd=1，arg=permanent?1:0）→ 等 code=CONFIRM(offset=0x55AA55AA) 或 TRANSFER_ERROR。
5. （可选）VERSION / REBOOT。

### 5.3 `fw_image` — MCUboot .bin 处理（原样复用 handler-receiver 的 `fw_image.{c,h}`）

handler-receiver 原样提供两个函数（`fw_image.h` 已定义）：

```c
bool fw_image_validate_header(const uint8_t *data, size_t len);   /* magic 0x96f3b83d + TLV info 0x6907 */
bool fw_image_extract_keyhash(const uint8_t *data, size_t len, uint8_t out[32]); /* TLV tag 0x0001, 32B */
```

keyhash 从 .bin 自身的 MCUboot TLV 区解析（tag `IMG_TLV_KEYHASH`=0x0001，32 字节），**不需要任何 .pem 文件**。这与固件端 MCUboot 引导时的 keyhash 校验逻辑一致。

补充说明（非 fw_image 模块）：

- **文件读取**：handler-receiver 的 `fw_image` 只做内存缓冲校验，不读文件。本工具在 `upgrade_tab.c`（UI 层）用 Win32 `CreateFileW`/`ReadFile` 把 .bin 整段读入堆缓冲，再传给 `fw_image_validate_header`/`fw_image_extract_keyhash`。
- **CRC16-CCITT**：handler-receiver 把它放在 `udp_manager` 中（`UdpManager_CRC16_CCITT`，与 Zephyr `crc16_ccitt` 对齐，poly 0x1021 init 0x0000），**不在 fw_image**。本工具沿用同一位置——`UdpManager_CRC16_CCITT`，UDP 升级 `FwEnd` 前调用它算整镜像 CRC。

### 5.4 `modbus_client` — Modbus TCP/RTU 主机（全新）

手撸 Modbus ADU/PDU，不引入 libmodbus。

```c
typedef enum { MB_TCP, MB_RTU } MbTransport;
typedef struct MbClient MbClient;

MbClient *MbClient_Create(void);
void MbClient_Destroy(MbClient *m);

bool MbClient_ConnectTcp(MbClient *m, uint32_t ip_be, uint16_t port, uint8_t unit_id); /* 端口默认 502 */
bool MbClient_ConnectRtu(MbClient *m, const wchar_t *com_port, uint32_t baud, uint8_t unit_id); /* 8N1 */
void MbClient_Disconnect(MbClient *m);
bool MbClient_IsConnected(const MbClient *m);
MbTransport MbClient_GetTransport(const MbClient *m);
const char *MbClient_GetLastError(const MbClient *m);

bool MbClient_ReadCoils(MbClient *m, uint16_t addr, uint16_t qty, uint8_t *out_bits);          /* FC01 */
bool MbClient_ReadDiscreteInputs(MbClient *m, uint16_t addr, uint16_t qty, uint8_t *out_bits); /* FC02 */
bool MbClient_ReadHolding(MbClient *m, uint16_t addr, uint16_t qty, uint16_t *out_regs);       /* FC03 */
bool MbClient_ReadInput(MbClient *m, uint16_t addr, uint16_t qty, uint16_t *out_regs);         /* FC04 */
bool MbClient_WriteSingleCoil(MbClient *m, uint16_t addr, bool on);                            /* FC05 */
bool MbClient_WriteSingleReg(MbClient *m, uint16_t addr, uint16_t value);                      /* FC06 */
bool MbClient_WriteMultiReg(MbClient *m, uint16_t addr, uint16_t qty, const uint16_t *values); /* FC16 */
```

实现要点：

- **TCP ADU**：`[MBAP: tid(2)+pid(2)=0+len(2)+uid(1)][PDU: fc(1)+...]`，端口 502，超时 1s。
- **RTU ADU**：`[addr(1)][PDU][CRC16(2)]`，CRC16-Modbus（poly 0xA001, init 0xFFFF）。串口用 Win32 `CreateFile(L"\\\\.\\COMn")` + `DCB` 配 8N1，3.5 字符帧间隔用超时模拟。
- 同步阻塞：每次请求等响应，超时返回 false + `GetLastError`。异常响应（fc | 0x80）解析 exception code 映射中文。

## 6. UI 设计（三个 Tab）

主窗口：`CreateWindowExW(WS_EX_APPWINDOW, ..., L"io-edge-hub 上位机", WS_OVERLAPPEDWINDOW)`，固定约 720×560，内嵌 `WC_TABCTRL` 占满客户区，底部状态栏。每个 tab 一个无模式子对话框，切换时显隐。

### 6.1 Tab1 UDP 参数配置

```
┌─ UDP 参数配置 ────────────────────────────────────────┐
│ 设备发现                                              │
│  [发现设备]    设备列表(下拉):                        │
│                io-edge-hub 192.168.12.101 v0.1.0_6e199a│
│                [选中 → 自动填充目标 IP]               │
│                                                       │
│ 目标设备 IP  [192].[168].[012].[101]  [查询版本][重启]│
│ 固件版本     v0.1.0_6e199a (只读)                     │
│                                                       │
│ ── 网络参数 ──                                        │
│ 新 IP        [192].[168].[012].[102]   [应用]         │
│ Modbus从机ID [1]  TCP端口(只读) 502                   │
│                                                       │
│ ── Modbus 参数(RS485) ──                              │
│ RS485 从机ID [1]  波特率[9600▼]      [应用] [读取]    │
│                                                       │
│ ── CAN 参数 ──                                        │
│ CAN 帧 ID    [0x0111]  波特率(k)[250] [应用] [读取]   │
│                                                       │
│ [出厂重置]                                            │
│                                                       │
│ 操作日志(只读): [13:01:22] 发现设备 ...               │
└────────────────────────────────────────────────────────┘
```

行为：

- **发现设备**：广播 `0x18` 到 255.255.255.255:8600 + 监听 8601（跨网段回复），所有响应填下拉框；选中自动填充目标 IP。
- **应用**：调对应 `Set_*` API，依 `out_ok` 显示成功/失败；SET_IP 成功提示"设备将在 1s 后重启"。波特率下拉（4800/9600/19200/38400/57600/115200）。
- **读取**：调 `Get_*`，把当前值回填输入框。
- **查询版本**：`0x04`，显示到只读标签。
- **重启**：`0x05`。
- **出厂重置**：`0x19`，二次确认（擦 storage 分区 + 冷启动）。

### 6.2 Tab2 固件升级

```
┌─ 固件升级 ────────────────────────────────────────────┐
│ 升级通道  ( ) UDP  ( ) CAN                            │
│                                                       │
│ [UDP 选中时] 目标设备 IP [192.168.012.101]            │
│              测试模式   [ ] 临时升级(下次启动回滚)    │
│                                                       │
│ [CAN 选中时] PCAN 设备 [USB-CHANNEL 1 ▼] 波特率[250000]│
│              测试模式   [ ] 临时升级                  │
│                                                       │
│ 镜像文件   [____________ .bin] [浏览...]              │
│ 镜像信息   magic=0x96f3b83d size=187KB keyhash=OK     │
│            (校验失败红字"非 MCUboot 镜像")            │
│                                                       │
│ [开始升级]  [取消]                                    │
│ 总进度    [████████░░░░░] 62%                         │
│ 状态      正在发送数据块 456/735 ...                  │
│                                                       │
│ 升级日志: [13:05:02] FW_START ok, 187264 字节         │
└────────────────────────────────────────────────────────┘
```

行为：

- **选文件**：浏览 → `fw_image_load` → `fw_image_validate_header`（失败红字）→ `fw_image_extract_keyhash`（成功显示 keyhash=OK）。校验未通过则禁用"开始升级"。
- **通道切换**：单选 UDP/CAN，切换显隐子区。
- **UDP 升级**（worker 线程）：`FwStart(size, keyhash, &status)` → status==1 → 循环 `FwData(≤511B)` → `FwEnd(test, crc16)` → result==1 → 提示"完成，设备将重启进行 MCUboot 交换"。
- **CAN 升级**（worker 线程）：见 §5.2 流程。
- **进度回 UI**：worker 通过 `PostMessage(WM_APP_UPG_PROGRESS/WM_APP_UPG_LOG/WM_APP_UPG_DONE)` 回 UI 线程更新进度条/状态/日志。
- **取消**：`volatile LONG g_upgrade_cancel`，"取消"置 1，worker 每块前检查干净退出（不发 END/CONFIRM，设备侧超时自行丢弃）。

### 6.3 Tab3 Modbus 调试

```
┌─ Modbus 调试 ─────────────────────────────────────────┐
│ 传输  ( ) TCP  ( ) RTU                                │
│ [TCP] 目标 IP [192.168.012.101] 端口[502] UID[1]      │
│ [RTU] 串口 [COM3 ▼] 波特率[9600] UID[1]               │
│ [连接][断开]   状态: ● 已连接 (TCP 192.168.12.101)    │
│                                                       │
│ [刷新全部]  自动刷新 [ ] 间隔[1000]ms                 │
│ ════════════════════════════════════════════════════ │
│ ┌─ DI 数字输入 (16路, 只读) ────────────────────────┐ │
│ │ DI1● DI2○ DI3● ... DI16○                          │ │
│ └────────────────────────────────────────────────────┘│
│ ┌─ DO 数字输出 (8路, 可写) ─────────────────────────┐ │
│ │ DO1■ DO2□ ... DO8□ (点击 → WriteSingleCoil 0-7)   │ │
│ └────────────────────────────────────────────────────┘│
│ ┌─ AI 模拟输入 (4路, 只读) ─────────────────────────┐ │
│ │ AI1 4.20 mA  AI2 0.00 mA  AI3 5.00 V  AI4 0.00 V   │ │
│ └────────────────────────────────────────────────────┘│
│ ┌─ 寄存器表 (holding FC03 / input FC04) ────────────┐ │
│ │ 地址  名称          值     R/W [查询][设置]        │ │
│ │ 40001 DO输出控制   0x0055 RW   [查][改...]         │ │
│ │ ...                                               │ │
│ │ 30001 固件版本     0x0001 R    [查]                │ │
│ └────────────────────────────────────────────────────┘│
│ 操作日志: [13:10:01] 写 coil 0 = ON 成功             │
└────────────────────────────────────────────────────────┘
```

行为：

- **连接**：TCP 走 `MbClient_ConnectTcp`；RTU 走 `MbClient_ConnectRtu`（枚举 COM1-COM32 下拉）。状态灯 + 文字。
- **DI 面板**：16 圆点，`ReadDiscreteInputs(0,16)` 刷新亮灭。只读。
- **DO 面板**：8 方块按钮，点击 → `WriteSingleCoil(addr,on)` → 立即 `ReadCoils(0,8)` 回显。每个 DO 单独可控。
- **AI 面板**：4 数值，`ReadInput(1,4)`；AI1/AI2 显示 `value/100.0 mA`，AI3/AI4 显示 `value/100.0 V`（固件单位 0.01mA / 0.01V）。
- **寄存器表**：ListView report 风格，列：地址/名称/当前值/R/W/[查询]/[设置]。固化的元数据表（地址/名称/RW）写死在上位机代码（见 §7）。
  - 行内"查询"：`ReadHolding`/`ReadInput` 刷新当前行。
  - 行内"设置"（仅 RW）：弹小对话框输入 16/10 进制 → `WriteSingleReg`。
  - 只读/WO 标记的"设置"按钮禁用。
- **刷新全部**：一次性读 DI/DO/AI + 所有寄存器。
- **自动刷新**：勾选用 `SetTimer` 按间隔轮询 DI/DO/AI；**不动 holding 表**避免误触"设置"。取消勾选 `KillTimer`。

## 7. 寄存器元数据表（tab3 固化）

| holding 地址 | 名称 | R/W | 备注 |
|---|---|---|---|
| 40001 (0x00) | DO输出控制 | RW | 写即驱动 GPIO |
| 40002 (0x01) | DI使能位图 | RW | |
| 40003 (0x02) | AI使能位图 | RW | |
| 40004 (0x03) | DI采样间隔ms | RW | |
| 40005 (0x04) | AI采样间隔ms | RW | |
| 40006 (0x05) | 历史保存开关 | RW | |
| 40007 (0x06) | CAN业务帧ID | RW | |
| 40008 (0x07) | CAN波特率(k) | RW | |
| 40009 (0x08) | RS485波特率 | RW | |
| 40010 (0x09) | Modbus从机ID | RW | 改后需重启 |
| 40011 (0x0A) | IP第1字节 | RW | |
| 40012 (0x0B) | IP第2字节 | RW | |
| 40013 (0x0C) | IP第3字节 | RW | |
| 40014 (0x0D) | IP第4字节 | RW | 改后重启生效 |
| 40015 (0x0E) | 时间戳高字 | WO触发 | 配合低字设 RTC |
| 40016 (0x0F) | 时间戳低字 | WO触发 | 写低字触发 set_timestamp |
| 40017 (0x10) | 参数保存触发 | WO | 写非0触发全量保存 |
| 40018 (0x11) | 重启触发 | WO | 写1冷启动 |

| input 地址 | 名称 | R/W | 备注 |
|---|---|---|---|
| 30001 (0x00) | 固件版本 | R | `(MAJOR<<8)\|MINOR` |
| 30002 (0x01) | AI1电流 | R | 0.01 mA |
| 30003 (0x02) | AI2电流 | R | 0.01 mA |
| 30004 (0x03) | AI3电压 | R | 0.01 V |
| 30005 (0x04) | AI4电压 | R | 0.01 V |
| 30006 (0x05) | DI1-DI16位图 | R | bit0=DI1 ... bit15=DI16 |

Modbus 映射：coils 0-7 ↔ holding 0x00 位 0-7（DO1-DO8）；discrete inputs 0-15 ↔ input 0x05 位 0-15（DI1-DI16）。支持 FC01/02/03/04/05/06/15/16。

## 8. 错误处理（对齐 handler-receiver）

所有通信 API 返回 `bool` + nullable out 参数；失败时模块记录最近错误，UI 通过 `GetLastError()` 取中文。

| 场景 | 处理 |
|---|---|
| UDP 无响应（1s 超时） | `MessageBoxW` "设备无响应，请检查 IP 和网络" |
| UDP 回复 cmd 不匹配/长度异常 | 视为无响应 |
| SET_* 返回 ok=0 | 显示"设备拒绝参数"（SET_IP 末位 0/0xFF、首段 224-239） |
| FW_START status=2 (keyhash mismatch) | 红字"密钥哈希不匹配，镜像与设备引导加载器不匹配" |
| FW_END result=0 | "CRC 校验失败" |
| PCAN DLL 缺失 | tab2 CAN 通道提示"未检测到 PCAN-USB 驱动，请安装 PCAN-Basic" |
| CAN FLASH_ERROR/TRANSFER_ERROR/KEYHASH_ERROR | 对应中文 + 升级中止 |
| Modbus 超时/异常响应 | `GetLastError` 显示 exception code 中文映射 |
| 固件文件非 MCUboot 镜像 | tab2 校验阶段红字 + 禁用升级按钮 |

用户可见错误 `MessageBoxW(MB_OK | MB_ICONERROR/WARNING)`，成功 `MB_ICONINFORMATION`，底部日志框同时记录。

## 9. 并发模型

UI 线程处理所有窗口消息与控件刷新；快速操作（单寄存器读写、单次 GET、连接建立，<1s）直接在 UI 线程阻塞。长任务（tab2 固件升级，数十秒到几分钟）用 worker 线程：

- worker 线程禁止直接操作控件，通过自定义消息回 UI：
  - `WM_APP_UPG_PROGRESS`（wParam=百分比 0-100, lParam=状态码）
  - `WM_APP_UPG_LOG`（lParam=堆上分配的字符串指针，UI 收到后 free）
  - `WM_APP_UPG_DONE`（wParam=1 成功/0 失败）
- 取消：`volatile LONG g_upgrade_cancel`，"取消"按钮 `InterlockedExchange` 置 1，worker 每块前检查干净退出（不发 END/CONFIRM）。
- Modbus 自动刷新：`SetTimer` + `WM_TIMER` 同步读 DI/DO/AI（每次 <100ms），禁用时 `KillTimer`。

## 10. 字符串与编码

全 UNICODE 构建，UI `L"..."` + `*W` API。协议字节流与 UI 间用 `swprintf`/`WideCharToMultiByte`。MinGW preset 加 `-finput-charset=UTF-8 -fexec-charset=GBK`（源 UTF-8 存中文，运行时 GBK 适配旧 Win32 控件）；MSVC 加 `/utf-8`。日志用 `GetLocalTime` 时间戳。

## 11. 实现顺序（用于 writing-plans）

1. **骨架**：`CMakeLists.txt` + `CMakePresets.json` + `.gitignore` + `main.c` 最小主窗口 + tab 框架（3 空 tab 可切换）→ 能编译出 exe。
2. **通信层迁移**：`pcan_loader` + `fw_image`（原样）+ `can_manager`（改帧 ID 0x101-0x105）+ `udp_manager`（加 0x10-0x1B）。
3. **tab1 完整**：配置 + 发现 + 运维。
4. **tab2 完整**：UDP + CAN 升级 + worker 线程 + 进度/取消。
5. **`modbus_client` 新模块**：TCP + RTU。
6. **tab3 完整**：连接 + DI/DO/AI/寄存器表。
7. **收尾**：README + 资源 + 整体联调。

## 12. 不做的事（YAGNI 边界）

- 不做多设备批量管理（单设备调试工具）。
- 不做固件签名验签（信任 .bin 是 MCUboot 已签名产物，只做 magic + keyhash 提取）。
- 不做自动测试（对齐 handler-receiver，无测试框架）。
- 不做 CI/Release workflow（除非后续需要）。
- 不做 i18n（全中文 UI）。
- tab1 不做采样参数/历史开关。
- 不改固件（所有协议按 v3.3 现状实现）。

## 13. 关键协议参考（来自固件代码，实现时以代码为准）

- UDP 8600，帧格式 `[cmd 1B][data...]` 无 magic；回复首字节 echo cmd，回复缓冲 64B（payload ≤63B）。
- UDP app 命令（0x10+）**大端**；UDP FW 命令（0x01-0x05）size/offset/crc **小端**；CAN 0x101/0x102 32 位字段**小端**。
- UDP DISCOVER（0x18）是唯一允许跨网段广播回复的命令；跨网段回复发到端口 8601。
- keyhash = SHA-256(RSA 公钥 DER PKCS#1)，从 .bin 的 MCUboot TLV（tag 0x0001）直接提取，不需要 .pem。
- UDP CRC = CRC16-CCITT 整镜像 init 0；CAN 无 CRC，靠 CONFIRM 时 size 匹配。
- Modbus holding 枚举 0x00-0x11（18 个），Kconfig 默认 21 但 0x12-0x14 已废弃读回 0，上位机只访问 0x00-0x11。
- Modbus TCP 端口 502，会话超时 60s（代码值，非 USER_GUIDE 的 30s），TCP Keepalive idle 30s/interval 5s/count 3。
- Modbus 时间戳：先写高字 0x0E，再写低字 0x0F，仅低字写入触发 `set_timestamp`。
- IP 改动触发设备延迟重启；slave_id 改动需重启生效；netmask 固定 /24，gateway = IP 末位改 1。

权威源文件：

- `applications/io-edge-hub/src/udp.c` + `udp.h` — UDP 配置命令
- `applications/io-edge-hub/include/init.h` — 寄存器枚举
- `applications/io-edge-hub/src/modbus/function.c` — 寄存器默认值、回调、副作用
- `applications/io-edge-hub/src/modbus/tcp.c` — Modbus TCP 服务端行为
- `libs/udp_fw_upgrade/udp_fw_upgrade.c` — UDP FW 协议（0x01-0x05）
- `libs/can_fw_upgrade/can_fw_upgrade.c` — CAN FW 协议（0x101-0x105）
- `libs/gen_keyhash.py` — keyhash 算法（上位机改为从 .bin TLV 提取，与本算法结果一致）
- `docs/io-edge-hub.md` §6 — 协议表（与代码不一致处以代码为准，尤其 SET_MODBUS/SET_CAN 字段宽 2B）
