# io-edge-hub 上位机实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 构建一个 Win32 原生 C GUI 上位机 `io-edge-hub.exe`，分 3 个 tab 完成 io-edge-hub 设备的 UDP 参数配置 / UDP+CAN 固件升级 / Modbus TCP+RTU 调试。

**Architecture:** 单进程 Win32 GUI，3 个 tab 各为一个子对话框模块（`config_tab` / `upgrade_tab` / `modbus_tab`），共享 5 个不依赖 UI 的通信模块（`udp_manager` / `can_manager` / `fw_image` / `pcan_loader` / `modbus_client`）。前 4 个通信模块从 `~/code/handler-receiver` 迁移适配，`modbus_client` 全新。

**Tech Stack:** C11 + CMake 3.25 + CMakePresets（MSVC 原生 + MinGW 交叉），Win32 + comctl32，Winsock2（UDP/TCP），Win32 串口 API（RTU），PCANBasic.dll 动态加载（CAN）。

## Global Constraints

来自 spec 的项目级硬约束，每个 task 隐式继承：

- **语言/标准**：C11（`set(CMAKE_C_STANDARD 11)`），无 C++。
- **CMake**：≥ 3.25，`project(io-edge-hub VERSION 0.1.0 LANGUAGES C RC)`。
- **CMakePresets**：两个 configure preset —— `vs`（MSVC，binaryDir `out`）和 `mingw`（`x86_64-w64-mingw32-gcc` + Ninja，binaryDir `build`）。
- **依赖**：仅系统库 `comctl32 comdlg32 gdi32 ws2_32 iphlpapi`；PCANBasic.dll 运行时 `LoadLibrary` 加载，不 vendoring、不用 vcpkg/conan/FetchContent/find_package。
- **编码**：全 `UNICODE` + `_UNICODE` + `_CRT_SECURE_NO_WARNINGS`；MinGW 加 `-finput-charset=UTF-8 -fexec-charset=GBK`；MSVC 加 `/utf-8`；MinGW 链接 `-mwindows -static-libgcc -s`，MSVC 链接 `/SUBSYSTEM:WINDOWS`。
- **代码风格**：Tab 缩进、K&R 大括号、`switch` case 不缩进；公共 API `Module_Action`，静态 helper `snake_case`；类型 PascalCase；enum `enum lowercase { ALL_CAPS, };` 全局 `g_` 前缀；宏 `ALL_CAPS`；注释中文 `/* */` + `/* ===== banner ===== */`；**不加文件头 license 注释**。
- **错误处理**：通信 API 返回 `bool` + nullable out 参数，失败记录最近错误，`GetLastError()` 取中文，UI 用 `MessageBoxW`。
- **UI 文案**：全中文。
- **不做**：自动测试（spec §12）、CI、i18n、多设备、固件签名验签。
- **路径**：项目根 `C:\Users\jxwaz\code\io-edge-hub`，独立 git 仓库。**当前没有 MSVC/MinGW 编译环境时，验证以"语法/结构审阅 + 能被 CMake configure 通过"为最低门槛，最终联调在有设备时进行**（spec §11 step 7）。

**重要参考路径**（迁移源，只读）：
- `C:\Users\jxwaz\code\handler-receiver\` —— 风格 + 模块源
- `C:\Users\jxwaz\code\app\apps\applications\io-edge-hub\` —— 固件协议权威源
- `C:\Users\jxwaz\code\app\apps\libs\` —— udp_fw_upgrade / can_fw_upgrade 库
- spec：`C:\Users\jxwaz\code\io-edge-hub\docs\superpowers\specs\2026-08-11-host-tool-design.md`

**协议速查**（实现时以固件代码为准）：

| 通道 | 端口/帧 | 端序 |
|---|---|---|
| UDP 配置（0x10-0x1B）| 8600，`[cmd 1B][data...]`，回复首字节 echo cmd，回复缓冲 64B（payload ≤63B） | 多字节大端 |
| UDP 升级（0x01-0x05）| 同 8600 | size/offset/crc 小端 |
| CAN 升级 | 0x101-0x105，8B DLC | 32 位字段小端 |
| Modbus TCP | 502 | MBAP+PDU 标准 |
| Modbus RTU | PC 串口 8N1 | CRC16-Modbus（poly 0xA001 init 0xFFFF）|

UDP DISCOVER（0x18）是唯一允许跨网段广播回复的命令；跨网段回复发到端口 8601。

---

## 文件结构（最终形态）

每个文件单一职责，1:1 `.h`+`.c`：

| 文件 | 职责 | 来源 |
|---|---|---|
| `CMakeLists.txt` | 构建定义 | 新写（仿 handler-receiver）|
| `CMakePresets.json` | 双工具链 preset | 新写（仿 handler-receiver）|
| `.gitignore` | 忽略 build/out/.zcode/.superpowers | 新写 |
| `README.md` | 项目说明 + 构建步骤 | Task 8 写 |
| `include/app.h` | 全局公共：版本宏、自定义消息、公共日志函数、tab 框架声明 | 新写 |
| `include/resource.h` | 所有控件 ID 宏 | 新写，随 tab 增长 |
| `include/pcan_loader.h` | PCANBasic.dll 函数指针声明 | 迁移 handler-receiver 原样 |
| `include/fw_image.h` | MCUboot header/keyhash 解析声明 | 迁移 handler-receiver 原样 |
| `include/udp_manager.h` | UDP 8600 配置+升级 API | 新写（参考 handler-receiver 简化为同步 req/resp）|
| `include/can_manager.h` | PCAN + CAN 升级 API | 迁移 handler-receiver 适配 io-edge-hub 帧 ID |
| `include/modbus_client.h` | Modbus TCP/RTU 主机 API | 新写 |
| `include/config_tab.h` | tab1 接口 | 新写 |
| `include/upgrade_tab.h` | tab2 接口 | 新写 |
| `include/modbus_tab.h` | tab3 接口 | 新写 |
| `src/main.c` | WinMain + 主窗口 + tab control | 新写 |
| `src/pcan_loader.c` | DLL 加载实现 | 迁移原样 |
| `src/fw_image.c` | MCUboot 解析实现 | 迁移原样 |
| `src/udp_manager.c` | UDP 实现 | 新写 |
| `src/can_manager.c` | CAN 实现 | 迁移适配 |
| `src/modbus_client.c` | Modbus 实现 | 新写 |
| `src/config_tab.c` | tab1 UI+业务 | 新写 |
| `src/upgrade_tab.c` | tab2 UI+业务 | 新写 |
| `src/modbus_tab.c` | tab3 UI+业务 | 新写 |
| `resources/resource.rc` | 版本信息 + 图标 | 新写（仿 handler-receiver）|
| `resources/app.ico` | 图标 | 用 handler-receiver 的 `icon.ico` 临时占位 |
| `tools/build.bat` | MSVC 一键构建（可选）| Task 8 写 |

**模块依赖方向**（无环）：
```
main.c → app.h, resource.h, 三个 *_tab.h
config_tab → udp_manager, app, resource
upgrade_tab → udp_manager, can_manager, fw_image, pcan_loader, app, resource
modbus_tab → modbus_client, app, resource
can_manager → pcan_loader
udp_manager / modbus_client / fw_image → 无内部依赖
```

---

## Task 1: 项目骨架与最小可编译 GUI

**目标**：建立项目骨架、构建配置、最小主窗口 + 3 空 tab 可切换，能被 CMake configure + build 通过。

**Files:**
- Create: `CMakeLists.txt`
- Create: `CMakePresets.json`
- Create: `.gitignore`
- Create: `include/app.h`
- Create: `include/resource.h`
- Create: `src/main.c`
- Create: `resources/resource.rc`
- Create: `resources/app.ico`（从 handler-receiver 复制 `icon.ico`）

**Interfaces:**
- Produces: `app.h` 提供 `g_hInst` / `g_hMain` extern、版本宏 `APP_VERSION_W`、公共日志 `AppLog_Printf(const wchar_t *fmt, ...)`、3 个 tab 工厂函数声明（此 task 仅声明，空实现见下）；`resource.h` 提供 tab 与控件 ID。
- Consumes: 无。

- [ ] **Step 1: 复制图标占位**

```bash
cp ~/code/handler-receiver/resources/icon.ico ~/code/io-edge-hub/resources/app.ico
```

- [ ] **Step 2: 写 `.gitignore`**

文件 `C:\Users\jxwaz\code\io-edge-hub\.gitignore`：

```
build/
out/
.zcode/
.superpowers/
*.exe
*.obj
*.o
```

- [ ] **Step 3: 写 `CMakeLists.txt`**（仿 handler-receiver，去掉无关源，先只列 main.c）

文件 `CMakeLists.txt`：

```cmake
cmake_minimum_required(VERSION 3.25)
project(io-edge-hub VERSION 0.1.0 LANGUAGES C RC)

set(CMAKE_C_STANDARD 11)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

include_directories(${CMAKE_SOURCE_DIR}/include)

set(SOURCES
    src/main.c
)

if(WIN32)
    list(APPEND SOURCES resources/resource.rc)
endif()

add_executable(${PROJECT_NAME} WIN32 ${SOURCES})

target_compile_definitions(${PROJECT_NAME} PRIVATE
    UNICODE
    _UNICODE
    _CRT_SECURE_NO_WARNINGS
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    comctl32
    comdlg32
    gdi32
    ws2_32
    iphlpapi
)

if(MINGW)
    target_compile_options(${PROJECT_NAME} PRIVATE -Os -finput-charset=UTF-8 -fexec-charset=GBK)
    target_link_options(${PROJECT_NAME} PRIVATE -mwindows -static-libgcc -s)
elseif(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /utf-8)
    target_link_options(${PROJECT_NAME} PRIVATE /SUBSYSTEM:WINDOWS)
endif()

target_compile_definitions(${PROJECT_NAME} PRIVATE
    APP_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    APP_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    APP_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)
```

- [ ] **Step 4: 写 `CMakePresets.json`**（仿 handler-receiver）

文件 `CMakePresets.json`：

```json
{
    "version": 7,
    "cmakeMinimumRequired": { "major": 3, "minor": 25, "patch": 0 },
    "configurePresets": [
        {
            "name": "mingw",
            "displayName": "MinGW (Linux Cross-Compile)",
            "generator": "Ninja",
            "binaryDir": "${sourceDir}/build",
            "cacheVariables": {
                "CMAKE_SYSTEM_NAME": "Windows",
                "CMAKE_C_COMPILER": "x86_64-w64-mingw32-gcc",
                "CMAKE_RC_COMPILER": "x86_64-w64-mingw32-windres"
            }
        },
        {
            "name": "vs",
            "displayName": "Visual Studio (Windows Native)",
            "binaryDir": "${sourceDir}/out"
        }
    ],
    "buildPresets": [
        { "name": "mingw-release", "configurePreset": "mingw", "configuration": "Release" },
        { "name": "mingw-debug",   "configurePreset": "mingw", "configuration": "Debug" },
        { "name": "vs-release",    "configurePreset": "vs",    "configuration": "Release" },
        { "name": "vs-debug",      "configurePreset": "vs",    "configuration": "Debug" }
    ],
    "workflowPresets": [
        { "name": "mingw-release", "steps": [ {"type":"configure","name":"mingw"}, {"type":"build","name":"mingw-release"} ] },
        { "name": "mingw-debug",   "steps": [ {"type":"configure","name":"mingw"}, {"type":"build","name":"mingw-debug"} ] },
        { "name": "vs-release",    "steps": [ {"type":"configure","name":"vs"},    {"type":"build","name":"vs-release"} ] },
        { "name": "vs-debug",      "steps": [ {"type":"configure","name":"vs"},    {"type":"build","name":"vs-debug"} ] }
    ]
}
```

- [ ] **Step 5: 写 `include/resource.h`**（ID 段规划：tab1=1xxx, tab2=2xxx, tab3=3xxx, 通用=9xxx）

文件 `include/resource.h`：

```c
#ifndef RESOURCE_H
#define RESOURCE_H

/* 资源源 ID */
#define IDI_APP_ICON            100

/* tab 索引 */
#define TAB_CONFIG              0
#define TAB_UPGRADE             1
#define TAB_MODBUS              2
#define TAB_COUNT               3

/* ===== 通用控件 (9xxx) ===== */
#define IDC_STATUSBAR           9001

/* tab1 控件 ID (1xxx) — Task 6 扩展 */
/* tab2 控件 ID (2xxx) — Task 7 扩展 */
/* tab3 控件 ID (3xxx) — Task 9 扩展 */

#endif /* RESOURCE_H */
```

- [ ] **Step 6: 写 `include/app.h`**

文件 `include/app.h`：

```c
#ifndef APP_H
#define APP_H

#include <windows.h>

/* 应用版本号宽字符串 (CMakeLists 注入 APP_VERSION_MAJOR/MINOR/PATCH).
 * 双层宏字符串化 + L"" 拼接 (MSVC 不支持 L#x). */
#define ZC_STR2(x) #x
#define ZC_STR(x)  ZC_STR2(x)
#define APP_VERSION_W L"" ZC_STR(APP_VERSION_MAJOR) L"." \
                      L"" ZC_STR(APP_VERSION_MINOR) L"." \
                      L"" ZC_STR(APP_VERSION_PATCH)

/* 工作线程 → UI 线程 自定义消息 */
#define WM_APP_UPG_PROGRESS  (WM_APP + 1)  /* wParam=0-100, lParam=阶段码 */
#define WM_APP_UPG_LOG       (WM_APP + 2)  /* lParam=堆字符串指针, UI 收到 free */
#define WM_APP_UPG_DONE      (WM_APP + 3)  /* wParam=1 成功 / 0 失败 */

/* 全局实例 (main.c 定义) */
extern HINSTANCE g_hInst;
extern HWND g_hMain;

/* 公共日志: 向主窗口底部状态栏临时显示 + 控制台打印 (后续 tab 各自维护日志框).
 * 线程安全: 内部临界区. */
