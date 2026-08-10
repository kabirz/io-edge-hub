/* io-edge-hub 上位机 - Tab2 "固件升级"
 *
 * 程序化创建全部控件, 单一 g_upg 静态结构持有所有控件 HWND + UdpManager + CanManager
 * 实例 + 当前固件 image 缓冲. WM_CREATE 创建控件 + 两个 manager; WM_DESTROY 销毁.
 *
 * 通道: 单选 UDP / CAN. 切换时 ShowWindow 显示对应子区域 (IP / PCAN 设备行).
 *
 * 流程:
 *   1. 浏览 .bin → CreateFileW/ReadFile 全量入堆 → fw_image_validate_header 拒非 MCUboot;
 *      fw_image_extract_keyhash 取 keyhash (缺失则跳过校验, 兼容旧固件).
 *   2. 开始升级 → 校验输入 → 禁用开始/启用取消 → CreateThread 起 UDP 或 CAN worker.
 *   3. worker 全程只通过 PostMessage (WM_APP_UPG_PROGRESS/LOG/DONE) 与 UI 通信.
 *   4. UI 收 DONE → 恢复按钮 + MessageBox 结果.
 *
 * 布局 (主窗口 tab 显示区约 712x504):
 *   - 通道 groupbox: 通道单选 + (UDP: 目标IP+测试 / CAN: PCAN+波特率+连接)
 *   - 固件文件 groupbox: 路径 + 浏览 + fileinfo
 *   - 升级控制 groupbox: 开始 + 取消 + 进度条 + 状态文字
 *   - 操作日志 groupbox: 多行只读 EDIT (带时间戳)
 */
#include "udp_manager.h"   /* 须先于 windows.h 拉 winsock2.h (避免 winsock1 冲突) */
#include "can_manager.h"
#include "pcan_loader.h"   /* PCAN_BAUD_* 波特率 BTR 寄存器值 */
#include "fw_image.h"
#include "upgrade_tab.h"
#include "resource.h"
#include <commctrl.h>
#include <commdlg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <wchar.h>

/* ===== 静态状态: 所有控件 HWND + manager + image 缓冲 + 线程状态 ===== */
typedef struct {
	HWND hSelf;
	/* 通道单选 */
	HWND hChanUdp, hChanCan;
	/* UDP 行: 目标 IP 4 段 + 测试复选框 + 标签 */
	HWND hUdpLbl, hIp[4], hTest;
	/* CAN 行: 设备下拉 + 波特率下拉 + 刷新按钮 + 连接按钮 */
	HWND hCanLbl1, hCanDev, hCanLbl2, hCanBaud, hCanRefresh, hCanConn;
	/* 版本信息行: label + 查询按钮 */
	HWND hVerLbl, hVersion, hGetVer;
	/* 文件 */
	HWND hFile, hBrowse, hFileInfo;
	/* 升级控制 */
	HWND hStart, hReboot, hProgress, hStatus, hLog;
	/* manager */
	UdpManager *udp;
	CanManager *can;
	bool can_connected;
	int  can_channel;
	/* image 缓冲 (浏览时加载, 升级时消费, 切换文件/退出时释放) */
	uint8_t *img;
	uint32_t img_size;
	uint8_t  keyhash[32];
	bool has_keyhash;
	/* worker 输入 (UI 线程在 CreateThread 前缓存, worker 只读) */
	char  cur_ip[32];
	bool  cur_test;
	bool  cur_permanent;
	/* 取消标志 + 线程句柄 */
	volatile LONG cancel;
	HANDLE thread;
} UpgradeTab;

static UpgradeTab g_upg;
static HINSTANCE g_hInst = NULL;
static HFONT g_hFont = NULL;
static const wchar_t *UPGRADE_TAB_CLASS = L"ioEdgeHubUpgradeTabCls";
static BOOL g_classRegistered = FALSE;

/* PCAN 波特率 (与 pcan_loader.h BTR 寄存器值对应, can_manager 直传) */
static const struct { const wchar_t *label; uint32_t btr; } g_bauds[] = {
	{ L"250k (默认)", PCAN_BAUD_250K },
	{ L"500k",        PCAN_BAUD_500K },
	{ L"1M",          PCAN_BAUD_1M   },
	{ L"125k",        PCAN_BAUD_125K },
	{ L"100k",        PCAN_BAUD_100K },
	{ L"50k",         PCAN_BAUD_50K  },
};
#define BAUD_COUNT (int)(sizeof(g_bauds) / sizeof(g_bauds[0]))

/* ===== 控件创建辅助 ===== */

static HWND create_label(const wchar_t *text, int x, int y, int w, int h)
{
	HWND hw = CreateWindowExW(0, L"STATIC", text,
		WS_CHILD | WS_VISIBLE, x, y, w, h,
		g_upg.hSelf, NULL, g_hInst, NULL);
	SendMessageW(hw, WM_SETFONT, (WPARAM)g_hFont, TRUE);
	return hw;
}

static HWND create_edit(int x, int y, int w, int h, int id, DWORD extra)
{
	HWND hw = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
		WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | extra,
		x, y, w, h, g_upg.hSelf, (HMENU)(INT_PTR)id, g_hInst, NULL);
	SendMessageW(hw, WM_SETFONT, (WPARAM)g_hFont, TRUE);
	return hw;
}