void AppLog_Printf(const wchar_t *fmt, ...);

/* tab 工厂: 在主窗口 tab 控件内创建子对话框, 返回子窗口 HWND.
 * 每个 tab 模块在各自 .c 实现. */
HWND ConfigTab_Create(HWND hParent, HINSTANCE hInst);
HWND UpgradeTab_Create(HWND hParent, HINSTANCE hInst);
HWND ModbusTab_Create(HWND hParent, HINSTANCE hInst);

#endif /* APP_H */
```

- [ ] **Step 7: 写 `src/main.c`**（主窗口 + tab control + tab 切换显隐子窗口）

文件 `src/main.c`：

```c
/* io-edge-hub 上位机 - Win32 GUI 主入口
 * Tab1: UDP 参数配置
 * Tab2: 固件升级 (UDP + CAN)
 * Tab3: Modbus 调试 (TCP + RTU)
 */
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include "app.h"
#include "resource.h"

#pragma comment(lib, "comctl32.lib")

HINSTANCE g_hInst;
HWND g_hMain;
static HWND g_hTab;
static HWND g_hTabDlg[TAB_COUNT];
static HWND g_hStatus;
static CRITICAL_SECTION g_logCs;

/* AppLog_Printf: 状态栏临时显示 (简单实现, 后续 tab 自己有日志框). */
void AppLog_Printf(const wchar_t *fmt, ...)
{
	wchar_t buf[512];
	va_list ap;
	va_start(ap, fmt);
	vswprintf(buf, 512, fmt, ap);
	va_end(ap);

	EnterCriticalSection(&g_logCs);
	if (g_hStatus) {
		SendMessageW(g_hStatus, SB_SETTEXTW, 0, (LPARAM)buf);
	}
	LeaveCriticalSection(&g_logCs);
}

/* 主窗口 on WM_SIZE: tab 控件 + 状态栏自适应. */
static void main_on_size(int cx, int cy)
{
	if (g_hStatus) {
		SendMessageW(g_hStatus, WM_SIZE, 0, 0);
	}
	RECT rcStatus = {0};
	GetWindowRect(g_hStatus, &rcStatus);
	int statusH = rcStatus.bottom - rcStatus.top;

	if (g_hTab) {
		MoveWindow(g_hTab, 0, 0, cx, cy - statusH, TRUE);
	}
	/* tab 子对话框贴 tab 控件显示区 */
	if (g_hTab) {
		RECT rcDisp = {0};
		TabCtrl_GetItemRect(g_hTab, 0, &rcDisp);
		int top = rcDisp.bottom + 4;
		for (int i = 0; i < TAB_COUNT; i++) {
			if (g_hTabDlg[i]) {
				MoveWindow(g_hTabDlg[i], 4, top, cx - 8, cy - statusH - top - 4, TRUE);
			}
		}
	}
}

static LRESULT CALLBACK main_wndproc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg) {
	case WM_CREATE: {
		g_hTab = CreateWindowExW(0, WC_TABCONTROLW, L"",
			WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | TCS_TABS,
			0, 0, 0, 0, hWnd, (HMENU)0, g_hInst, NULL);

		TCITEMW it = { .mask = TCIF_TEXT };
		it.pszText = (LPWSTR)L"UDP 参数配置";
		TabCtrl_InsertItem(g_hTab, TAB_CONFIG, &it);
		it.pszText = (LPWSTR)L"固件升级";
		TabCtrl_InsertItem(g_hTab, TAB_UPGRADE, &it);
		it.pszText = (LPWSTR)L"Modbus 调试";
		TabCtrl_InsertItem(g_hTab, TAB_MODBUS, &it);

		g_hStatus = CreateWindowExW(0, STATUSCLASSNAMEW, L"就绪",
			WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
			0, 0, 0, 0, hWnd, (HMENU)IDC_STATUSBAR, g_hInst, NULL);

		g_hTabDlg[TAB_CONFIG]  = ConfigTab_Create(g_hTab, g_hInst);
		g_hTabDlg[TAB_UPGRADE] = UpgradeTab_Create(g_hTab, g_hInst);
		g_hTabDlg[TAB_MODBUS]  = ModbusTab_Create(g_hTab, g_hInst);
		for (int i = 0; i < TAB_COUNT; i++) {
			ShowWindow(g_hTabDlg[i], i == TAB_CONFIG ? SW_SHOW : SW_HIDE);
		}
		return 0;
	}
	case WM_NOTIFY: {
		LPNMHDR pnmh = (LPNMHDR)lParam;
		if (pnmh->hwndFrom == g_hTab && pnmh->code == TCN_SELCHANGE) {
			int sel = TabCtrl_GetCurSel(g_hTab);
			for (int i = 0; i < TAB_COUNT; i++) {
				ShowWindow(g_hTabDlg[i], i == sel ? SW_SHOW : SW_HIDE);
			}
		}
		return 0;
	}
	case WM_SIZE:
		main_on_size(LOWORD(lParam), HIWORD(lParam));
		return 0;
	case WM_GETMINMAXINFO: {
		MINMAXINFO *mmi = (MINMAXINFO *)lParam;
		mmi->ptMinTrackSize.x = 720;
		mmi->ptMinTrackSize.y = 560;
		return 0;
	}
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProcW(hWnd, msg, wParam, lParam);
}

/* 临时空 tab 工厂 (后续 Task 6/7/9 替换为真实实现). */
HWND ConfigTab_Create(HWND hParent, HINSTANCE hInst)
{
	return CreateWindowExW(0, L"STATIC", L"Tab1 占位 (UDP 参数配置)",
		WS_CHILD | SS_CENTER, 0, 0, 0, 0, hParent, NULL, hInst, NULL);
}
HWND UpgradeTab_Create(HWND hParent, HINSTANCE hInst)
{
	return CreateWindowExW(0, L"STATIC", L"Tab2 占位 (固件升级)",
		WS_CHILD | SS_CENTER, 0, 0, 0, 0, hParent, NULL, hInst, NULL);
}
HWND ModbusTab_Create(HWND hParent, HINSTANCE hInst)
{
	return CreateWindowExW(0, L"STATIC", L"Tab3 占位 (Modbus 调试)",
		WS_CHILD | SS_CENTER, 0, 0, 0, 0, hParent, NULL, hInst, NULL);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrev, PWSTR cmdLine, int show)
{
	INITCOMMONCONTROLSEX icc = { .dwSize = sizeof(icc), .dwICC = ICC_TAB_CLASSES | ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES };
	InitCommonControlsEx(&icc);

	WSADATA wsa;
	WSAStartup(MAKEWORD(2, 2), &wsa);

	InitializeCriticalSection(&g_logCs);
	g_hInst = hInstance;

	WNDCLASSEXW wc = { .cbSize = sizeof(wc) };
	wc.lpfnWndProc = main_wndproc;
	wc.hInstance = hInstance;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
	wc.lpszClassName = L"ioEdgeHubMainCls";
	wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON));
	RegisterClassExW(&wc);

	g_hMain = CreateWindowExW(0, wc.lpszClassName, L"io-edge-hub 上位机 v" APP_VERSION_W,
		WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX,
		CW_USEDEFAULT, CW_USEDEFAULT, 720, 560,
		NULL, NULL, hInstance, NULL);
	ShowWindow(g_hMain, show);
	UpdateWindow(g_hMain);

	MSG msg;
	while (GetMessageW(&msg, NULL, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}

	DeleteCriticalSection(&g_logCs);
	WSACleanup();
	return (int)msg.wParam;
}
```

- [ ] **Step 8: 写 `resources/resource.rc`**

文件 `resources/resource.rc`：

```
#include <windows.h>
#include "resource.h"

#pragma code_page(65001)

IDI_APP_ICON ICON "app.ico"

VS_VERSION_INFO VERSIONINFO
FILEVERSION 0,1,0,0
PRODUCTVERSION 0,1,0,0
FILEFLAGSMASK 0x3fL
FILEFLAGS 0x0L
FILEOS 0x40004L
FILETYPE 0x1L
FILESUBTYPE 0x0L
BEGIN
    BLOCK "StringFileInfo"
    BEGIN
        BLOCK "040904b0"
        BEGIN
            VALUE "CompanyName", "Kabirz"
            VALUE "FileDescription", "io-edge-hub 上位机"
            VALUE "FileVersion", "0.1.0.0"
            VALUE "InternalName", "io-edge-hub"
            VALUE "OriginalFilename", "io-edge-hub.exe"
            VALUE "ProductName", "io-edge-hub 上位机"
            VALUE "ProductVersion", "0.1.0.0"
        END
    END
    BLOCK "VarFileInfo"
    BEGIN
        VALUE "Translation", 0x0804, 936
    END
END
```

注意：`resource.h` 在 `include/` 下，rc 文件用 `#include "resource.h"` 需要让 rc 编译器找到它。CMake 已用全局 `include_directories(${CMAKE_SOURCE_DIR}/include)`，MSVC/windres 均会沿 include path 搜索，OK。

- [ ] **Step 9: 配置 + 构建验证**

Run:
```bash
cd ~/code/io-edge-hub
cmake --preset vs
cmake --build out --config Release
```
Expected: 生成 `out/bin/io-edge-hub.exe`（或 `out/bin/Release/io-edge-hub.exe`，依 VS 生成器布局）；无错误。若无 MSVC 环境，改用 `cmake --preset mingw && cmake --build build --config Release` 验证 MinGW 交叉链路（Linux 上）。两者都不可用时，最低门槛为 `cmake --preset vs` 的 configure 阶段无错（即 CMakeLists/preset/文件路径正确）。

- [ ] **Step 10: 运行验证**

双击 `io-edge-hub.exe`：窗口标题 "io-edge-hub 上位机 v0.1.0"，3 个 tab（"UDP 参数配置"/"固件升级"/"Modbus 调试"）可切换，每个 tab 显示占位静态文本。窗口最小尺寸 720×560。底部状态栏 "就绪"。

- [ ] **Step 11: Commit**

```bash
cd ~/code/io-edge-hub
git add CMakeLists.txt CMakePresets.json .gitignore include/ src/main.c resources/
git commit -m "feat: scaffold io-edge-hub host tool project with minimal 3-tab GUI"
```

---

## Task 2: 迁移 `pcan_loader` 与 `fw_image`（原样复用）

**目标**：把 handler-receiver 中协议无关的 `pcan_loader` 与 `fw_image` 原样复制过来，接入 CMake，编译通过。

**Files:**
- Create: `include/pcan_loader.h`（复制 `~/code/handler-receiver/include/pcan_loader.h`）
- Create: `src/pcan_loader.c`（复制 `~/code/handler-receiver/src/pcan_loader.c`）
- Create: `include/fw_image.h`（复制 `~/code/handler-receiver/include/fw_image.h`）
- Create: `src/fw_image.c`（复制 `~/code/handler-receiver/src/fw_image.c`）
- Modify: `CMakeLists.txt`（SOURCES 列表加 `src/pcan_loader.c` 和 `src/fw_image.c`）

**Interfaces:**
- Produces: `pcan_loader.h` 提供 `Pcan_Initialize`/`Pcan_Read`/`Pcan_Write`/`Pcan_Close`/`Pcan_GetStatus`/`Pcan_Uninitialize`/`Pcan_Reset`/`Pcan_GetValue`/`Pcan_SetValue` 等 `extern` 函数指针 + `Pcan_Load()`/`Pcan_Available()`；`fw_image.h` 提供 `fw_image_validate_header`、`fw_image_extract_keyhash`、宏 `IMG_MAGIC`/`IMG_TLV_INFO_MAGIC`/`IMG_TLV_KEYHASH`/`IMG_KEYHASH_LEN`。
- Consumes: 无。

- [ ] **Step 1: 复制 4 个文件**

```bash
cp ~/code/handler-receiver/include/pcan_loader.h ~/code/io-edge-hub/include/
cp ~/code/handler-receiver/src/pcan_loader.c     ~/code/io-edge-hub/src/
cp ~/code/handler-receiver/include/fw_image.h    ~/code/io-edge-hub/include/
cp ~/code/handler-receiver/src/fw_image.c        ~/code/io-edge-hub/src/
```

复制后**检查文件内容不含其他模块依赖**（`pcan_loader.c` 仅依赖 windows.h + 自身头；`fw_image.c` 仅依赖 `string.h` + 自身头）。如发现 include 了 `udp_manager.h`/`can_manager.h` 等无关头，删除那些 include。

- [ ] **Step 2: 修改 `CMakeLists.txt` 的 SOURCES**

把 `set(SOURCES src/main.c)` 改为：

```cmake
set(SOURCES
    src/main.c
    src/pcan_loader.c
    src/fw_image.c
)
```

- [ ] **Step 3: 构建验证**

Run:
```bash
cd ~/code/io-edge-hub
cmake --build out --config Release
```
Expected: 编译通过（`fw_image.c` 纯算法、`pcan_loader.c` 仅 `LoadLibrary`，无外部依赖）。`io-edge-hub.exe` 仍能运行，行为同 Task 1（这两个模块此时尚未被 UI 调用）。

- [ ] **Step 4: Commit**

```bash
git add include/pcan_loader.h include/fw_image.h src/pcan_loader.c src/fw_image.c CMakeLists.txt
git commit -m "feat: port pcan_loader and fw_image modules from handler-receiver"
```

---

## Task 3: 实现 `udp_manager`（UDP 8600 配置 + 升级协议）

**目标**：全新写一个**同步 req/resp** 的 UDP 管理器（不复用 handler-receiver 的 RX 线程模型，因本工具 tab1 命令都是单发单收、UI 阻塞即可；升级流式部分由 tab2 worker 线程顺序调用）。覆盖固件 UDP 全部命令：0x04/0x05/0x10-0x13/0x16/0x17/0x18/0x19/0x01-0x03。

**Files:**
- Create: `include/udp_manager.h`
- Create: `src/udp_manager.c`
- Modify: `CMakeLists.txt`（SOURCES 加 `src/udp_manager.c`）

**Interfaces:**
- Consumes: Winsock2。
- Produces: 见下方头文件完整声明。关键约定 —— 所有 `Set_*`/`Get_*` 返回 `bool` 表示"是否收到合法回复"，参数 `out_ok`/out 字段在成功时填充；超时/回复不合法返回 false 且模块内 `last_error` 填中文。`target_ip`/`target_port` 在每个调用显式传入（不绑定单一目标，tab2 升级与 tab1 配置可指向不同设备）。

- [ ] **Step 1: 写 `include/udp_manager.h`**

文件 `include/udp_manager.h`：

```c
#ifndef UDP_MANAGER_H
#define UDP_MANAGER_H

/* winsock2 必须在 windows.h 之前, 否则 winsock1 冲突 */
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdint.h>
#include <stdbool.h>

#define IOEDGE_UDP_PORT        8600   /* 固件配置/UDP升级端口 */
#define IOEDGE_UDP_REPLY_PORT  8601   /* DISCOVER 跨网段回复端口 */
#define IOEDGE_UDP_TIMEOUT_MS  1000   /* 单条命令同步等待超时 */

/* opaque 句柄 */
typedef struct UdpManager UdpManager;

UdpManager *UdpManager_Create(void);
void UdpManager_Destroy(UdpManager *m);
const char *UdpManager_GetLastError(UdpManager *m);

/* --- 升级命令 (0x01-0x03, 小端; 0x04/0x05 同端口) --- */
/* FwStart: 发 [0x01][size LE32][keyhash 32B 可选], 回 [0x01][status].
 * keyhash=NULL 时不带 (兼容旧设备); status: 0=失败 1=成功 2=keyhash 不匹配 */
bool UdpManager_FwStart(UdpManager *m, const char *ip, uint32_t img_size,
                        const uint8_t keyhash[32], uint8_t *out_status);
/* FwData: 发 [0x02][data<=511B], 回 [0x02][offset LE32]. */
bool UdpManager_FwData(UdpManager *m, const char *ip, const uint8_t *data, int len,
                       uint32_t *out_offset);
/* FwEnd: 发 [0x03][test 1B][crc16 LE16], 回 [0x03][result 1B]. */
bool UdpManager_FwEnd(UdpManager *m, const char *ip, uint8_t test, uint16_t crc16,
                      uint8_t *out_result);

/* --- 配置命令 (0x10+, 大端) --- */
bool UdpManager_SetIp(UdpManager *m, const char *ip, uint8_t ip4[4], uint8_t *out_ok);  /* 0x10 */
bool UdpManager_GetNet(UdpManager *m, const char *ip, uint8_t ip4[4],
                       uint8_t *out_slave, uint16_t *out_tcp_port);                      /* 0x11 */
bool UdpManager_SetModbus(UdpManager *m, const char *ip, uint8_t slave_id,
                          uint16_t baud, uint8_t *out_ok);                               /* 0x12 */
bool UdpManager_GetModbus(UdpManager *m, const char *ip, uint8_t *out_slave,
                          uint16_t *out_baud);                                           /* 0x13 */
bool UdpManager_SetCan(UdpManager *m, const char *ip, uint16_t can_id,
                       uint16_t baud_k, uint8_t *out_ok);                                /* 0x16 */
bool UdpManager_GetCan(UdpManager *m, const char *ip, uint16_t *out_can_id,
                       uint16_t *out_baud_k);                                            /* 0x17 */
/* DISCOVER (0x18): 向所有本机网卡子网定向广播发送, 单播+8601 监听回复.
 * out 一次性填所有回复: "io-edge-hub <ip> v0.1.0_xxxxxx" 一行一条, '\n' 分隔.
 * out_cap 为 out 缓冲字节. 返回 true=至少发现 1 台. */
bool UdpManager_Discover(UdpManager *m, char *out, int out_cap, int *out_count);
bool UdpManager_FactoryReset(UdpManager *m, const char *ip, uint8_t *out_ok);            /* 0x19 */
bool UdpManager_GetVersion(UdpManager *m, const char *ip, char *out_ver, int out_cap);   /* 0x04 */
bool UdpManager_Reboot(UdpManager *m, const char *ip);                                   /* 0x05 */

/* CRC16-CCITT (poly 0x1021, init 0x0000), 与 Zephyr crc16_ccitt 对齐 (UDP 升级用). */
uint16_t UdpManager_CRC16_CCITT(const uint8_t *data, size_t len);

#endif /* UDP_MANAGER_H */
```

- [ ] **Step 2: 写 `src/udp_manager.c`**（实现核心：单 socket bind 本地随机端口 + sendto + recvfrom 超时）

文件 `src/udp_manager.c`（要点逐项给代码）：

```c
#include "udp_manager.h"
#include <iphlpapi.h>
#include <stdio.h>
#include <string.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")

struct UdpManager {
	SOCKET sock;                /* UDP socket, bind 0.0.0.0:0 */
	char last_error[128];
};

/* ---------- 内部: 收发原语 ---------- */

/* 发 cmd 到 ip:8600, 阻塞等回复 (1s), 校验回复首字节 == cmd.
 * req: 含 cmd 字节的完整请求. resp/out_resp_len: 回复 (含 echo cmd).
 * 返回 true=收到合法回复. */
static bool send_recv(UdpManager *m, const char *ip, uint8_t cmd,
                      const uint8_t *req, int req_len,
                      uint8_t *resp, int *out_resp_len)
{
	struct sockaddr_in dst = {0};
	dst.sin_family = AF_INET;
	dst.sin_port = htons(IOEDGE_UDP_PORT);
	dst.sin_addr.s_addr = inet_addr(ip);
	if (dst.sin_addr.s_addr == INADDR_NONE) {
		sprintf(m->last_error, "非法 IP: %s", ip);
		return false;
	}

	if (sendto(m->sock, (const char *)req, req_len, 0,
	           (struct sockaddr *)&dst, sizeof(dst)) == SOCKET_ERROR) {
		sprintf(m->last_error, "sendto 失败: %d", WSAGetLastError());
		return false;
	}

	/* 设置 1s 接收超时 */
	DWORD tmo = IOEDGE_UDP_TIMEOUT_MS;
	setsockopt(m->sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tmo, sizeof(tmo));

	struct sockaddr_in from = {0};
	int fromlen = sizeof(from);
	int n = recvfrom(m->sock, (char *)resp, 128, 0,
	                 (struct sockaddr *)&from, &fromlen);
	if (n <= 0) {
		sprintf(m->last_error, "设备无响应 (timeout)");
		return false;
	}
	if (resp[0] != cmd || n < 1) {
		sprintf(m->last_error, "回复格式错误");
		return false;
	}
	*out_resp_len = n;
	return true;
}

/* ---------- 生命周期 ---------- */

UdpManager *UdpManager_Create(void)
{
	UdpManager *m = (UdpManager *)calloc(1, sizeof(*m));
	if (!m) return NULL;
	m->sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (m->sock == INVALID_SOCKET) {
		free(m);
		return NULL;
	}
	/* bind 本地任意端口 (源端口由 OS 分配, 固件回复到源端口) */
	struct sockaddr_in local = {0};
	local.sin_family = AF_INET;
	local.sin_addr.s_addr = INADDR_ANY;
	bind(m->sock, (struct sockaddr *)&local, sizeof(local));
	/* 允许广播 */
	BOOL bc = TRUE;
	setsockopt(m->sock, SOL_SOCKET, SO_BROADCAST, (const char *)&bc, sizeof(bc));
	return m;
}

void UdpManager_Destroy(UdpManager *m)
{
	if (!m) return;
	if (m->sock != INVALID_SOCKET) closesocket(m->sock);
	free(m);
}

const char *UdpManager_GetLastError(UdpManager *m)
{
	return m ? m->last_error : "NULL manager";
}
```

然后实现各命令。**升级命令组（小端）**：

```c
bool UdpManager_FwStart(UdpManager *m, const char *ip, uint32_t img_size,
                        const uint8_t keyhash[32], uint8_t *out_status)
{
	uint8_t req[1 + 4 + 32];
	int reqlen = 1 + 4;
	req[0] = 0x01;
	req[1] = (uint8_t)(img_size);          /* LE32 */
	req[2] = (uint8_t)(img_size >> 8);
	req[3] = (uint8_t)(img_size >> 16);
	req[4] = (uint8_t)(img_size >> 24);
	if (keyhash) {
		memcpy(req + 5, keyhash, 32);
		reqlen += 32;
	}
	uint8_t resp[64];
	int rn = 0;
	if (!send_recv(m, ip, 0x01, req, reqlen, resp, &rn)) return false;
	if (rn < 2) { sprintf(m->last_error, "FW_START 回复过短"); return false; }
	if (out_status) *out_status = resp[1];
	return true;
}

bool UdpManager_FwData(UdpManager *m, const char *ip, const uint8_t *data, int len,
                       uint32_t *out_offset)
{
	if (len > 511) { sprintf(m->last_error, "FW_DATA 单块超 511B"); return false; }
	uint8_t req[1 + 511];
	req[0] = 0x02;
	memcpy(req + 1, data, len);
	uint8_t resp[64];
	int rn = 0;
	if (!send_recv(m, ip, 0x02, req, 1 + len, resp, &rn)) return false;
	if (rn < 5) { sprintf(m->last_error, "FW_DATA 回复过短"); return false; }
	if (out_offset) *out_offset = (uint32_t)resp[1] | ((uint32_t)resp[2] << 8) |
	                              ((uint32_t)resp[3] << 16) | ((uint32_t)resp[4] << 24);
	return true;
}

bool UdpManager_FwEnd(UdpManager *m, const char *ip, uint8_t test, uint16_t crc16,
                      uint8_t *out_result)
{
	uint8_t req[4];
	req[0] = 0x03;
	req[1] = test;
	req[2] = (uint8_t)(crc16);       /* LE16 */
	req[3] = (uint8_t)(crc16 >> 8);
	uint8_t resp[64];
	int rn = 0;
	if (!send_recv(m, ip, 0x03, req, 4, resp, &rn)) return false;
	if (rn < 2) { sprintf(m->last_error, "FW_END 回复过短"); return false; }
	if (out_result) *out_result = resp[1];
	return true;
}
```

**配置命令组（大端）**——以 `SetModbus`/`GetModbus`/`SetCan`/`GetCan` 为例（其余类同，注意 0x10 SET_IP 触发设备延迟重启、0x18 DISCOVER 用广播）：

```c
bool UdpManager_SetModbus(UdpManager *m, const char *ip, uint8_t slave_id,
                          uint16_t baud, uint8_t *out_ok)
{
	uint8_t req[4];
	req[0] = 0x12;
	req[1] = slave_id;
	req[2] = (uint8_t)(baud >> 8);    /* BE16 */
	req[3] = (uint8_t)(baud);
	uint8_t resp[64];
	int rn = 0;
	if (!send_recv(m, ip, 0x12, req, 4, resp, &rn)) return false;
	if (out_ok) *out_ok = resp[1];
	return true;
}

bool UdpManager_GetModbus(UdpManager *m, const char *ip, uint8_t *out_slave,
                          uint16_t *out_baud)
{
	uint8_t req[1] = { 0x13 };
	uint8_t resp[64];
	int rn = 0;
	if (!send_recv(m, ip, 0x13, req, 1, resp, &rn)) return false;
	if (rn < 4) { sprintf(m->last_error, "GET_MODBUS 回复过短"); return false; }
	if (out_slave) *out_slave = resp[1];
	if (out_baud)  *out_baud  = ((uint16_t)resp[2] << 8) | resp[3]; /* BE16 */
	return true;
}

bool UdpManager_SetCan(UdpManager *m, const char *ip, uint16_t can_id,
                       uint16_t baud_k, uint8_t *out_ok)
{
	uint8_t req[5];
	req[0] = 0x16;
	req[1] = (uint8_t)(can_id >> 8);   /* BE16 */
	req[2] = (uint8_t)(can_id);
	req[3] = (uint8_t)(baud_k >> 8);   /* BE16 */
	req[4] = (uint8_t)(baud_k);
	uint8_t resp[64];
	int rn = 0;
	if (!send_recv(m, ip, 0x16, req, 5, resp, &rn)) return false;
	if (out_ok) *out_ok = resp[1];
	return true;
}

bool UdpManager_GetCan(UdpManager *m, const char *ip, uint16_t *out_can_id,
                       uint16_t *out_baud_k)
{
	uint8_t req[1] = { 0x17 };
	uint8_t resp[64];
	int rn = 0;
	if (!send_recv(m, ip, 0x17, req, 1, resp, &rn)) return false;
	if (rn < 5) { sprintf(m->last_error, "GET_CAN 回复过短"); return false; }
	if (out_can_id) *out_can_id = ((uint16_t)resp[1] << 8) | resp[2];
	if (out_baud_k) *out_baud_k = ((uint16_t)resp[3] << 8) | resp[4];
	return true;
}
```

`SetIp`/`GetNet`/`FactoryReset`/`GetVersion`/`Reboot` 按协议同法实现：