static HWND create_button(const wchar_t *text, int x, int y, int w, int h, int id)
{
	HWND hw = CreateWindowExW(0, L"BUTTON", text,
		WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
		x, y, w, h, g_upg.hSelf, (HMENU)(INT_PTR)id, g_hInst, NULL);
	SendMessageW(hw, WM_SETFONT, (WPARAM)g_hFont, TRUE);
	return hw;
}

static HWND create_groupbox(const wchar_t *text, int x, int y, int w, int h)
{
	HWND hw = CreateWindowExW(0, L"BUTTON", text,
		WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
		x, y, w, h, g_upg.hSelf, NULL, g_hInst, NULL);
	SendMessageW(hw, WM_SETFONT, (WPARAM)g_hFont, TRUE);
	return hw;
}

/* 创建 4 段 IP 编辑框 (ES_NUMBER + 限 3 字符). 返回末尾 x 坐标. */
static int create_ip_row(int x, int y, int ids[4], HWND out_hwnd[4])
{
	int seg_w = 34, dot_w = 6, gap = 2;
	for (int i = 0; i < 4; i++) {
		out_hwnd[i] = create_edit(x, y, seg_w, 22, ids[i], ES_NUMBER);
		SendMessageW(out_hwnd[i], EM_SETLIMITTEXT, 3, 0);
		x += seg_w;
		if (i < 3) {
			create_label(L".", x, y + 3, dot_w, 16);
			x += dot_w + gap;
		}
	}
	return x;
}

/* ===== 业务辅助 ===== */

/* 当前选中的通道: 0=UDP, 1=CAN */
static int current_channel(void)
{
	return (SendMessageW(g_upg.hChanCan, BM_GETCHECK, 0, 0) == BST_CHECKED) ? 1 : 0;
}

/* 切换通道: 显示/隐藏 UDP 与 CAN 子区域. */
static void apply_channel_visibility(void)
{
	int can = current_channel();
	/* UDP 行 */
	ShowWindow(g_upg.hUdpLbl, can ? SW_HIDE : SW_SHOW);
	for (int i = 0; i < 4; i++) ShowWindow(g_upg.hIp[i], can ? SW_HIDE : SW_SHOW);
	ShowWindow(g_upg.hTest, can ? SW_HIDE : SW_SHOW);
	/* CAN 行 */
	ShowWindow(g_upg.hCanLbl1,   can ? SW_SHOW : SW_HIDE);
	ShowWindow(g_upg.hCanDev,    can ? SW_SHOW : SW_HIDE);
	ShowWindow(g_upg.hCanLbl2,   can ? SW_SHOW : SW_HIDE);
	ShowWindow(g_upg.hCanBaud,   can ? SW_SHOW : SW_HIDE);
	ShowWindow(g_upg.hCanRefresh, can ? SW_SHOW : SW_HIDE);
	ShowWindow(g_upg.hCanConn,   can ? SW_SHOW : SW_HIDE);
}

/* UI 线程: 设置 fileinfo 静态文本 (默认黑色). */
static void set_fileinfo(const wchar_t *msg)
{
	SetWindowTextW(g_upg.hFileInfo, msg);
}

/* 从 4 个 IP EDIT 拼点分十进制窄串到 out. 段非数字按 0 处理. */
static void read_ip_to_str(HWND ip_edits[4], char *out, int cap)
{
	int v[4];
	for (int i = 0; i < 4; i++) {
		wchar_t buf[8];
		GetWindowTextW(ip_edits[i], buf, 8);
		v[i] = _wtoi(buf);
		if (v[i] < 0 || v[i] > 255) v[i] = 0;
	}
	snprintf(out, cap, "%d.%d.%d.%d", v[0], v[1], v[2], v[3]);
}

/* 日志框追加一行 (UI 线程: WM_APP_UPG_LOG 处理时调用). msg 已是用户字符串. */
static void log_append_ptr(const wchar_t *msg)
{
	SYSTEMTIME st;
	GetLocalTime(&st);
	wchar_t line[600];
	swprintf(line, 600, L"[%02d:%02d:%02d] %ls\r\n",
	         st.wHour, st.wMinute, st.wSecond, msg);
	int len = GetWindowTextLengthW(g_upg.hLog);
	SendMessageW(g_upg.hLog, EM_SETSEL, len, len);
	SendMessageW(g_upg.hLog, EM_REPLACESEL, 0, (LPARAM)line);
}

/* worker → UI: 投递一条日志 (堆字符串, UI free). */
static void post_log(const wchar_t *msg)
{
	wchar_t *dup = _wcsdup(msg);
	if (dup) {
		PostMessageW(g_upg.hSelf, WM_APP_UPG_LOG, 0, (LPARAM)dup);
	}
}

/* worker → UI: 投递完成. success=1 成功 / 0 失败. */
static void post_done(int success)
{
	PostMessageW(g_upg.hSelf, WM_APP_UPG_DONE, (WPARAM)success, 0);
}

/* worker → UI: 投递进度 (percent 0-100, stage 0=未用/1=发送数据/2=等待重启). */
static void post_progress(int percent, int stage)
{
	PostMessageW(g_upg.hSelf, WM_APP_UPG_PROGRESS, (WPARAM)percent, (LPARAM)stage);
}