```c
bool UdpManager_SetIp(UdpManager *m, const char *ip, uint8_t ip4[4], uint8_t *out_ok)
{
	uint8_t req[5];
	req[0] = 0x10;
	memcpy(req + 1, ip4, 4);
	uint8_t resp[64];
	int rn = 0;
	if (!send_recv(m, ip, 0x10, req, 5, resp, &rn)) return false;
	if (out_ok) *out_ok = resp[1];
	return true;
}

bool UdpManager_GetNet(UdpManager *m, const char *ip, uint8_t ip4[4],
                       uint8_t *out_slave, uint16_t *out_tcp_port)
{
	uint8_t req[1] = { 0x11 };
	uint8_t resp[64];
	int rn = 0;
	if (!send_recv(m, ip, 0x11, req, 1, resp, &rn)) return false;
	/* 回 [0x11][ip 4B][slave 1B][tcp_port 2B BE] */
	if (rn < 8) { sprintf(m->last_error, "GET_NET 回复过短"); return false; }
	if (ip4)        memcpy(ip4, resp + 1, 4);
	if (out_slave)  *out_slave = resp[5];
	if (out_tcp_port) *out_tcp_port = ((uint16_t)resp[6] << 8) | resp[7];
	return true;
}

bool UdpManager_FactoryReset(UdpManager *m, const char *ip, uint8_t *out_ok)
{
	uint8_t req[1] = { 0x19 };
	uint8_t resp[64];
	int rn = 0;
	if (!send_recv(m, ip, 0x19, req, 1, resp, &rn)) return false;
	if (out_ok) *out_ok = resp[1];
	return true;
}

bool UdpManager_GetVersion(UdpManager *m, const char *ip, char *out_ver, int out_cap)
{
	uint8_t req[1] = { 0x04 };
	uint8_t resp[64];
	int rn = 0;
	if (!send_recv(m, ip, 0x04, req, 1, resp, &rn)) return false;
	/* 回 [0x04][ASCII 版本串, 无 NUL] */
	int vlen = rn - 1;
	if (vlen <= 0) { sprintf(m->last_error, "GET_VERSION 空回复"); return false; }
	if (vlen >= out_cap) vlen = out_cap - 1;
	memcpy(out_ver, resp + 1, vlen);
	out_ver[vlen] = 0;
	return true;
}

bool UdpManager_Reboot(UdpManager *m, const char *ip)
{
	uint8_t req[1] = { 0x05 };
	uint8_t resp[64];
	int rn = 0;
	/* REBOOT 回复空也认为成功 (设备已重启) */
	send_recv(m, ip, 0x05, req, 1, resp, &rn);
	return true;  /* reboot 不强求回复 */
}
```

`Discover` 需要遍历本机网卡做子网定向广播 + 同时监听 8601 跨网段回复。**直接复用 handler-receiver `src/udp_manager.c` 的 `collect_broadcast_addrs()`**（复制到本文件作 static helper），然后：

```c
/* 收集本机各非回环网卡的子网定向广播地址 (复制自 handler-receiver src/udp_manager.c). */
static int collect_broadcast_addrs(unsigned long *addrs, int max_cnt);

bool UdpManager_Discover(UdpManager *m, char *out, int out_cap, int *out_count)
{
	unsigned long bcasts[16];
	int nb = collect_broadcast_addrs(bcasts, 16);
	uint8_t req[1] = { 0x18 };

	/* 发定向广播到所有网卡:8600 */
	struct sockaddr_in dst = {0};
	dst.sin_family = AF_INET;
	dst.sin_port = htons(IOEDGE_UDP_PORT);
	for (int i = 0; i < nb; i++) {
		dst.sin_addr.s_addr = bcasts[i];
		sendto(m->sock, (const char *)req, 1, 0, (struct sockaddr *)&dst, sizeof(dst));
	}

	/* 监听 800ms: 同子网单播回源端口 / 跨子网发 8601.
	 * 简化: 同 socket 也能收单播; 8601 单独建临时 socket 收. */
	DWORD tmo = 800;
	setsockopt(m->sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tmo, sizeof(tmo));
 SOCKET s86 = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	struct sockaddr_in a86 = {0};
	a86.sin_family = AF_INET;
	a86.sin_port = htons(IOEDGE_UDP_REPLY_PORT);
	a86.sin_addr.s_addr = INADDR_ANY;
	bind(s86, (struct sockaddr *)&a86, sizeof(a86));
	setsockopt(s86, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tmo, sizeof(tmo));

	int cnt = 0;
	out[0] = 0;
	uint8_t buf[128];
	struct sockaddr_in from; int fl = sizeof(from);
	int n;
	time_t end = time(NULL) + 1;
	while (time(NULL) < end) {
		/* 轮询主 socket 与 8601 socket */
		n = recvfrom(m->sock, (char *)buf, sizeof(buf), 0, (struct sockaddr *)&from, &fl);
		if (n > 1 && buf[0] == 0x18) {
			char line[80];
			char ipstr[16];
			inet_ntop4_r(from.sin_addr, ipstr);  /* 见下方 helper */
			/* 固件回复 "io-edge-hub <a.b.c.d> vX.X.X_xxxxxx" (≤63B) */
			int ln = (n - 1 < 63) ? n - 1 : 63;
			memcpy(line, buf + 1, ln); line[ln] = 0;
			cnt++;
			strncat(out, line, out_cap - strlen(out) - 1);
			strncat(out, "\n", out_cap - strlen(out) - 1);
		}
		n = recvfrom(s86, (char *)buf, sizeof(buf), 0, (struct sockaddr *)&from, &fl);
		if (n > 1 && buf[0] == 0x18) {
			/* 同上处理 */
		}
	}
	closesocket(s86);
	if (out_count) *out_count = cnt;
	if (cnt == 0) sprintf(m->last_error, "未发现设备");
	return cnt > 0;
}
```

注：Discover 实现较繁琐，**实现时允许参照 handler-receiver 的 Discover 现成代码**（路径 `~/code/handler-receiver/src/udp_manager.c` 中查找 DISCOVER 相关函数）。helper `inet_ntop4_r` 用 `inet_ntoa` 替代即可：`strcpy(ipstr, inet_ntoa(from.sin_addr));`

最后 CRC16-CCITT，**原样复制 handler-receiver 的 `UdpManager_CRC16_CCITT` 实现**（与 Zephyr `crc16_ccitt` 对齐）。

- [ ] **Step 3: 修改 `CMakeLists.txt` 的 SOURCES**

```cmake
set(SOURCES
    src/main.c
    src/pcan_loader.c
    src/fw_image.c
    src/udp_manager.c
)
```

- [ ] **Step 4: 构建验证**

Run:
```bash
cmake --build out --config Release
```
Expected: 编译通过。注意：`#include <winsock2.h>` 必须在 `<windows.h>` 前（头文件已按此顺序，但若 `main.c` 或其他文件先 include `windows.h` 再 include `udp_manager.h` 会触发 winsock1 冲突——确保 `main.c` 第一个 include 就是 `<winsock2.h>`）。

- [ ] **Step 5: Commit**

```bash
git add include/udp_manager.h src/udp_manager.c CMakeLists.txt
git commit -m "feat: implement udp_manager with UDP 8600 config + fw-upgrade protocol"
```

---

## Task 4: 实现 `can_manager`（PCAN-USB + CAN 升级协议）

**目标**：迁移 handler-receiver 的 `can_manager` 并把帧 ID 适配为 io-edge-hub 的 0x101-0x105，命令码/回复码改用 io-edge-hub 的协议。

**Files:**
- Create: `include/can_manager.h`（参照 handler-receiver 适配）
- Create: `src/can_manager.c`（参照 handler-receiver 适配）
- Modify: `CMakeLists.txt`（SOURCES 加 `src/can_manager.c`）

**Interfaces:**
- Consumes: `pcan_loader.h`（运行时 `Pcan_*` 函数指针）、`fw_image.h`（无，CAN 不用 CRC）。
- Produces: 见头文件。

**关键适配点**（io-edge-hub CAN 协议，源自固件 `libs/can_fw_upgrade/can_fw_upgrade.c`）：

| 帧 ID | 用途 | 布局 |
|---|---|---|
| 0x101 | 命令 (主→设) | `data_32[0]`=cmd LE32 (0=START,1=CONFIRM,2=VERSION,3=REBOOT), `data_32[1]`=arg LE32 |
| 0x102 | 回复 (设→主) | `data_32[0]`=code LE32 (0=OFFSET,1=UPDATE_SUCCESS,2=VERSION,3=CONFIRM,4=FLASH_ERROR,5=TRANSFER_ERROR,6=KEYHASH_ERROR), `data_32[1]`=offset LE32 |
| 0x103 | 数据 (主→设) | ≤8B 原始 |
| 0x104 | keyhash 分片 (主→设) | `data[0]`=seq 0..4, `data[1..7]`=7B |
| 0x105 | 版本分片 (设→主) | `data[0]`=seq, `data[1..7]`=ASCII |

handler-receiver 的帧 ID 是另一套（gateway 协议），**必须改**。

- [ ] **Step 1: 阅读 handler-receiver 的 can_manager 作骨架**

阅读 `~/code/handler-receiver/include/can_manager.h` 与 `src/can_manager.c`，理解其：PCAN 通道枚举、`Connect`、`Read`/`Write`、`FirmwareUpgrade`、`GetVersion`、`Reboot`、`GetLastError` 结构。本 task 不复制粘贴，而是**新写一份只含 io-edge-hub 所需 API、复用其 PCAN 调用框架**（`Pcan_Initialize` / `Pcan_Write` / `Pcan_Read` 等通过 `pcan_loader` 调）。

- [ ] **Step 2: 写 `include/can_manager.h`**

文件 `include/can_manager.h`：

```c
#ifndef CAN_MANAGER_H
#define CAN_MANAGER_H

#include <windows.h>
#include <stdint.h>
#include <stdbool.h>

/* io-edge-hub CAN 固件升级帧 ID (固件 libs/can_fw_upgrade/can_fw_upgrade.c) */
#define CAN_ID_IO_CMD      0x101   /* 主→设 命令 */
#define CAN_ID_IO_RESP     0x102   /* 设→主 回复 */
#define CAN_ID_IO_DATA     0x103   /* 主→设 固件数据 */
#define CAN_ID_IO_KEYHASH  0x104   /* 主→设 keyhash 分片 */
#define CAN_ID_IO_VERSION  0x105   /* 设→主 版本分片 */

#define CAN_IO_DEFAULT_BITRATE  250000

typedef struct CanManager CanManager;
typedef void (*can_progress_cb)(int percent, void *user);

CanManager *CanManager_Create(void);
void CanManager_Destroy(CanManager *m);
const char *CanManager_GetLastError(CanManager *m);

/* 探测 PCAN-USB: 返回可用通道数 (Pcan_Load + 枚举 0..15 调 Initialize 测试) */
bool CanManager_DetectDevice(CanManager *m, int *out_channel);
/* 连接指定通道 (channel=Pcan 通道号, 0-based), 失败 last_error 填 PCAN 错误码 */
bool CanManager_Connect(CanManager *m, int channel, uint32_t bitrate);
void CanManager_Disconnect(CanManager *m);
bool CanManager_IsConnected(const CanManager *m);

/* 完整升级流程 (阻塞, 调用方在 worker 线程调):
 * 1. keyhash!=NULL → 发 5 帧 0x104
 * 2. START (0x101 cmd=0, arg=size) → 等 OFFSET(0)/FLASH_ERROR/KEYHASH_ERROR
 * 3. 流式 0x103 (8B/帧), 每 64B 设备回 OFFSET 做流控, 总量满回 UPDATE_SUCCESS
 * 4. CONFIRM (0x101 cmd=1, arg=permanent?1:0) → 等 CONFIRM(0x55AA55AA)/TRANSFER_ERROR
 * progress 回调 0-100, user 透传 */
bool CanManager_FirmwareUpgrade(CanManager *m, const uint8_t *img, uint32_t size,
                                const uint8_t keyhash[32], bool permanent,
                                can_progress_cb progress, void *user);
bool CanManager_GetVersion(CanManager *m, char *out_ver, int out_cap);
bool CanManager_Reboot(CanManager *m);

#endif /* CAN_MANAGER_H */
```

- [ ] **Step 3: 写 `src/can_manager.c`**

文件 `src/can_manager.c`。结构：opaque struct 持有 `connected`、`channel`、`bitrate`、`last_error[128]`。所有 PCAN API 通过 `pcan_loader.h` 的 `extern` 函数指针调用（`Pcan_Initialize` 等）。

```c
#include "can_manager.h"
#include "pcan_loader.h"
#include <stdio.h>
#include <string.h>

struct CanManager {
	bool connected;
	int channel;
	uint32_t bitrate;
	char last_error[128];
};

CanManager *CanManager_Create(void) { return (CanManager *)calloc(1, sizeof(CanManager)); }
void CanManager_Destroy(CanManager *m) { if (m) { CanManager_Disconnect(m); free(m); } }
const char *CanManager_GetLastError(CanManager *m) { return m ? m->last_error : "NULL"; }

bool CanManager_DetectDevice(CanManager *m, int *out_channel)
{
	if (!Pcan_Load() || !Pcan_Available()) {
		sprintf(m->last_error, "未加载 PCANBasic.dll, 请安装 PCAN-Basic");
		return false;
	}
	/* 枚举通道 0..15 试 Initialize (USB 通道通常是 3=USB1) */
	for (int ch = 0; ch < 16; ch++) {
		uint32_t st = Pcan_Initialize(ch, m->bitrate ? m->bitrate : CAN_IO_DEFAULT_BITRATE, 0, 0, 0);
		if (st == 0 /* PCAN_ERROR_OK */) {
			Pcan_Uninitialize(ch);
			if (out_channel) *out_channel = ch;
			return true;
		}
	}
	sprintf(m->last_error, "未检测到 PCAN-USB 设备");
	return false;
}

bool CanManager_Connect(CanManager *m, int channel, uint32_t bitrate)
{
	if (!Pcan_Load() || !Pcan_Available()) {
		sprintf(m->last_error, "未加载 PCANBasic.dll");
		return false;
	}
	uint32_t st = Pcan_Initialize(channel, bitrate, 0, 0, 0);
	if (st != 0) {
		sprintf(m->last_error, "PCAN Initialize 失败: 0x%X", st);
		return false;
	}
	m->channel = channel;
	m->bitrate = bitrate;
	m->connected = true;
	return true;
}

void CanManager_Disconnect(CanManager *m)
{
	if (m && m->connected) {
		Pcan_Uninitialize(m->channel);
		m->connected = false;
	}
}

bool CanManager_IsConnected(const CanManager *m) { return m && m->connected; }
```