/* 释放已加载 image 缓冲 (浏览新文件 / 退出时). */
static void free_image(void)
{
	if (g_upg.img) {
		free(g_upg.img);
		g_upg.img = NULL;
	}
	g_upg.img_size = 0;
	g_upg.has_keyhash = false;
}

/* ===== 浏览 + MCUboot 校验 ===== */

static void on_browse(void)
{
	wchar_t path[MAX_PATH] = {0};
	OPENFILENAMEW ofn;
	memset(&ofn, 0, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = g_upg.hSelf;
	ofn.lpstrFilter = L"固件镜像 (*.bin)\0*.bin\0所有文件\0*.*\0";
	ofn.lpstrFile = path;
	ofn.nMaxFile = MAX_PATH;
	ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
	if (!GetOpenFileNameW(&ofn)) return;
	SetWindowTextW(g_upg.hFile, path);

	/* 释放旧缓冲 */
	free_image();

	/* 全量读入堆 */
	HANDLE hf = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
	                        OPEN_EXISTING, 0, NULL);
	if (hf == INVALID_HANDLE_VALUE) {
		set_fileinfo(L"打开文件失败");
		EnableWindow(g_upg.hStart, FALSE);
		return;
	}
	DWORD size = GetFileSize(hf, NULL);
	if (size == INVALID_FILE_SIZE || size == 0) {
		CloseHandle(hf);
		set_fileinfo(L"文件为空或读取大小失败");
		EnableWindow(g_upg.hStart, FALSE);
		return;
	}
	uint8_t *buf = (uint8_t *)malloc(size);
	if (!buf) {
		CloseHandle(hf);
		set_fileinfo(L"内存不足");
		EnableWindow(g_upg.hStart, FALSE);
		return;
	}
	DWORD rd = 0;
	BOOL ok = ReadFile(hf, buf, size, &rd, NULL);
	CloseHandle(hf);
	if (!ok || rd != size) {
		free(buf);
		set_fileinfo(L"读取文件不完整");
		EnableWindow(g_upg.hStart, FALSE);
		return;
	}

	/* MCUboot header 校验: magic + 头长度 + 镜像长度 + TLV info magic */
	if (!fw_image_validate_header(buf, size)) {
		set_fileinfo(L"非固件镜像 (magic 不匹配)");
		free(buf);
		EnableWindow(g_upg.hStart, FALSE);
		return;
	}

	/* keyhash 提取 (可选, 缺失则跳过校验, 兼容无签名旧固件) */
	uint8_t kh[32];
	bool has_kh = fw_image_extract_keyhash(buf, size, kh);
	g_upg.img = buf;
	g_upg.img_size = size;
	if (has_kh) {
		memcpy(g_upg.keyhash, kh, 32);
		g_upg.has_keyhash = true;
	} else {
		g_upg.has_keyhash = false;
	}

	wchar_t info[200];
	swprintf(info, 200, L"size=%u 字节  keyhash=%ls",
	         (unsigned)size,
	         has_kh ? L"已提取" : L"缺失(将跳过校验)");
	set_fileinfo(info);
	EnableWindow(g_upg.hStart, TRUE);
}

/* ===== 版本查询 (UI 线程) ===== */

/* 按当前通道查询设备版本并刷新版本 label.
 * UDP: GET_VERSION (0x04) 到目标 IP; CAN: CanManager_GetVersion (0x101 cmd=2).
 * 失败时 label 显示 "(查询失败)" 并把详细错误写入日志框. */
static void on_query_version(void)
{
	char ver[80] = {0};
	bool ok = false;
	int can = current_channel();

	if (!can) {
		/* UDP: 取目标 IP */
		char ip[64] = {0};
		read_ip_to_str(g_upg.hIp, ip, sizeof(ip));
		/* 空段当 0, 这里要求 4 段都填 */
		bool ip_empty = false;
		for (int i = 0; i < 4; i++) {
			wchar_t buf[8];
			if (GetWindowTextW(g_upg.hIp[i], buf, 8) == 0) { ip_empty = true; break; }
		}
		if (ip_empty) {
			SetWindowTextW(g_upg.hVersion, L"(请先填目标 IP)");
			return;
		}
		ok = UdpManager_GetVersion(g_upg.udp, ip, ver, sizeof(ver));
		if (!ok) {
			swprintf((wchar_t[160]){0}, 160, L"版本查询失败: %hs",
			         UdpManager_GetLastError(g_upg.udp));
		}
	} else {
		/* CAN: 必须已连接 */
		if (!g_upg.can_connected) {
			SetWindowTextW(g_upg.hVersion, L"(请先连接 PCAN)");
			return;
		}
		ok = CanManager_GetVersion(g_upg.can, ver, sizeof(ver));
		if (!ok) {
			swprintf((wchar_t[160]){0}, 160, L"版本查询失败: %hs",
			         CanManager_GetLastError(g_upg.can));
		}
	}

	if (ok) {
		wchar_t wver[160] = {0};
		MultiByteToWideChar(CP_UTF8, 0, ver, -1, wver, 160);
		SetWindowTextW(g_upg.hVersion, wver);
		log_append_ptr(L"版本查询成功");
	} else {
		SetWindowTextW(g_upg.hVersion, L"(查询失败)");
		wchar_t m[200];
		swprintf(m, 200, L"版本查询失败: %hs",
		         can ? CanManager_GetLastError(g_upg.can)
		             : UdpManager_GetLastError(g_upg.udp));
		log_append_ptr(m);
	}
}

/* ===== CAN 连接 (UI 线程) ===== */

/* 刷新 PCAN 设备下拉: 调 DetectDevice 探测首个 PCAN-USB 通道. */
static void refresh_can_device(void)
{
	SendMessageW(g_upg.hCanDev, CB_RESETCONTENT, 0, 0);
	int ch = 0;
	if (CanManager_DetectDevice(g_upg.can, &ch)) {
		wchar_t buf[32];
		swprintf(buf, 32, L"PCAN-USB 通道 %d", ch);
		SendMessageW(g_upg.hCanDev, CB_ADDSTRING, 0, (LPARAM)buf);
		SendMessageW(g_upg.hCanDev, CB_SETITEMDATA, 0, (LPARAM)ch);
		SendMessageW(g_upg.hCanDev, CB_SETCURSEL, 0, 0);
		log_append_ptr(L"已刷新: 检测到 PCAN-USB 设备");
	} else {
		SendMessageW(g_upg.hCanDev, CB_ADDSTRING, 0, (LPARAM)L"(未检测到 PCAN-USB)");
		SendMessageW(g_upg.hCanDev, CB_SETCURSEL, 0, 0);
		log_append_ptr(L"已刷新: 未检测到 PCAN-USB 设备");
	}
}

/* 连接/断开 PCAN. 切换按钮文字 + 状态. */
static void on_can_connect(void)
{
	if (g_upg.can_connected) {
		CanManager_Disconnect(g_upg.can);
		g_upg.can_connected = false;
		g_upg.can_channel = -1;
		SetWindowTextW(g_upg.hCanConn, L"连接");
		EnableWindow(g_upg.hCanDev, TRUE);
		EnableWindow(g_upg.hCanBaud, TRUE);
		log_append_ptr(L"PCAN 已断开");
		return;
	}
	int sel = (int)SendMessageW(g_upg.hCanDev, CB_GETCURSEL, 0, 0);
	if (sel < 0) {
		MessageBoxW(g_upg.hSelf, L"请先点刷新检测 PCAN 设备", L"提示",
		            MB_ICONWARNING);
		return;
	}
	int channel = (int)SendMessageW(g_upg.hCanDev, CB_GETITEMDATA, sel, 0);
	int bsel = (int)SendMessageW(g_upg.hCanBaud, CB_GETCURSEL, 0, 0);
	uint32_t bitrate = (bsel >= 0 && bsel < BAUD_COUNT) ? g_bauds[bsel].btr
	                                                    : PCAN_BAUD_250K;
	if (!CanManager_Connect(g_upg.can, channel, bitrate)) {
		wchar_t m[256];
		swprintf(m, 256, L"PCAN 连接失败: %hs", CanManager_GetLastError(g_upg.can));
		MessageBoxW(g_upg.hSelf, m, L"连接失败", MB_ICONERROR);
		return;
	}
	g_upg.can_connected = true;
	g_upg.can_channel = channel;
	SetWindowTextW(g_upg.hCanConn, L"断开");
	log_append_ptr(L"PCAN 已连接, 查询设备版本...");
	EnableWindow(g_upg.hCanDev, FALSE);
	EnableWindow(g_upg.hCanBaud, FALSE);
	/* 连接成功后自动查询一次设备版本 */
	on_query_version();
}

/* ===== UDP worker 线程 =====
 * 全程只读 g_upg 缓存值 (cur_ip/cur_test/img), 仅通过 PostMessage 与 UI 交互. */

static DWORD WINAPI udp_upgrade_thread(LPVOID arg)
{
	(void)arg;
	post_log(L"UDP 升级开始");

	uint8_t status = 0;
	uint32_t sz = g_upg.img_size;
	const uint8_t *kh = g_upg.has_keyhash ? g_upg.keyhash : NULL;
	const char *ip = g_upg.cur_ip;

	if (!UdpManager_FwStart(g_upg.udp, ip, sz, kh, &status)) {
		post_log(L"FW_START 无响应 (设备未开机或 IP 错误)");
		post_done(0);
		return 0;
	}
	if (status == 2) {
		post_log(L"keyhash 不匹配, 设备拒绝升级");
		post_done(0);
		return 0;
	}
	if (status != 1) {
		post_log(L"FW_START 失败 (设备忙或存储不足)");
		post_done(0);
		return 0;
	}

	post_log(L"FW_START 成功, 开始发送数据");

	uint32_t off = 0;
	/* 固件 RX 缓冲 512B, payload <=511B (1B cmd + 511B data = 512B 帧). */
	const uint32_t CHUNK = 511;
	while (off < sz) {
		/* 检查取消 (每个 chunk 一次) */
		if (InterlockedCompareExchange(&g_upg.cancel, 0, 0)) {
			post_log(L"用户取消升级");
			post_done(0);
			return 0;
		}
		uint32_t n = sz - off;
		if (n > CHUNK) n = CHUNK;
		uint32_t roff = 0;
		if (!UdpManager_FwData(g_upg.udp, ip, g_upg.img + off, (int)n, &roff)) {
			wchar_t m[128];
			swprintf(m, 128, L"FW_DATA 失败 (offset=%u)", off);
			post_log(m);
			post_done(0);
			return 0;
		}
		off += n;
		/* 进度 0-90%: 数据发送阶段 */
		post_progress((int)((uint64_t)off * 90 / sz), 1);
	}

	/* CRC + FwEnd. test=1 测试模式 (不永久, 设备重启回滚). */
	uint16_t crc = UdpManager_CRC16_CCITT(g_upg.img, sz);
	uint8_t test = g_upg.cur_test ? 1 : 0;
	uint8_t result = 0;
	if (!UdpManager_FwEnd(g_upg.udp, ip, test, crc, &result) || result != 1) {
		post_log(L"FW_END 失败 (CRC 校验错误或设备写 flash 失败)");
		post_done(0);
		return 0;
	}

	post_progress(100, 2);
	post_log(L"升级完成, 设备将重启进行 MCUboot 交换");
	post_done(1);
	return 0;
}