辅助：发送/接收 CAN 帧（PCAN 帧 ID 用 `TPCANMsg`，参考 handler-receiver 的封装）。`FirmwareUpgrade` 的关键片段：

```c
/* 写一帧: 11-bit std ID, DLC, data */
static bool can_write(CanManager *m, uint32_t id, const uint8_t *data, int dlc)
{
	TPCANMsg msg;
	msg.ID = id;
	msg.MSGTYPE = 0x02; /* PCAN_MESSAGE_STANDARD */
	msg.LEN = (uint8_t)dlc;
	memcpy(msg.DATA, data, dlc);
	uint32_t st = Pcan_Write(m->channel, &msg);
	if (st != 0) { sprintf(m->last_error, "PCAN Write 失败: 0x%X", st); return false; }
	return true;
}

/* 读一帧 (timeout_ms), 过滤 ID==expect_id 的才返回; out_code/out_arg 解 0x102 */
static bool can_read_resp(CanManager *m, uint32_t expect_id, int timeout_ms,
                          uint32_t *out_code, uint32_t *out_arg)
{
	TPCANMsg msg;
	TPCANTimestamp ts;
	DWORD end = GetTickCount() + timeout_ms;
	while ((long)(end - GetTickCount()) > 0) {
		uint32_t st = Pcan_Read(m->channel, &msg, &ts);
		if (st == 0 && msg.ID == expect_id) {
			uint32_t code = msg.DATA[0] | (msg.DATA[1]<<8) | (msg.DATA[2]<<16) | ((uint32_t)msg.DATA[3]<<24);
			uint32_t arg  = msg.DATA[4] | (msg.DATA[5]<<8) | (msg.DATA[6]<<16) | ((uint32_t)msg.DATA[7]<<24);
			if (out_code) *out_code = code;
			if (out_arg)  *out_arg  = arg;
			return true;
		}
		Sleep(1);
	}
	sprintf(m->last_error, "CAN 回复超时");
	return false;
}

bool CanManager_FirmwareUpgrade(CanManager *m, const uint8_t *img, uint32_t size,
                                const uint8_t keyhash[32], bool permanent,
                                can_progress_cb progress, void *user)
{
	/* 1. keyhash (5 帧 0x104) */
	if (keyhash) {
		for (int seq = 0; seq < 5; seq++) {
			uint8_t fr[8] = {0};
			fr[0] = (uint8_t)seq;
			int n = 32 - seq * 7;
			if (n > 7) n = 7;
			memcpy(fr + 1, keyhash + seq * 7, n);
			if (!can_write(m, CAN_ID_IO_KEYHASH, fr, 8)) return false;
		}
	}
	/* 2. START */
	{
		uint8_t fr[8] = {0};
		fr[0] = 0; /* cmd=START */
		uint32_t sz = size;
		memcpy(fr + 4, &sz, 4); /* LE32 */
		if (!can_write(m, CAN_ID_IO_CMD, fr, 8)) return false;
		uint32_t code = 0, arg = 0;
		if (!can_read_resp(m, CAN_ID_IO_RESP, 2000, &code, &arg)) return false;
		if (code == 4) { sprintf(m->last_error, "FLASH 错误"); return false; }
		if (code == 6) { sprintf(m->last_error, "keyhash 不匹配"); return false; }
		if (code != 0) { sprintf(m->last_error, "START 未知回复 code=%u", code); return false; }
	}
	/* 3. 流式 DATA (8B/帧), 每 64B 设备回 OFFSET */
	uint32_t off = 0;
	while (off < size) {
		int n = size - off; if (n > 8) n = 8;
		if (!can_write(m, CAN_ID_IO_DATA, img + off, n)) return false;
		off += n;
		/* 每 8 帧 (64B) 读一次 OFFSET 做流控 */
		if ((off % 64) == 0 && off < size) {
			uint32_t code = 0, arg = 0;
			if (can_read_resp(m, CAN_ID_IO_RESP, 2000, &code, &arg, 0)) {
				if (code == 4) { sprintf(m->last_error, "FLASH 错误"); return false; }
			}
		}
		if (progress) progress((int)(off * 90 / size), user); /* 流式占 0-90% */
	}
	/* 等 UPDATE_SUCCESS */
	{
		uint32_t code = 0, arg = 0;
		if (!can_read_resp(m, CAN_ID_IO_RESP, 2000, &code, &arg)) return false;
		if (code == 5) { sprintf(m->last_error, "TRANSFER 错误 (大小不匹配)"); return false; }
		if (code != 1) { sprintf(m->last_error, "DATA 结束未知 code=%u", code); return false; }
	}
	/* 4. CONFIRM */
	{
		uint8_t fr[8] = {0};
		fr[0] = 1; /* cmd=CONFIRM */
		fr[4] = permanent ? 1 : 0;
		if (!can_write(m, CAN_ID_IO_CMD, fr, 8)) return false;
		uint32_t code = 0, arg = 0;
		if (!can_read_resp(m, CAN_ID_IO_RESP, 2000, &code, &arg)) return false;
		if (code != 3 || arg != 0x55AA55AA) { sprintf(m->last_error, "CONFIRM 失败"); return false; }
	}
	if (progress) progress(100, user);
	return true;
}
```

`GetVersion` 与 `Reboot` 类似（`0x101 cmd=2/3`，GetVersion 还要收 0x105 分片拼接）。

注：上面 `can_read_resp(m, ..., &code, &arg, 0)` 多了一个参数是笔误，实际实现时函数签名是 `(m, expect_id, timeout, out_code, out_arg)`——去掉末尾 `0`。实现时仔细对齐签名。

- [ ] **Step 4: 修改 `CMakeLists.txt`**

```cmake
set(SOURCES
    src/main.c
    src/pcan_loader.c
    src/fw_image.c
    src/udp_manager.c
    src/can_manager.c
)
```

- [ ] **Step 5: 构建验证**

Run:
```bash
cmake --build out --config Release
```
Expected: 编译通过。PCAN 类型（`TPCANMsg` 等）由 `pcan_loader.h` 定义（迁移自 handler-receiver）。

- [ ] **Step 6: Commit**

```bash
git add include/can_manager.h src/can_manager.c CMakeLists.txt
git commit -m "feat: implement can_manager for io-edge-hub CAN fw-upgrade protocol"
```

---

## Task 5: 实现 `modbus_client`（Modbus TCP + RTU 主机）

**目标**：全新实现一个不依赖第三方库的 Modbus 主机，支持 TCP（端口 502）和 RTU（PC 串口 8N1），覆盖 FC01/02/03/04/05/06/16。

**Files:**
- Create: `include/modbus_client.h`
- Create: `src/modbus_client.c`
- Modify: `CMakeLists.txt`（SOURCES 加 `src/modbus_client.c`）

**Interfaces:**
- Consumes: Winsock2（TCP）、Win32 串口 API（RTU）。
- Produces: 见头文件。

- [ ] **Step 1: 写 `include/modbus_client.h`**

文件 `include/modbus_client.h`：

```c
#ifndef MODBUS_CLIENT_H
#define MODBUS_CLIENT_H

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdint.h>
#include <stdbool.h>

typedef enum { MB_TCP, MB_RTU } MbTransport;

typedef struct MbClient MbClient;

MbClient *MbClient_Create(void);
void MbClient_Destroy(MbClient *m);
const char *MbClient_GetLastError(MbClient *m);
bool MbClient_IsConnected(const MbClient *m);
MbTransport MbClient_GetTransport(const MbClient *m);

/* TCP: 连 ip:port (默认 502), 后续读写用 unit_id */
bool MbClient_ConnectTcp(MbClient *m, const char *ip, uint16_t port, uint8_t unit_id);
/* RTU: 打开串口 (如 "\\\\.\\COM3"), 8N1, baud */
bool MbClient_ConnectRtu(MbClient *m, const wchar_t *com_port, uint32_t baud, uint8_t unit_id);
void MbClient_Disconnect(MbClient *m);

/* FC01 读线圈 (DO). out_bits: 每元素 0/1, 调用者按 qty 分配. */
bool MbClient_ReadCoils(MbClient *m, uint16_t addr, uint16_t qty, uint8_t *out_bits);
/* FC02 读离散输入 (DI) */
bool MbClient_ReadDiscreteInputs(MbClient *m, uint16_t addr, uint16_t qty, uint8_t *out_bits);
/* FC03 读保持寄存器. out_regs: 调用者按 qty 分配 uint16_t[] */
bool MbClient_ReadHolding(MbClient *m, uint16_t addr, uint16_t qty, uint16_t *out_regs);
/* FC04 读输入寄存器 (AI) */
bool MbClient_ReadInput(MbClient *m, uint16_t addr, uint16_t qty, uint16_t *out_regs);
/* FC05 写单线圈 */
bool MbClient_WriteSingleCoil(MbClient *m, uint16_t addr, bool on);
/* FC06 写单保持寄存器 */
bool MbClient_WriteSingleReg(MbClient *m, uint16_t addr, uint16_t value);
/* FC16 写多保持寄存器 */
bool MbClient_WriteMultiReg(MbClient *m, uint16_t addr, uint16_t qty, const uint16_t *values);

#endif /* MODBUS_CLIENT_H */
```

- [ ] **Step 2: 写 `src/modbus_client.c`**

文件 `src/modbus_client.c`。结构要点：

```c
#include "modbus_client.h"
#include <stdio.h>
#include <string.h>

struct MbClient {
	MbTransport transport;
	bool connected;
	uint8_t unit_id;
	/* TCP */
	SOCKET sock;
	uint16_t tcp_tid;        /* MBAP transaction id, 自增 */
	/* RTU */
	HANDLE hCom;
	uint32_t baud;
	char last_error[128];
};

/* CRC16-Modbus (poly 0xA001, init 0xFFFF, 反向) */
static uint16_t crc16_modbus(const uint8_t *data, int len)
{
	uint16_t crc = 0xFFFF;
	for (int i = 0; i < len; i++) {
		crc ^= data[i];
		for (int j = 0; j < 8; j++) {
			crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : (crc >> 1);
		}
	}
	return crc;
}
```

公共 PDU 传输层（TCP/RTU 各包装一层）：

```c
/* 发请求 PDU (fc + pdu, pdulen), 收响应 PDU (跳过 MBAP/RTU 头与 CRC), out_pdu 含 fc.
 * 返回响应 PDU 总长 (含 fc); 0=失败. 异常响应 (fc|0x80) 填 last_error. */
static int mb_transact(MbClient *m, const uint8_t *pdu, int pdulen,
                       uint8_t *out_pdu, int out_cap)
{
	if (m->transport == MB_TCP) {
		/* MBAP: tid(2 BE)+pid(2=0)+len(2 BE)+uid(1) */
		uint8_t adu[260];
		uint16_t tid = m->tcp_tid++;
		uint16_t len = pdulen + 1; /* uid + pdu */
		adu[0] = tid >> 8; adu[1] = tid;
		adu[2] = 0; adu[3] = 0;       /* protocol id = 0 (Modbus) */
		adu[4] = len >> 8; adu[5] = len;
		adu[6] = m->unit_id;
		memcpy(adu + 7, pdu, pdulen);
		int adulen = 7 + pdulen;
		if (send(m->sock, (const char *)adu, adulen, 0) != adulen) {
			sprintf(m->last_error, "TCP 发送失败"); return 0;
		}
		/* 收 MBAP + 响应 */
		int n = 0;
		while (n < 6) {
			int r = recv(m->sock, (char *)adu + n, 6 - n, 0);
			if (r <= 0) { sprintf(m->last_error, "TCP 响应超时"); return 0; }
			n += r;
		}
		uint16_t rlen = ((uint16_t)adu[4] << 8) | adu[5];
		int want = rlen; /* uid + pdu */
		int got = 0;
		while (got < want) {
			int r = recv(m->sock, (char *)out_pdu + got, want - got, 0);
			if (r <= 0) { sprintf(m->last_error, "TCP 响应中断"); return 0; }
			got += r;
		}
		/* out_pdu[0] 跳过 uid, 实际 pdu 从 out_pdu[1] */
		memmove(out_pdu, out_pdu + 1, got - 1);
		return got - 1;
	} else {
		/* RTU: addr(1) + pdu + crc(2) */
		uint8_t adu[260];
		adu[0] = m->unit_id;
		memcpy(adu + 1, pdu, pdulen);
		uint16_t crc = crc16_modbus(adu, 1 + pdulen);
		adu[1 + pdulen] = crc & 0xFF;
		adu[2 + pdulen] = crc >> 8;
		DWORD wr;
		WriteFile(m->hCom, adu, 3 + pdulen, &wr, NULL);
		/* 3.5 字符间隔 (简化: Sleep 按 baud 计算) */
		Sleep(2);
		/* 读响应: 先读 2 (addr+fc), 若异常 fc|0x80 再读 3 (ec+crc); 正常按 fc 解析长度 */
		DWORD rd = 0;
		uint8_t hdr[2];
		ReadFile(m->hCom, hdr, 2, &rd, NULL);
		if (rd < 2) { sprintf(m->last_error, "RTU 响应超时"); return 0; }
		int rest;
		uint8_t fc = hdr[1];
		if (fc & 0x80) rest = 3; /* ec + crc16 */
		else if (fc == 0x01 || fc == 0x02) {
			/* 需先读 byte count */
			uint8_t bc;
			ReadFile(m->hCom, &bc, 1, &rd, NULL);
			rest = bc + 2; /* data + crc16 */
			out_pdu[0] = fc; out_pdu[1] = bc;
			ReadFile(m->hCom, out_pdu + 2, bc + 2, &rd, NULL);
			return 2 + bc;
		}
		else if (fc == 0x03 || fc == 0x04) { /* 同上, byte count + data + crc */ /* 类似实现 */ rest = 0; }
		else rest = 4; /* FC05/06/16: 8 字节总 (已读 2, 剩 6 含crc) 实际为 6 */
		/* 简化: 对 FC05/06/16 响应固定 8 字节, 已读 2, 再读 6 */
		/* 实现时按 fc 分别处理 */
		/* ... 此处实现略, 见 step 说明 ... */
		return 0; /* 占位, 实现需补全 */
	}
}
```