/* ===== CAN worker 线程 ===== */

/* CAN 升级进度回调 (CanManager_FirmwareUpgrade 调用, 在 worker 线程上下文).
 * 函数名避开 can_manager.h 的 typedef can_progress_cb. */
static void can_progress_handler(int percent, void *user)
{
	(void)user;
	if (percent < 0) percent = 0;
	if (percent > 100) percent = 100;
	/* stage=1 发送数据 (CAN 升级中) */
	post_progress(percent, 1);
}

static DWORD WINAPI can_upgrade_thread(LPVOID arg)
{
	(void)arg;
	post_log(L"CAN 升级开始");

	const uint8_t *kh = g_upg.has_keyhash ? g_upg.keyhash : NULL;
	bool permanent = g_upg.cur_permanent; /* true=永久 (test unchecked) */

	bool ok = CanManager_FirmwareUpgrade(g_upg.can, g_upg.img, g_upg.img_size,
	                                     kh, permanent, can_progress_handler, NULL);
	if (!ok) {
		wchar_t m[256];
		swprintf(m, 256, L"CAN 升级失败: %hs", CanManager_GetLastError(g_upg.can));
		post_log(m);
		post_done(0);
		return 0;
	}

	post_progress(100, 2);
	post_log(L"CAN 升级完成, 设备将重启");
	post_done(1);
	return 0;
}

/* ===== 开始升级 / 取消 (UI 线程) ===== */

static void on_start(void)
{
	if (!g_upg.img || g_upg.img_size == 0) {
		MessageBoxW(g_upg.hSelf, L"请先选择有效的固件文件", L"提示",
		            MB_ICONWARNING);
		return;
	}
	if (g_upg.thread) {
		MessageBoxW(g_upg.hSelf, L"已有升级任务在进行", L"提示",
		            MB_ICONWARNING);
		return;
	}

	int can = current_channel();

	if (!can) {
		/* UDP: 检查目标 IP 已填 */
		read_ip_to_str(g_upg.hIp, g_upg.cur_ip, sizeof(g_upg.cur_ip));
		/* 简单校验: 4 段都填了 (空段 read_ip_to_str 当 0 处理, 这里要求非空) */
		bool ip_empty = false;
		for (int i = 0; i < 4; i++) {
			wchar_t buf[8];
			if (GetWindowTextW(g_upg.hIp[i], buf, 8) == 0) { ip_empty = true; break; }
		}
		if (ip_empty) {
			MessageBoxW(g_upg.hSelf, L"请填写目标设备 IP", L"提示",
			            MB_ICONWARNING);
			return;
		}
		/* UDP: 升级前查询一次设备版本, 刷新 label */
		on_query_version();
		g_upg.cur_test = (SendMessageW(g_upg.hTest, BM_GETCHECK, 0, 0) == BST_CHECKED);
		g_upg.cur_permanent = !g_upg.cur_test;
	} else {
		/* CAN: 检查已连接 */
		if (!g_upg.can_connected) {
			MessageBoxW(g_upg.hSelf, L"请先点 \"连接\" 接入 PCAN 设备", L"提示",
			            MB_ICONWARNING);
			return;
		}
		/* CAN: test 复选框含义对调 (与 UDP 复用同一控件, 但 CAN 升级时建议永久)
		 * 这里固定 permanent=true (UDP 的 test 复选框对 CAN 通道无意义, 因 CAN
		 * 升级 sub-section 不显示该复选框). */
		g_upg.cur_permanent = true;
	}

	/* 禁用开始/浏览, 重置进度条. 浏览须禁用: on_browse 会
	 * free_image() 后重赋 g_upg.img, 升级期间 worker 正读 img, 误触将
	 * use-after-free. */
	EnableWindow(g_upg.hStart, FALSE);
	EnableWindow(g_upg.hBrowse, FALSE);
	SendMessageW(g_upg.hProgress, PBM_SETPOS, 0, 0);
	SetWindowTextW(g_upg.hStatus, L"升级中...");
	InterlockedExchange(&g_upg.cancel, 0);

	DWORD tid = 0;
	HANDLE h = CreateThread(NULL, 0,
	                        can ? can_upgrade_thread : udp_upgrade_thread,
	                        NULL, 0, &tid);
	if (!h) {
		MessageBoxW(g_upg.hSelf, L"创建升级线程失败", L"错误", MB_ICONERROR);
		EnableWindow(g_upg.hStart, TRUE);
		EnableWindow(g_upg.hBrowse, TRUE);
		return;
	}
	g_upg.thread = h;
}

/* 重启设备: UDP 通道走 UdpManager_Reboot (0x05), CAN 通道走 CanManager_Reboot. */
static void on_reboot(void)
{
	int can = current_channel();
	bool ok;
	if (!can) {
		char ip[64] = {0};
		read_ip_to_str(g_upg.hIp, ip, sizeof(ip));
		bool ip_empty = false;
		for (int i = 0; i < 4; i++) {
			wchar_t buf[8];
			if (GetWindowTextW(g_upg.hIp[i], buf, 8) == 0) { ip_empty = true; break; }
		}
		if (ip_empty) {
			MessageBoxW(g_upg.hSelf, L"请先填写目标设备 IP", L"提示",
			            MB_ICONWARNING);
			return;
		}
		ok = UdpManager_Reboot(g_upg.udp, ip);
		if (!ok) {
			log_append_ptr(L"重启命令发送失败 (设备无响应)");
		} else {
			log_append_ptr(L"重启命令已发送");
		}
	} else {
		if (!g_upg.can_connected) {
			MessageBoxW(g_upg.hSelf, L"请先点 \"连接\" 接入 PCAN 设备", L"提示",
			            MB_ICONWARNING);
			return;
		}
		ok = CanManager_Reboot(g_upg.can);
		if (!ok) {
			wchar_t m[160];
			swprintf(m, 160, L"重启失败: %hs", CanManager_GetLastError(g_upg.can));
			log_append_ptr(m);
		} else {
			log_append_ptr(L"重启命令已发送");
		}
	}
}

/* ===== WM_COMMAND 分发 ===== */

static void on_command(WPARAM wParam)
{
	WORD id = LOWORD(wParam);
	WORD code = HIWORD(wParam);

	/* 通道单选切换 */
	if (id == IDC_UPG_CHAN_UDP && code == BN_CLICKED) {
		apply_channel_visibility();
		return;
	}
	if (id == IDC_UPG_CHAN_CAN && code == BN_CLICKED) {
		apply_channel_visibility();
		return;
	}
	if (code != BN_CLICKED) return;

	switch (id) {
	case IDC_UPG_BROWSE:    on_browse(); break;
	case IDC_UPG_GETVER:    on_query_version(); break;
	case IDC_UPG_START:     on_start(); break;
	case IDC_UPG_REBOOT:    on_reboot(); break;
	case IDC_UPG_CAN_CONN:  on_can_connect(); break;
	case IDC_UPG_CAN_REFRESH: refresh_can_device(); break;
	}
}

/* ===== WM_CREATE: 创建所有控件 ===== */