**注意**：上面 RTU 分支较繁琐，实现时建议把"读 N 字节带超时"封装成 helper（用串口 `COMMTIMEOUTS` 配 ReadTotalTimeoutConstant），然后按 fc 分别算响应长度。**不要留占位 return 0**——这是 plan 失败标记，实现时必须补全每个 fc 的响应读取 + CRC 校验 + exception 解析。

各 FC 包装（用 `mb_transact`）：

```c
bool MbClient_ReadHolding(MbClient *m, uint16_t addr, uint16_t qty, uint16_t *out_regs)
{
	uint8_t pdu[5] = { 0x03, (uint8_t)(addr>>8), (uint8_t)addr, (uint8_t)(qty>>8), (uint8_t)qty };
	uint8_t resp[256];
	int n = mb_transact(m, pdu, 5, resp, sizeof(resp));
	if (n <= 0) return false;
	if (resp[0] & 0x80) { sprintf(m->last_error, "Modbus 异常 code=%d", resp[1]); return false; }
	if (n < 2 + resp[1]) { sprintf(m->last_error, "响应过短"); return false; }
	int bc = resp[1];
	for (int i = 0; i < bc / 2; i++) {
		out_regs[i] = ((uint16_t)resp[2 + i*2] << 8) | resp[3 + i*2];
	}
	return true;
}
```

其余 `ReadCoils`/`ReadDiscreteInputs`（解包按位）、`ReadInput`、`WriteSingleCoil`（on→0xFF00）、`WriteSingleReg`、`WriteMultiReg`（pdu 含 byte count + N×2B）类似。

`ConnectTcp`/`ConnectRtu`/`Disconnect`：

```c
bool MbClient_ConnectTcp(MbClient *m, const char *ip, uint16_t port, uint8_t unit_id)
{
	m->sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	struct sockaddr_in sa = {0};
	sa.sin_family = AF_INET;
	sa.sin_port = htons(port);
	sa.sin_addr.s_addr = inet_addr(ip);
	DWORD tmo = 1000;
	setsockopt(m->sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tmo, sizeof(tmo));
	setsockopt(m->sock, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tmo, sizeof(tmo));
	if (connect(m->sock, (struct sockaddr *)&sa, sizeof(sa)) != 0) {
		sprintf(m->last_error, "TCP 连接失败: %d", WSAGetLastError());
		closesocket(m->sock); m->sock = INVALID_SOCKET;
		return false;
	}
	m->transport = MB_TCP; m->connected = true; m->unit_id = unit_id; m->tcp_tid = 1;
	return true;
}

bool MbClient_ConnectRtu(MbClient *m, const wchar_t *com_port, uint32_t baud, uint8_t unit_id)
{
	m->hCom = CreateFileW(com_port, GENERIC_READ | GENERIC_WRITE, 0, NULL,
	                      OPEN_EXISTING, 0, NULL);
	if (m->hCom == INVALID_HANDLE_VALUE) {
		sprintf(m->last_error, "打开串口失败: %lu", GetLastError());
		return false;
	}
	DCB dcb = { .DCBlength = sizeof(dcb) };
	GetCommState(m->hCom, &dcb);
	dcb.BaudRate = baud;
	dcb.ByteSize = 8;
	dcb.Parity = NOPARITY;
	dcb.StopBits = ONESTOPBIT;
	SetCommState(m->hCom, &dcb);
	COMMTIMEOUTS to = { .ReadIntervalTimeout = 50, .ReadTotalTimeoutConstant = 1000,
	                    .ReadTotalTimeoutMultiplier = 0, .WriteTotalTimeoutConstant = 1000,
	                    .WriteTotalTimeoutMultiplier = 0 };
	SetCommTimeouts(m->hCom, &to);
	m->transport = MB_RTU; m->connected = true; m->unit_id = unit_id; m->baud = baud;
	return true;
}

void MbClient_Disconnect(MbClient *m)
{
	if (m->transport == MB_TCP && m->sock != INVALID_SOCKET) {
		closesocket(m->sock); m->sock = INVALID_SOCKET;
	} else if (m->transport == MB_RTU && m->hCom != INVALID_HANDLE_VALUE) {
		CloseHandle(m->hCom); m->hCom = INVALID_HANDLE_VALUE;
	}
	m->connected = false;
}
```

- [ ] **Step 3: 修改 `CMakeLists.txt`**

```cmake
set(SOURCES
    src/main.c
    src/pcan_loader.c
    src/fw_image.c
    src/udp_manager.c
    src/can_manager.c
    src/modbus_client.c
)
```

- [ ] **Step 4: 构建验证**

Run:
```bash
cmake --build out --config Release
```
Expected: 编译通过。注意头文件 include 顺序：`modbus_client.h` 内 `<winsock2.h>` 在 `<windows.h>` 前。

- [ ] **Step 5: Commit**

```bash
git add include/modbus_client.h src/modbus_client.c CMakeLists.txt
git commit -m "feat: implement modbus_client with TCP + RTU master (FC01-06/16)"
```

---

## Task 6: 实现 Tab1（UDP 参数配置）

**目标**：替换 main.c 中的 `ConfigTab_Create` 占位为完整 tab1 实现：设备发现 + 网络/Modbus/CAN 参数配置 + 版本查询 + 重启 + 出厂重置 + 操作日志。

**Files:**
- Modify: `include/resource.h`（加 tab1 控件 ID 1xxx）
- Create: `include/config_tab.h`
- Create: `src/config_tab.c`
- Modify: `CMakeLists.txt`（SOURCES 加 `src/config_tab.c`）
- Modify: `src/main.c`（删除占位 `ConfigTab_Create`，改为包含 `config_tab.h`）

**Interfaces:**
- Consumes: `udp_manager.h`、`app.h`、`resource.h`。
- Produces: `config_tab.h` 声明 `HWND ConfigTab_Create(HWND hParent, HINSTANCE hInst);`（取代 main.c 内的占位实现）。

- [ ] **Step 1: 扩展 `include/resource.h`**

在 `IDC_STATUSBAR` 之后追加 tab1 控件 ID：

```c
/* ===== tab1 控件 ID (1xxx) ===== */
#define IDC_CFG_DISCOVER_BTN    1001
#define IDC_CFG_DEVLIST         1002   /* 下拉框 (CBS_DROPDOWNLIST) */
#define IDC_CFG_IP1             1010   /* 目标设备 IP 4 段 */
#define IDC_CFG_IP2             1011
#define IDC_CFG_IP3             1012
#define IDC_CFG_IP4             1013
#define IDC_CFG_GETVER          1014
#define IDC_CFG_REBOOT          1015
#define IDC_CFG_VERSION         1016   /* 静态文本, 显示版本 */
/* 网络参数 */
#define IDC_CFG_NIP1            1020   /* 新 IP 4 段 */
#define IDC_CFG_NIP2            1021
#define IDC_CFG_NIP3            1022
#define IDC_CFG_NIP4            1023
#define IDC_CFG_NIP_APPLY       1024
/* Modbus 参数 */
#define IDC_CFG_MB_SLAVE        1030
#define IDC_CFG_MB_BAUD         1031   /* 下拉 */
#define IDC_CFG_MB_APPLY        1032
#define IDC_CFG_MB_READ         1033
/* CAN 参数 */
#define IDC_CFG_CAN_ID          1040
#define IDC_CFG_CAN_BAUD        1041
#define IDC_CFG_CAN_APPLY       1042
#define IDC_CFG_CAN_READ        1043
/* 运维 */
#define IDC_CFG_FACTORY         1050
/* 日志 */
#define IDC_CFG_LOG             1060   /* 多行只读 EDIT */
```

- [ ] **Step 2: 写 `include/config_tab.h`**

文件 `include/config_tab.h`：

```c
#ifndef CONFIG_TAB_H
#define CONFIG_TAB_H

#include <windows.h>
#include "app.h"

/* 在主窗口 tab 控件内创建 tab1 子窗口. */
HWND ConfigTab_Create(HWND hParent, HINSTANCE hInst);

#endif /* CONFIG_TAB_H */
```

- [ ] **Step 3: 写 `src/config_tab.c`**

文件 `src/config_tab.c`。结构：用一个静态结构体持有所有控件 HWND + UdpManager 实例（tab 生命周期内复用）。所有控件在 `WM_CREATE` 用 `CreateWindowExW` 程序化创建（不用 dialog resource，与 handler-receiver 一致）。

```c
#include "config_tab.h"
#include "udp_manager.h"
#include "resource.h"
#include <stdio.h>

typedef struct {
	HWND hSelf;
	HWND hDevList, hIp[4], hNip[4], hVersion, hMbSlave, hMbBaud;
	HWND hCanId, hCanBaud, hLog;
	UdpManager *udp;
} ConfigTab;

static ConfigTab g_cfg;

/* 从 4 个 EDIT 控件读 IP, 写入 ip4[4]. 返回是否合法 (每段 0-255) */
static bool read_ip4(HWND ip_edits[4], uint8_t ip4[4])
{
	for (int i = 0; i < 4; i++) {
		wchar_t buf[8];
		GetWindowTextW(ip_edits[i], buf, 8);
		int v = _wtoi(buf);
		if (v < 0 || v > 255) return false;
		ip4[i] = (uint8_t)v;
	}
	return true;
}

/* 当前目标 IP 拼成点分十进制字符串. */
static void current_target_ip(char *out, int cap)
{
	wchar_t wbuf[4][8];
	for (int i = 0; i < 4; i++) GetWindowTextW(g_cfg.hIp[i], wbuf[i], 8);
	swprintf((wchar_t[64]){0}, 64, L"%ls.%ls.%ls.%ls", wbuf[0], wbuf[1], wbuf[2], wbuf[3]);
	/* 简化: 直接 sprintf char */
	sprintf(out, "%d.%d.%d.%d",
	        _wtoi(wbuf[0]), _wtoi(wbuf[1]), _wtoi(wbuf[2]), _wtoi(wbuf[3]));
}

/* 日志框追加一行 (带时间戳). */
static void log_append(const wchar_t *msg)
{
	SYSTEMTIME st;
	GetLocalTime(&st);
	wchar_t line[600];
	swprintf(line, 600, L"[%02d:%02d:%02d] %ls\r\n", st.wHour, st.wMinute, st.wSecond, msg);
	int len = GetWindowTextLengthW(g_cfg.hLog);
	SendMessageW(g_cfg.hLog, EM_SETSEL, len, len);
	SendMessageW(g_cfg.hLog, EM_REPLACESEL, 0, (LPARAM)line);
}
```

`WM_COMMAND` 处理各按钮（以"发现设备"/"应用 IP"/"读取 Modbus"为例，其余类同）：

```c
static void on_command(WPARAM wParam)
{
	switch (LOWORD(wParam)) {
	case IDC_CFG_DISCOVER_BTN: {
		char buf[2048];
		int cnt = 0;
		SendMessageW(g_cfg.hDevList, CB_RESETCONTENT, 0, 0);
		log_append(L"正在发现设备...");
		if (UdpManager_Discover(g_cfg.udp, buf, sizeof(buf), &cnt)) {
			/* 按 '\n' 拆分填下拉框 */
			char *p = strtok(buf, "\n");
			while (p) {
				wchar_t w[128];
				MultiByteToWideChar(CP_UTF8, 0, p, -1, w, 128);
				SendMessageW(g_cfg.hDevList, CB_ADDSTRING, 0, (LPARAM)w);
				p = strtok(NULL, "\n");
			}
			wchar_t m[64]; swprintf(m, 64, L"发现 %d 台设备", cnt);
			log_append(m);
		} else {
			log_append(L"未发现设备");
		}
		break;
	}
	case IDC_CFG_NIP_APPLY: {
		char ip[32]; current_target_ip(ip, sizeof(ip));
		uint8_t nip[4];
		if (!read_ip4(g_cfg.hNip, nip)) { MessageBoxW(g_cfg.hSelf, L"新 IP 非法", L"错误", MB_ICONERROR); break; }
		uint8_t ok = 0;
		if (UdpManager_SetIp(g_cfg.udp, ip, nip, &ok)) {
			if (ok) { log_append(L"SET_IP 成功, 设备将重启"); MessageBoxW(g_cfg.hSelf, L"IP 已设置, 设备将重启", L"成功", MB_ICONINFORMATION); }
			else    { log_append(L"SET_IP 被拒绝 (IP 末段 0/255 或首段 224-239)"); MessageBoxW(g_cfg.hSelf, L"设备拒绝该 IP", L"警告", MB_ICONWARNING); }
		} else {
			wchar_t m[128]; swprintf(m, 128, L"操作失败: %hs", UdpManager_GetLastError(g_cfg.udp));
			MessageBoxW(g_cfg.hSelf, m, L"错误", MB_ICONERROR); log_append(m);
		}
		break;
	}
	case IDC_CFG_MB_READ: {
		char ip[32]; current_target_ip(ip, sizeof(ip));
		uint8_t slave = 0; uint16_t baud = 0;
		if (UdpManager_GetModbus(g_cfg.udp, ip, &slave, &baud)) {
			wchar_t buf[16];
			swprintf(buf, 16, L"%u", slave); SetWindowTextW(g_cfg.hMbSlave, buf);
			swprintf(buf, 16, L"%u", baud);
			/* 在 baud 下拉框里选中匹配项, 或直接显示数值 */
			log_append(L"GET_MODBUS 成功");
		} else {
			wchar_t m[128]; swprintf(m, 128, L"GET_MODBUS 失败: %hs", UdpManager_GetLastError(g_cfg.udp));
			log_append(m);
		}
		break;
	}
	case IDC_CFG_FACTORY: {
		if (MessageBoxW(g_cfg.hSelf, L"确认出厂重置? 将擦除所有参数并重启", L"确认", MB_YESNO | MB_ICONWARNING) != IDYES) break;
		char ip[32]; current_target_ip(ip, sizeof(ip));
		uint8_t ok = 0;
		if (UdpManager_FactoryReset(g_cfg.udp, ip, &ok) && ok) log_append(L"出厂重置已执行");
		else log_append(L"出厂重置失败");
		break;
	}
	/* IDC_CFG_GETVER, IDC_CFG_REBOOT, IDC_CFG_MB_APPLY, IDC_CFG_CAN_APPLY, IDC_CFG_CAN_READ 类同 */
	}
}
```