static void create_controls(HWND hWnd)
{
	g_upg.hSelf = hWnd;
	g_hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

	int gx = 12, gw = 776;

	/* ===== 升级通道 groupbox ===== */
	create_groupbox(L"升级通道", gx, 4, gw, 100);
	/* 行1: 通道单选 */
	create_label(L"通道:", gx + 12, 32, 40, 14);
	g_upg.hChanUdp = CreateWindowExW(0, L"BUTTON", L"UDP",
		WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP,
		gx + 56, 28, 60, 22, hWnd, (HMENU)(INT_PTR)IDC_UPG_CHAN_UDP, g_hInst, NULL);
	g_upg.hChanCan = CreateWindowExW(0, L"BUTTON", L"CAN (PCAN)",
		WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
		gx + 120, 28, 120, 22, hWnd, (HMENU)(INT_PTR)IDC_UPG_CHAN_CAN, g_hInst, NULL);
	SendMessageW(g_upg.hChanUdp, WM_SETFONT, (WPARAM)g_hFont, TRUE);
	SendMessageW(g_upg.hChanCan, WM_SETFONT, (WPARAM)g_hFont, TRUE);
	SendMessageW(g_upg.hChanUdp, BM_SETCHECK, BST_CHECKED, 0); /* 默认 UDP */

	/* 行2: UDP 目标 IP + 测试 (默认显示) */
	g_upg.hUdpLbl = create_label(L"目标 IP:", gx + 12, 62, 56, 14);
	int ip_ids[4] = { IDC_UPG_IP1, IDC_UPG_IP2, IDC_UPG_IP3, IDC_UPG_IP4 };
	create_ip_row(gx + 70, 58, ip_ids, g_upg.hIp);
	g_upg.hTest = CreateWindowExW(0, L"BUTTON", L"测试模式 (不永久)",
		WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
		gx + 260, 58, 180, 22, hWnd, (HMENU)(INT_PTR)IDC_UPG_TEST, g_hInst, NULL);
	SendMessageW(g_upg.hTest, WM_SETFONT, (WPARAM)g_hFont, TRUE);

	/* 行2 (重叠位置, 默认隐藏): CAN PCAN 设备 + 波特率 + 刷新 + 连接 */
	g_upg.hCanLbl1 = create_label(L"PCAN 设备:", gx + 12, 62, 64, 14);
	g_upg.hCanDev = CreateWindowExW(0, L"COMBOBOX", L"",
		WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
		gx + 78, 58, 140, 200, hWnd, (HMENU)(INT_PTR)IDC_UPG_CAN_DEV, g_hInst, NULL);
	SendMessageW(g_upg.hCanDev, WM_SETFONT, (WPARAM)g_hFont, TRUE);
	g_upg.hCanLbl2 = create_label(L"波特率:", gx + 226, 62, 48, 14);
	g_upg.hCanBaud = CreateWindowExW(0, L"COMBOBOX", L"",
		WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
		gx + 274, 58, 100, 200, hWnd, (HMENU)(INT_PTR)IDC_UPG_CAN_BAUD, g_hInst, NULL);
	SendMessageW(g_upg.hCanBaud, WM_SETFONT, (WPARAM)g_hFont, TRUE);
	for (int i = 0; i < BAUD_COUNT; i++) {
		SendMessageW(g_upg.hCanBaud, CB_ADDSTRING, 0, (LPARAM)g_bauds[i].label);
	}
	SendMessageW(g_upg.hCanBaud, CB_SETCURSEL, 0, 0); /* 默认 250k */
	g_upg.hCanRefresh = create_button(L"刷新", gx + gw - 130, 58, 60, 22, IDC_UPG_CAN_REFRESH);
	g_upg.hCanConn = create_button(L"连接", gx + gw - 60, 58, 60, 22, IDC_UPG_CAN_CONN);

	/* 默认 UDP, 隐藏 CAN 行 */
	apply_channel_visibility();

	/* ===== 版本信息行: label + 版本号 label + 查询按钮 ===== */
	g_upg.hVerLbl = create_label(L"设备版本:", gx + 12, 116, 60, 14);
	g_upg.hVersion = create_label(L"(未查询)", gx + 76, 114, 380, 14);
	g_upg.hGetVer = create_button(L"查询版本", gx + 446, 112, 80, 22, IDC_UPG_GETVER);

	/* ===== 固件文件 groupbox ===== */
	/* ===== 固件升级 groupbox (固件文件 + 升级控制合并) ===== */
	create_groupbox(L"固件升级", gx, 150, gw, 130);
	/* 行1: 固件文件路径 + 浏览 + 文件信息 */
	create_label(L"路径:", gx + 12, 176, 36, 14);
	g_upg.hFile = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
		WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_READONLY,
		gx + 50, 172, 440, 22, hWnd, (HMENU)(INT_PTR)IDC_UPG_FILE, g_hInst, NULL);
	SendMessageW(g_upg.hFile, WM_SETFONT, (WPARAM)g_hFont, TRUE);
	g_upg.hBrowse = create_button(L"浏览...", gx + 498, 172, 70, 22, IDC_UPG_BROWSE);
	g_upg.hFileInfo = create_label(L"(未选择)", gx + 576, 176, 200, 14);
	/* 行2: 进度 + 状态 (左侧), 开始升级 + 重启 (右侧右对齐) */
	create_label(L"进度:", gx + 12, 246, 36, 14);
	g_upg.hProgress = CreateWindowExW(0, PROGRESS_CLASSW, L"",
		WS_CHILD | WS_VISIBLE, gx + 50, 244, 260, 18,
		hWnd, (HMENU)(INT_PTR)IDC_UPG_PROGRESS, g_hInst, NULL);
	SendMessageW(g_upg.hProgress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
	SendMessageW(g_upg.hProgress, PBM_SETPOS, 0, 0);
	g_upg.hStatus = create_label(L"就绪", gx + 318, 246, 300, 14);
	g_upg.hStart = create_button(L"开始升级", gx + gw - 190, 242, 90, 26, IDC_UPG_START);
	g_upg.hReboot = create_button(L"重启", gx + gw - 90, 242, 80, 26, IDC_UPG_REBOOT);
	EnableWindow(g_upg.hStart, FALSE);

	/* ===== 操作日志 groupbox + 多行只读 EDIT ===== */
	create_groupbox(L"操作日志", gx, 300, gw, 468);
	g_upg.hLog = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
		WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY |
		ES_AUTOVSCROLL | WS_VSCROLL,
		gx + 12, 320, gw - 24, 440,
		hWnd, (HMENU)(INT_PTR)IDC_UPG_LOG, g_hInst, NULL);
	SendMessageW(g_upg.hLog, WM_SETFONT, (WPARAM)g_hFont, TRUE);

	/* 初始探测一次 PCAN 设备 (供切换到 CAN 时已有列表) */
	refresh_can_device();
}

/* ===== 窗口过程 ===== */