窗口过程 + `WM_CREATE` 创建所有控件 + `WM_SIZE` 自适应布局。`WM_CREATE` 中 `g_cfg.udp = UdpManager_Create();`。`WM_DESTROY` 中 `UdpManager_Destroy(g_cfg.udp);`。

- [ ] **Step 4: 修改 `src/main.c`**

删除 main.c 内的占位 `ConfigTab_Create`（占位 STATIC 那段），在顶部 include 区加 `#include "config_tab.h"`。

- [ ] **Step 5: 修改 `CMakeLists.txt`**

```cmake
set(SOURCES
    src/main.c
    src/pcan_loader.c
    src/fw_image.c
    src/udp_manager.c
    src/can_manager.c
    src/modbus_client.c
    src/config_tab.c
)
```

- [ ] **Step 6: 构建验证**

Run:
```bash
cmake --build out --config Release
```
Expected: 编译通过。

- [ ] **Step 7: 运行验证（无设备）**

运行 exe，切到 tab1：所有控件布局正常（IP 输入框、下拉框、按钮、日志框）。点"发现设备"在无设备时应弹"未发现设备"日志（不崩溃）。点"应用 IP"在无设备时弹"操作失败: 设备无响应 (timeout)"。

- [ ] **Step 8: Commit**

```bash
git add include/resource.h include/config_tab.h src/config_tab.c src/main.c CMakeLists.txt
git commit -m "feat: implement tab1 UDP parameter configuration UI"
```

---

## Task 7: 实现 Tab2（UDP + CAN 固件升级）

**目标**：替换 `UpgradeTab_Create` 占位，实现通道选择、文件选择 + MCUboot 校验 + keyhash 提取、UDP 升级 worker 线程、CAN 升级 worker 线程、进度/取消。

**Files:**
- Modify: `include/resource.h`（加 tab2 控件 ID 2xxx）
- Create: `include/upgrade_tab.h`
- Create: `src/upgrade_tab.c`
- Modify: `CMakeLists.txt`（SOURCES 加 `src/upgrade_tab.c`）
- Modify: `src/main.c`（删占位，加 `#include "upgrade_tab.h"`）

**Interfaces:**
- Consumes: `udp_manager.h`、`can_manager.h`、`fw_image.h`、`pcan_loader.h`、`app.h`、`resource.h`。
- Produces: `upgrade_tab.h` 声明 `HWND UpgradeTab_Create(HWND, HINSTANCE);`。

- [ ] **Step 1: 扩展 `include/resource.h`**（tab2 ID 2xxx）

```c
/* ===== tab2 控件 ID (2xxx) ===== */
#define IDC_UPG_CHAN_UDP        2001   /* 单选 */
#define IDC_UPG_CHAN_CAN        2002
#define IDC_UPG_IP1             2010   /* UDP 目标 IP 4 段 */
#define IDC_UPG_IP2             2011
#define IDC_UPG_IP3             2012
#define IDC_UPG_IP4             2013
#define IDC_UPG_TEST            2014   /* 测试模式 复选框 */
#define IDC_UPG_CAN_DEV         2020   /* PCAN 设备下拉 */
#define IDC_UPG_CAN_BAUD        2021
#define IDC_UPG_FILE            2030
#define IDC_UPG_BROWSE          2031
#define IDC_UPG_FILEINFO        2032   /* 静态: magic/size/keyhash */
#define IDC_UPG_START           2040
#define IDC_UPG_CANCEL          2041
#define IDC_UPG_PROGRESS        2042   /* 进度条 */
#define IDC_UPG_STATUS          2043   /* 静态: 状态文字 */
#define IDC_UPG_LOG             2044   /* 多行日志 */
```

- [ ] **Step 2: 写 `include/upgrade_tab.h`**

```c
#ifndef UPGRADE_TAB_H
#define UPGRADE_TAB_H
#include <windows.h>
#include "app.h"
HWND UpgradeTab_Create(HWND hParent, HINSTANCE hInst);
#endif
```

- [ ] **Step 3: 写 `src/upgrade_tab.c`**

结构：tab 持有 `UdpManager*`、`CanManager*`、固件 buffer/size/keyhash、`volatile LONG g_cancel`、worker 线程 `HANDLE`。

**文件浏览 + 校验**（`IDC_UPG_BROWSE`）：

```c
static void on_browse(void)
{
	wchar_t path[MAX_PATH] = {0};
	OPENFILENAMEW ofn = { .lStructSize = sizeof(ofn), .hwndOwner = g_upg.hSelf,
	                      .lpstrFilter = L"MCUboot 镜像 (*.bin)\0*.bin\0所有文件\0*.*\0",
	                      .lpstrFile = path, .nMaxFile = MAX_PATH,
	                      .Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST };
	if (!GetOpenFileNameW(&ofn)) return;
	SetWindowTextW(g_upg.hFile, path);

	/* 读文件到堆 */
	HANDLE hf = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (hf == INVALID_HANDLE_VALUE) { set_fileinfo(L"打开失败"); return; }
	DWORD size = GetFileSize(hf, NULL);
	uint8_t *buf = (uint8_t *)malloc(size);
	DWORD rd; ReadFile(hf, buf, size, &rd, NULL); CloseHandle(hf);

	if (!fw_image_validate_header(buf, size)) {
		set_fileinfo_color(L"非 MCUboot 镜像 (magic 不匹配)", RGB(255, 0, 0));
		free(buf); buf = NULL; size = 0;
		EnableWindow(g_upg.hStart, FALSE);
		return;
	}
	uint8_t kh[32];
	bool has_kh = fw_image_extract_keyhash(buf, size, kh);
	g_upg.img = buf; g_upg.img_size = size;
	if (has_kh) { memcpy(g_upg.keyhash, kh, 32); g_upg.has_keyhash = true; }
	else        { g_upg.has_keyhash = false; }

	wchar_t info[200];
	swprintf(info, 200, L"size=%u 字节  keyhash=%ls",
	         size, has_kh ? L"已提取" : L"缺失(将跳过校验)");
	set_fileinfo(info);
	EnableWindow(g_upg.hStart, TRUE);
}
```

**UDP 升级 worker 线程**：

```c
static DWORD WINAPI udp_upgrade_thread(LPVOID arg)
{
	HWND hSelf = g_upg.hSelf;
	post_log(hSelf, L"UDP 升级开始");
	uint8_t st = 0;
	uint32_t sz = g_upg.img_size;
	const uint8_t *kh = g_upg.has_keyhash ? g_upg.keyhash : NULL;
	char ip[32]; current_upg_target_ip(ip, sizeof(ip));

	if (!UdpManager_FwStart(g_upg.udp, ip, sz, kh, &st)) {
		post_log(hSelf, L"FW_START 无响应");
		PostMessage(hSelf, WM_APP_UPG_DONE, 0, 0); return 0;
	}
	if (st == 2) {
		post_log(hSelf, L"keyhash 不匹配, 设备拒绝");
		PostMessage(hSelf, WM_APP_UPG_DONE, 0, 0); return 0;
	}
	if (st != 1) {
		post_log(hSelf, L"FW_START 失败");
		PostMessage(hSelf, WM_APP_UPG_DONE, 0, 0); return 0;
	}

	uint32_t off = 0;
	const int CHUNK = 512; /* <=511 即可 */
	while (off < sz) {
		if (InterlockedCompareExchange(&g_upg.cancel, 0, 0)) {
			post_log(hSelf, L"用户取消");
			PostMessage(hSelf, WM_APP_UPG_DONE, 0, 0); return 0;
		}
		int n = sz - off; if (n > CHUNK) n = CHUNK;
		uint32_t roff = 0;
		if (!UdpManager_FwData(g_upg.udp, ip, g_upg.img + off, n, &roff)) {
			post_log(hSelf, L"FW_DATA 失败"); PostMessage(hSelf, WM_APP_UPG_DONE, 0, 0); return 0;
		}
		off += n;
		PostMessage(hSelf, WM_APP_UPG_PROGRESS, (WPARAM)(off * 90 / sz), 1);
	}

	uint16_t crc = UdpManager_CRC16_CCITT(g_upg.img, sz);
	uint8_t test = (SendMessageW(g_upg.hTest, BM_GETCHECK, 0, 0) == BST_CHECKED) ? 1 : 0;
	uint8_t result = 0;
	if (!UdpManager_FwEnd(g_upg.udp, ip, test, crc, &result) || result != 1) {
		post_log(hSelf, L"FW_END 失败 (CRC 校验?)");
		PostMessage(hSelf, WM_APP_UPG_DONE, 0, 0); return 0;
	}
	PostMessage(hSelf, WM_APP_UPG_PROGRESS, 100, 2);
	post_log(hSelf, L"升级完成, 设备将重启进行 MCUboot 交换");
	PostMessage(hSelf, WM_APP_UPG_DONE, 1, 0);
	return 0;
}
```

CAN 升级 worker 类似，调 `CanManager_FirmwareUpgrade(mgr, img, size, kh, permanent, progress_cb, user)`，progress_cb 内 `PostMessage(WM_APP_UPG_PROGRESS, percent, ...)`。

**UI 接收自定义消息**（窗口过程 `WM_APP_UPG_PROGRESS`/`WM_APP_UPG_LOG`/`WM_APP_UPG_DONE`）：

```c
case WM_APP_UPG_PROGRESS:
	SendMessageW(g_upg.hProgress, PBM_SETPOS, wParam, 0);
	{ const wchar_t *stage[] = { L"", L"发送数据 %d%%", L"等待设备重启" };
	  wchar_t s[64]; swprintf(s, 64, stage[lParam], (int)wParam); SetWindowTextW(g_upg.hStatus, s); }
	return 0;
case WM_APP_UPG_LOG: {
	wchar_t line[600];
	SYSTEMTIME st; GetLocalTime(&st);
	swprintf(line, 600, L"[%02d:%02d:%02d] %ls\r\n", st.wHour, st.wMinute, st.wSecond, (const wchar_t*)lParam);
	int len = GetWindowTextLengthW(g_upg.hLog);
	SendMessageW(g_upg.hLog, EM_SETSEL, len, len);
	SendMessageW(g_upg.hLog, EM_REPLACESEL, 0, (LPARAM)line);
	free((void*)lParam);
	return 0; }
case WM_APP_UPG_DONE:
	EnableWindow(g_upg.hStart, TRUE);
	EnableWindow(g_upg.hCancel, FALSE);
	MessageBoxW(g_hMain, wParam ? L"升级成功" : L"升级失败", L"结果", wParam ? MB_ICONINFORMATION : MB_ICONERROR);
	return 0;
```

`post_log` helper：`PostMessageW(hSelf, WM_APP_UPG_LOG, 0, (LPARAM)_wcsdup(msg));`（UI 收到后 free）。

**"开始升级"按钮**：检查通道单选 + 文件已校验，禁用 start / 启用 cancel，`InterlockedExchange(&cancel, 0)`，`CreateThread` 起 worker。

**"取消"按钮**：`InterlockedExchange(&g_upg.cancel, 1);`

- [ ] **Step 4: 修改 `src/main.c`**（删占位，include `upgrade_tab.h`）

- [ ] **Step 5: 修改 `CMakeLists.txt`**（SOURCES 加 `src/upgrade_tab.c`）

- [ ] **Step 6: 构建验证**

Run: `cmake --build out --config Release`
Expected: 编译通过。

- [ ] **Step 7: 运行验证（无设备）**

tab2：选一个非 .bin 文件 → fileinfo 显示红字"非 MCUboot 镜像"，"开始升级"禁用。选一个合法 MCUboot .bin（可从 `~/code/app/apps` 固件构建产物拿）→ fileinfo 显示 size + keyhash 状态，"开始升级"启用。无设备时点开始 → 日志"FW_START 无响应" → 弹"升级失败"。

- [ ] **Step 8: Commit**

```bash
git add include/resource.h include/upgrade_tab.h src/upgrade_tab.c src/main.c CMakeLists.txt
git commit -m "feat: implement tab2 firmware upgrade (UDP + CAN, worker thread)"
```

---

## Task 8: 实现 Tab3（Modbus 调试）

**目标**：替换 `ModbusTab_Create` 占位，实现 TCP/RTU 单连接切换 + DI/DO/AI 面板 + 18 holding + 6 input 寄存器表 + 自动刷新。

**Files:**
- Modify: `include/resource.h`（tab3 ID 3xxx）
- Create: `include/modbus_tab.h`
- Create: `src/modbus_tab.c`
- Modify: `CMakeLists.txt`（SOURCES 加 `src/modbus_tab.c`）
- Modify: `src/main.c`（删占位，include `modbus_tab.h`）

**Interfaces:**
- Consumes: `modbus_client.h`、`app.h`、`resource.h`。
- Produces: `modbus_tab.h` 声明 `HWND ModbusTab_Create(HWND, HINSTANCE);`。

**寄存器元数据表**（spec §7）写死在 `modbus_tab.c`：