static LRESULT CALLBACK upg_wndproc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg) {
	case WM_CREATE:
		g_hInst = ((LPCREATESTRUCT)lParam)->hInstance;
		create_controls(hWnd);
		g_upg.udp = UdpManager_Create();
		g_upg.can = CanManager_Create();
		g_upg.thread = NULL;
		g_upg.cancel = 0;
		if (!g_upg.udp) {
			log_append_ptr(L"错误: UdpManager 创建失败");
		}
		if (!g_upg.can) {
			log_append_ptr(L"错误: CanManager 创建失败");
		}
		log_append_ptr(L"就绪. 请选择 .bin 固件文件");
		return 0;
	case WM_COMMAND:
		on_command(wParam);
		return 0;
	case WM_SIZE:
		/* 控件保持固定位置 (与 tab1 一致). */
		return 0;
	case WM_CTLCOLORDLG:
		/* 对话框底色 BTNFACE */
		return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
	case WM_CTLCOLORSTATIC: {
		HDC hdc = (HDC)wParam;
		HWND hCtrl = (HWND)lParam;
		/* 只读多行日志 EDIT 不能用 TRANSPARENT (会残留旧文字), 用不透明背景.
		 * 注: 只读 EDIT 也走 WM_CTLCOLORSTATIC. */
		if (GetWindowLongPtrW(hCtrl, GWL_STYLE) & ES_READONLY) {
			SetBkMode(hdc, OPAQUE);
			SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
			SetBkColor(hdc, GetSysColor(COLOR_WINDOW));
			return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
		}
		/* 其他 STATIC: 透明 + BTNFACE 底色 (视觉与父窗口一致). */
		SetBkMode(hdc, TRANSPARENT);
		SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
		return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
	}

	/* ===== 自定义消息: worker → UI ===== */
	case WM_APP_UPG_PROGRESS: {
		int percent = (int)wParam;
		int stage = (int)lParam;
		SendMessageW(g_upg.hProgress, PBM_SETPOS, percent, 0);
		wchar_t s[64];
		if (stage == 1) {
			swprintf(s, 64, L"发送数据 %d%%", percent);
		} else if (stage == 2) {
			wcscpy(s, L"等待设备重启 (MCUboot 交换)");
		} else {
			swprintf(s, 64, L"%d%%", percent);
		}
		SetWindowTextW(g_upg.hStatus, s);
		return 0;
	}
	case WM_APP_UPG_LOG: {
		const wchar_t *msg = (const wchar_t *)lParam;
		if (msg) {
			log_append_ptr(msg);
			free((void *)msg);
		}
		return 0;
	}
	case WM_APP_UPG_DONE: {
		int success = (int)wParam;
		/* 关闭线程句柄 */
		if (g_upg.thread) {
			CloseHandle(g_upg.thread);
			g_upg.thread = NULL;
		}
		/* 恢复按钮 (与 on_start 的禁用对称: start + 浏览) */
		EnableWindow(g_upg.hStart, g_upg.img ? TRUE : FALSE);
		EnableWindow(g_upg.hBrowse, TRUE);
		if (success) {
			SetWindowTextW(g_upg.hStatus, L"升级成功");
			MessageBoxW(g_hMain, L"升级成功", L"结果", MB_ICONINFORMATION);
		} else {
			SetWindowTextW(g_upg.hStatus, L"升级失败");
			MessageBoxW(g_hMain, L"升级失败 (详见日志)", L"结果", MB_ICONERROR);
		}
		return 0;
	}

	case WM_DESTROY:
		/* 等 worker 退出 (UI 销毁时通常已 DONE; 防御性等待避免悬挂线程) */
		if (g_upg.thread) {
			InterlockedExchange(&g_upg.cancel, 1);
			DWORD wres = WaitForSingleObject(g_upg.thread, 2000);
			if (wres == WAIT_OBJECT_0) {
				/* worker 已干净退出: 关闭句柄, 后续可安全销毁 manager/image */
				CloseHandle(g_upg.thread);
				g_upg.thread = NULL;
			} else {
				/* WAIT_TIMEOUT: worker 仍在阻塞 (典型为 CAN 升级的
				 * CanManager_FirmwareUpgrade — 纯阻塞调用, 无法中断).
				 * worker 仍在读 g_upg.img / 操作 g_upg.can, 此时若销毁
				 * manager 或释放 image 将触发 use-after-free. 故仅关闭线程
				 * 句柄 (让进程可退出), 跳过 manager/image 释放与 CAN 断开 —
				 * 这是有意为之的有界泄漏: 资源在进程终止时由 OS 回收.
				 * (泄漏永远安全; 释放另一线程正在用的内存永不安全.) */
				CloseHandle(g_upg.thread);
				g_upg.thread = NULL;
				return 0;
			}
		}
		if (g_upg.can_connected) {
			CanManager_Disconnect(g_upg.can);
			g_upg.can_connected = false;
		}
		if (g_upg.udp) {
			UdpManager_Destroy(g_upg.udp);
			g_upg.udp = NULL;
		}
		if (g_upg.can) {
			CanManager_Destroy(g_upg.can);
			g_upg.can = NULL;
		}
		free_image();
		return 0;
	}
	return DefWindowProcW(hWnd, msg, wParam, lParam);
}

/* ===== 公共 API ===== */

HWND UpgradeTab_Create(HWND hParent, HINSTANCE hInst)
{
	g_hInst = hInst;

	if (!g_classRegistered) {
		WNDCLASSW wc = {0};
		wc.lpfnWndProc = upg_wndproc;
		wc.hInstance = hInst;
		wc.hCursor = LoadCursor(NULL, IDC_ARROW);
		wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
		wc.lpszClassName = UPGRADE_TAB_CLASS;
		RegisterClassW(&wc);
		g_classRegistered = TRUE;
	}

	HWND h = CreateWindowExW(0, UPGRADE_TAB_CLASS, L"",
		WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
		0, 0, 700, 500, hParent, NULL, hInst, NULL);
	return h;
}