```c
typedef struct {
	uint16_t addr;       /* 0-based offset */
	const wchar_t *name;
	bool is_input;       /* true=input (FC04), false=holding (FC03/06) */
	enum { RW_RW, RW_RO, RW_WO, RW_WO_TRIG } rw;  /* RO=只读, WO=只写触发, WO_TRIG=低字触发 */
} RegMeta;

static const RegMeta g_regs[] = {
	/* holding */
	{0x00, L"DO输出控制",  false, RW_RW},
	{0x01, L"DI使能位图",  false, RW_RW},
	{0x02, L"AI使能位图",  false, RW_RW},
	{0x03, L"DI采样间隔ms", false, RW_RW},
	{0x04, L"AI采样间隔ms", false, RW_RW},
	{0x05, L"历史保存开关", false, RW_RW},
	{0x06, L"CAN业务帧ID", false, RW_RW},
	{0x07, L"CAN波特率(k)", false, RW_RW},
	{0x08, L"RS485波特率", false, RW_RW},
	{0x09, L"Modbus从机ID", false, RW_RW},
	{0x0A, L"IP第1字节",  false, RW_RW},
	{0x0B, L"IP第2字节",  false, RW_RW},
	{0x0C, L"IP第3字节",  false, RW_RW},
	{0x0D, L"IP第4字节",  false, RW_RW},
	{0x0E, L"时间戳高字", false, RW_WO_TRIG},
	{0x0F, L"时间戳低字", false, RW_WO_TRIG},
	{0x10, L"参数保存触发", false, RW_WO},
	{0x11, L"重启触发",   false, RW_WO},
	/* input */
	{0x00, L"固件版本",   true,  RW_RO},
	{0x01, L"AI1电流",   true,  RW_RO},
	{0x02, L"AI2电流",   true,  RW_RO},
	{0x03, L"AI3电压",   true,  RW_RO},
	{0x04, L"AI4电压",   true,  RW_RO},
	{0x05, L"DI位图",    true,  RW_RO},
};
#define REG_COUNT (sizeof(g_regs)/sizeof(g_regs[0]))
```

ListView 列：地址 / 名称 / 当前值 / R/W / [查询] / [设置]。ListView 自绘按钮较复杂，**简化方案**：双击行弹"查询/设置"小菜单，或行尾放两个 owner-draw 按钮。**实现选简化方案**：双击行 → 若 RO 弹"只读"提示；若 RW 弹输入框 → 写入；同时单选行 + 顶部"查询选中"按钮触发单寄存器读。

- [ ] **Step 1: 扩展 `include/resource.h`**（tab3 ID 3xxx）

```c
/* ===== tab3 控件 ID (3xxx) ===== */
#define IDC_MB_CHAN_TCP         3001
#define IDC_MB_CHAN_RTU         3002
#define IDC_MB_IP1              3010
#define IDC_MB_IP2              3011
#define IDC_MB_IP3              3012
#define IDC_MB_IP4              3013
#define IDC_MB_PORT             3014
#define IDC_MB_COM              3020   /* 串口下拉 */
#define IDC_MB_BAUD             3021
#define IDC_MB_UID              3022
#define IDC_MB_CONNECT          3030
#define IDC_MB_DISCONNECT       3031
#define IDC_MB_STATUS           3032
#define IDC_MB_REFRESH_ALL      3040
#define IDC_MB_AUTOREF          3041
#define IDC_MB_AUTOREF_INT      3042
#define IDC_MB_REG_LIST         3050   /* ListView */
#define IDC_MB_REG_QUERY        3051
#define IDC_MB_LOG              3060
/* DI/DO/AI 用动态创建的子控件 (16+8+4 个), ID 自 3100 起 */
#define IDC_MB_DI_BASE          3100   /* DI1..DI16 = 3100..3115 */
#define IDC_MB_DO_BASE          3120   /* DO1..DO8 = 3120..3127 */
#define IDC_MB_AI_BASE          3130   /* AI1..AI4 = 3130..3133 */
#define IDC_MB_TIMER            1      /* SetTimer id */
```

- [ ] **Step 2: 写 `include/modbus_tab.h`**

```c
#ifndef MODBUS_TAB_H
#define MODBUS_TAB_H
#include <windows.h>
#include "app.h"
HWND ModbusTab_Create(HWND hParent, HINSTANCE hInst);
#endif
```

- [ ] **Step 3: 写 `src/modbus_tab.c`**

要点：

- **连接**：根据通道单选，TCP 用 IP+port+uid 调 `MbClient_ConnectTcp`；RTU 用 COM+baud+uid 调 `MbClient_ConnectRtu`。连接成功后启用刷新按钮，状态灯绿。
- **DI 面板**：`MbClient_ReadDiscreteInputs(0, 16, bits)` → 16 个圆形 STATIC 控件按 bit 切换颜色（用 `WM_CTLCOLORSTATIC` 处理着色）。
- **DO 面板**：8 个 BUTTON 控件，点击 → `MbClient_WriteSingleCoil(addr, on)` → 立即 `MbClient_ReadCoils(0,8,bits)` 回显状态。
- **AI 面板**：`MbClient_ReadInput(1, 4, regs)`，AI1/2 显示 `regs[i]/100.0 mA`，AI3/4 显示 `regs[i]/100.0 V`（4 个 STATIC 文本）。
- **寄存器表**：ListView 初始化时插入 24 行（18 holding + 6 input），地址列显示 40001+/30001+，名称列用 `g_regs[].name`，R/W 列用 RW/RO/WO 字样，"设置"按钮按 rw 禁用/启用。
- **查询选中行**：取 ListView 选中行 → 按 `is_input` 调 `ReadInput`/`ReadHolding` 单个 → 刷新该行"当前值"列。
- **双击行**：若 RO/WO 弹提示；若 RW 弹 `InputBox`（小对话框模板或自绘）→ `WriteSingleReg` → 刷新。
- **刷新全部**：依次 `ReadCoils(0,8)` / `ReadDiscreteInputs(0,16)` / `ReadInput(1,4)` + 逐行读 holding/input 刷新表。整个序列在 UI 线程做（每条 <100ms，24 条 <2.4s，可接受；如卡顿后续移 worker）。
- **自动刷新**：勾选 `IDC_MB_AUTOREF` → `SetTimer(hSelf, IDC_MB_TIMER, interval_ms, NULL)`；`WM_TIMER` 只刷 DI/DO/AI 面板（不动 holding 表）；取消勾选 `KillTimer`。

布局：用 groupbox 分隔 DI/DO/AI/寄存器表四块，程序化创建控件 + `WM_SIZE` 自适应。

- [ ] **Step 4: 修改 `src/main.c`**（删占位，include `modbus_tab.h`）

- [ ] **Step 5: 修改 `CMakeLists.txt`**（SOURCES 加 `src/modbus_tab.c`）

- [ ] **Step 6: 构建验证**

Run: `cmake --build out --config Release`
Expected: 编译通过。

- [ ] **Step 7: 运行验证（无设备）**

tab3：所有面板布局正常。点"连接"在无设备/无串口时弹"连接失败"日志。切换 TCP/RTU 单选显隐对应输入区。勾选自动刷新 + 设间隔正常。

- [ ] **Step 8: Commit**

```bash
git add include/resource.h include/modbus_tab.h src/modbus_tab.c src/main.c CMakeLists.txt
git commit -m "feat: implement tab3 modbus debugging (TCP/RTU, DI/DO/AI, register table)"
```

---

## Task 9: README、build.bat 与收尾

**目标**：写 README + 一键构建脚本，最终整体构建验证。

**Files:**
- Create: `README.md`
- Create: `tools/build.bat`

- [ ] **Step 1: 写 `README.md`**

文件 `README.md`（含项目简介、3 tab 功能、构建步骤两个 preset、协议参考路径）：

```markdown
# io-edge-hub 上位机

io-edge-hub 工业边缘节点的 Windows 调试工具。原生 Win32 C GUI, 3 个 tab:

- **Tab1 UDP 参数配置**: 发现设备, 配置网络/Modbus(RS485)/CAN 参数, 查询版本, 重启, 出厂重置.
- **Tab2 固件升级**: 通过 UDP 或 PCAN-USB(CAN) 升级 MCUboot 签名镜像.
- **Tab3 Modbus 调试**: 以 Modbus 主机 (TCP/RTU) 操作每个寄存器, 每路 DI/DO/AI 单独查看与控制.

## 构建

需要 CMake ≥ 3.25. 两个工具链可选:

### MSVC (Windows 原生)
```
cmake --preset vs
cmake --build out --config Release
```
产物: `out/bin/io-edge-hub.exe`

### MinGW (Linux 交叉编译)
```
cmake --preset mingw
cmake --build build --config Release
```
产物: `build/bin/io-edge-hub.exe`

或用 `tools/build.bat` 一键 MSVC 构建.

## 协议参考
固件权威源: `app/apps/applications/io-edge-hub/` 与 `app/apps/libs/udp_fw_upgrade|can_fw_upgrade`.
设计文档: `docs/superpowers/specs/2026-08-11-host-tool-design.md`.

## CAN 升级依赖
需安装 PCAN-Basic 驱动 (PCANBasic.dll 运行时动态加载).
```

- [ ] **Step 2: 写 `tools/build.bat`**（仿 handler-receiver，定位 VS + vcvars64 + cmake）

```bat
@echo off
setlocal
rem 定位 Visual Studio
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.Component.MSBuild -property installationPath`) do set VS=%%i
if not defined VS (echo 未找到 Visual Studio & exit /b 1)
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" || (echo vcvars64 失败 & exit /b 2)
cmake --preset vs || (echo configure 失败 & exit /b 3)
cmake --build out --config Release || (echo build 失败 & exit /b 4)
echo 构建成功: out\bin\io-edge-hub.exe
```

- [ ] **Step 3: 整体构建验证**

Run:
```bash
cmake --build out --config Release
```
Expected: 全部 9 个源文件 + resource.rc 编译通过，生成 `io-edge-hub.exe`。

- [ ] **Step 4: 运行整体冒烟（无设备）**

运行 exe：3 tab 都可切换，无崩溃，所有占位已替换为真实 UI。tab1 发现/配置、tab2 选文件、tab3 连接 在无设备时都能给出合理错误提示。

- [ ] **Step 5: Commit**

```bash
git add README.md tools/build.bat
git commit -m "docs: add README and one-shot MSVC build script"
```

- [ ] **Step 6: 最终 git log 确认**

```bash
git log --oneline
```
Expected: 看到 9 个 commit，对应 9 个 task。

---

## Self-Review

**1. Spec coverage:**

| spec § | 覆盖 task |
|---|---|
| §3 项目骨架/构建 | Task 1 |
| §4 代码风格 | Global Constraints + 每个 task |
| §5.1 udp_manager | Task 3 |
| §5.2 can_manager | Task 4 |
| §5.3 fw_image | Task 2 |
| §5.4 pcan_loader | Task 2 |
| §5.5 modbus_client | Task 5 |
| §6.1 tab1 | Task 6 |
| §6.2 tab2 | Task 7 |
| §6.3 tab3 | Task 8 |
| §7 寄存器元数据表 | Task 8 |
| §8 错误处理 | 各 task 的 MessageBox + last_error |
| §9 并发模型 | Task 7 (worker + WM_APP_*) |
| §10 编码 | Global Constraints (MinGW/MSVC flags) |
| §11 实现顺序 | 9 个 task 顺序对齐 spec §11 的 7 步（细分）|
| §12 YAGNI | Global Constraints 不做项 |

无 spec 遗漏。

**2. Placeholder scan:**

- Task 5 `mb_transact` RTU 分支有"实现略"注释——这是 plan 失败标记。**已在 step 说明里明确要求补全每个 fc 的响应读取，不允许留占位 return 0**，但 plan 本身没有给出全部 RTU 响应代码。**修正**：RTU 实现复杂度与 TCP 相当，应在 plan 内给完整代码而非"略"。我会在下方补一个明确的子说明。

- Task 4 `can_read_resp` 签名笔误已注明修正。

- Task 6 / 7 / 8 有"类同"/"其余类同"——这是合理的（避免重复同模式代码），但每个 task 都给了至少一个完整例子的代码。可接受。

**3. Type consistency:**

- `UdpManager_FwStart` 等签名在 Task 3 定义、Task 7 使用——一致。
- `CanManager_FirmwareUpgrade` 签名 Task 4 定义、Task 7 使用——一致。
- `MbClient_*` Task 5 定义、Task 8 使用——一致。
- tab 工厂签名 `HWND XxxTab_Create(HWND, HINSTANCE)` 在 app.h 声明、main.c 调用、各 tab.c 实现——一致。

---

## 补充：Task 5 RTU mb_transact 完整要求

Task 5 `mb_transact` 的 RTU 分支必须完整实现以下逻辑（不留占位）：

1. 构造 ADU = `[unit_id][pdu...][crc16 LE]`，`WriteFile` 发出。
2. `Sleep(rtu_char_time_ms)` 等 3.5 字符间隔（按 baud 简化：9600bps≈4ms，115200bps≈1ms，用 `max(1, 35000/baud)` 近似）。
3. 按响应 fc 分支读：
   - **异常响应**（`fc & 0x80`）：再读 2 字节（exception code 1B + crc 2B，实际读 3 字节），校验 crc，填 `last_error` 返回 0。
   - **FC01/02/03/04 正常**：读 1 字节 byte count `bc`，再读 `bc + 2` 字节（data + crc），校验 crc，写入 out_pdu `[fc][bc][data...]`，返回 `2 + bc`。
   - **FC05/06/16 正常**：响应固定 8 字节（addr+fc 已读 2，剩 6 含 crc），直接读 6 字节，校验 crc，写入 out_pdu `[fc][...]`（共 7 字节 pdu），返回 7。
4. 每次读用带 `COMMTIMEOUTS` 超时的 `ReadFile`（已在 Connect 时配 1000ms 总超时）。
5. CRC 校验失败 → `last_error="RTU CRC 错误"`，返回 0。

实现者按上述逻辑写完整 C 代码，不允许 `return 0; // 占位`。
