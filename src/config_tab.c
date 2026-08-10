/* io-edge-hub 上位机 - Tab1 "UDP 参数配置"
 *
 * 程序化创建全部控件 (不用 dialog resource), 用静态 g_cfg 持有所有控件 HWND +
 * UdpManager 实例 (tab 生命周期内复用). WM_CREATE 创建控件 + UdpManager_Create;
 * WM_DESTROY 调 UdpManager_Destroy.
 *
 * 布局 (主窗口最小 720x560, tab 显示区约 712x504):
 *   - 设备发现 groupbox: 发现按钮 + 设备下拉 + 目标 IP (4 段) + 版本行 + 查询/重启
 *   - 网络参数 groupbox: 新 IP (4 段) + 应用
 *   - Modbus 参数 groupbox: 从机地址 + 波特率下拉 + 应用/读取
 *   - CAN 参数 groupbox: CAN ID + 波特率(k) + 应用/读取
 *   - 出厂重置按钮
 *   - 操作日志 groupbox: 多行只读 EDIT (带时间戳)
 */
#include "udp_manager.h"   /* 须先于 windows.h 拉 winsock2.h (避免 winsock1 冲突) */
#include "config_tab.h"
#include "resource.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <wchar.h>

/* ===== 静态状态: 所有控件 HWND + UdpManager 实例 ===== */
typedef struct {
	HWND hSelf;
	HWND hDevList, hIp[4], hNip[4], hVersion, hMbSlave, hMbBaud;
	HWND hCanId, hCanBaud, hLog;
	UdpManager *udp;
} ConfigTab;

static ConfigTab g_cfg;
static HINSTANCE g_hInst = NULL;
static HFONT g_hFont = NULL;
static const wchar_t *CONFIG_TAB_CLASS = L"ioEdgeHubConfigTabCls";
static BOOL g_classRegistered = FALSE;

/* 标准波特率 (Modbus 下拉用) */
static const int g_bauds[] = { 4800, 9600, 19200, 38400, 57600, 115200 };
#define BAUD_COUNT (int)(sizeof(g_bauds) / sizeof(g_bauds[0]))

/* ===== 控件创建辅助 ===== */

static HWND create_label(const wchar_t *text, int x, int y, int w, int h)
{
	HWND hw = CreateWindowExW(0, L"STATIC", text,
		WS_CHILD | WS_VISIBLE, x, y, w, h,
		g_cfg.hSelf, NULL, g_hInst, NULL);
	SendMessageW(hw, WM_SETFONT, (WPARAM)g_hFont, TRUE);
	return hw;
}

static HWND create_edit(int x, int y, int w, int h, int id, DWORD extra)
{
	HWND hw = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
		WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | extra,
		x, y, w, h, g_cfg.hSelf, (HMENU)(INT_PTR)id, g_hInst, NULL);
	SendMessageW(hw, WM_SETFONT, (WPARAM)g_hFont, TRUE);
	return hw;
}

static HWND create_button(const wchar_t *text, int x, int y, int w, int h, int id)
{
	HWND hw = CreateWindowExW(0, L"BUTTON", text,
		WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
		x, y, w, h, g_cfg.hSelf, (HMENU)(INT_PTR)id, g_hInst, NULL);
	SendMessageW(hw, WM_SETFONT, (WPARAM)g_hFont, TRUE);
	return hw;
}

static HWND create_groupbox(const wchar_t *text, int x, int y, int w, int h)
{
	HWND hw = CreateWindowExW(0, L"BUTTON", text,
		WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
		x, y, w, h, g_cfg.hSelf, NULL, g_hInst, NULL);
	SendMessageW(hw, WM_SETFONT, (WPARAM)g_hFont, TRUE);
	return hw;
}

/* 创建一段 4 段 IP 编辑框 (ES_NUMBER + 限 3 字符). 返回末尾 x 坐标. */
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

/* 从 4 个 EDIT 控件读 IP, 写入 ip4[4]. 返回是否合法 (每段 0-255). */
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

/* 当前目标 IP 拼成点分十进制窄字符串. */
static void current_target_ip(char *out, int cap)
{
	int v[4];
	for (int i = 0; i < 4; i++) {
		wchar_t buf[8];
		GetWindowTextW(g_cfg.hIp[i], buf, 8);
		v[i] = _wtoi(buf);
	}
	snprintf(out, cap, "%d.%d.%d.%d", v[0], v[1], v[2], v[3]);
}

/* 日志框追加一行 (带 [HH:MM:SS] 时间戳). */
static void log_append(const wchar_t *msg)
{
	SYSTEMTIME st;
	GetLocalTime(&st);
	wchar_t line[600];
	swprintf(line, 600, L"[%02d:%02d:%02d] %ls\r\n",
	         st.wHour, st.wMinute, st.wSecond, msg);
	int len = GetWindowTextLengthW(g_cfg.hLog);
	SendMessageW(g_cfg.hLog, EM_SETSEL, len, len);
	SendMessageW(g_cfg.hLog, EM_REPLACESEL, 0, (LPARAM)line);
}

/* 通用: 传输失败时弹错误框 + 记日志. */
static void show_transport_error(const wchar_t *op)
{
	wchar_t m[256];
	swprintf(m, 256, L"%ls 失败: %hs", op, UdpManager_GetLastError(g_cfg.udp));
	MessageBoxW(g_cfg.hSelf, m, L"错误", MB_ICONERROR);
	log_append(m);
}

/* 把 baud 值选中到下拉框 (若无匹配项则追加并选中). */
static void select_baud(uint16_t baud)
{
	for (int i = 0; i < BAUD_COUNT; i++) {
		if (g_bauds[i] == baud) {
			SendMessageW(g_cfg.hMbBaud, CB_SETCURSEL, i, 0);
			return;
		}
	}
	/* 非标准值: 追加 */
	wchar_t buf[16];
	swprintf(buf, 16, L"%u", baud);
	int idx = (int)SendMessageW(g_cfg.hMbBaud, CB_ADDSTRING, 0, (LPARAM)buf);
	SendMessageW(g_cfg.hMbBaud, CB_SETCURSEL, idx, 0);
}

/* ===== WM_CREATE: 创建所有控件 ===== */

static void create_controls(HWND hWnd)
{
	g_cfg.hSelf = hWnd;
	g_hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

	/* 行坐标基准 */
	int gx = 8, gw = 700;

	/* ===== 设备发现 groupbox ===== */
	create_groupbox(L"设备发现", gx, 4, gw, 112);
	/* 行1: 发现按钮 + 设备下拉 */
	create_button(L"发现设备", gx + 12, 28, 90, 24, IDC_CFG_DISCOVER_BTN);
	create_label(L"设备列表:", gx + 116, 32, 64, 14);
	g_cfg.hDevList = CreateWindowExW(0, L"COMBOBOX", L"",
		WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
		gx + 180, 28, 300, 220, hWnd, (HMENU)(INT_PTR)IDC_CFG_DEVLIST, g_hInst, NULL);
	SendMessageW(g_cfg.hDevList, WM_SETFONT, (WPARAM)g_hFont, TRUE);
	/* 行2: 目标设备 IP */
	create_label(L"目标设备 IP:", gx + 12, 62, 90, 14);
	int ip_ids[4] = { IDC_CFG_IP1, IDC_CFG_IP2, IDC_CFG_IP3, IDC_CFG_IP4 };
	create_ip_row(gx + 104, 58, ip_ids, g_cfg.hIp);
	/* 行3: 版本 + 查询 + 重启 */
	create_label(L"版本:", gx + 12, 92, 40, 14);
	g_cfg.hVersion = create_label(L"(未查询)", gx + 54, 92, 280, 14);
	create_button(L"查询版本", gx + 440, 88, 90, 24, IDC_CFG_GETVER);
	create_button(L"重启", gx + 540, 88, 90, 24, IDC_CFG_REBOOT);

	/* ===== 网络参数 groupbox ===== */
	create_groupbox(L"网络参数", gx, 124, gw, 50);
	create_label(L"新 IP:", gx + 12, 148, 44, 14);
	int nip_ids[4] = { IDC_CFG_NIP1, IDC_CFG_NIP2, IDC_CFG_NIP3, IDC_CFG_NIP4 };
	create_ip_row(gx + 60, 144, nip_ids, g_cfg.hNip);
	create_button(L"应用", gx + 240, 144, 80, 24, IDC_CFG_NIP_APPLY);

	/* ===== Modbus 参数 groupbox ===== */
	create_groupbox(L"Modbus 参数", gx, 182, gw, 50);
	create_label(L"从机地址:", gx + 12, 206, 64, 14);
	g_cfg.hMbSlave = create_edit(gx + 76, 202, 50, 22, IDC_CFG_MB_SLAVE, ES_NUMBER);
	create_label(L"波特率:", gx + 140, 206, 48, 14);
	g_cfg.hMbBaud = CreateWindowExW(0, L"COMBOBOX", L"",
		WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
		gx + 188, 202, 100, 220, hWnd, (HMENU)(INT_PTR)IDC_CFG_MB_BAUD, g_hInst, NULL);
	SendMessageW(g_cfg.hMbBaud, WM_SETFONT, (WPARAM)g_hFont, TRUE);
	for (int i = 0; i < BAUD_COUNT; i++) {
		wchar_t buf[16];
		swprintf(buf, 16, L"%d", g_bauds[i]);
		SendMessageW(g_cfg.hMbBaud, CB_ADDSTRING, 0, (LPARAM)buf);
	}
	SendMessageW(g_cfg.hMbBaud, CB_SETCURSEL, 1, 0); /* 默认 9600 */
	create_button(L"应用", gx + 300, 202, 70, 24, IDC_CFG_MB_APPLY);
	create_button(L"读取", gx + 380, 202, 70, 24, IDC_CFG_MB_READ);

	/* ===== CAN 参数 groupbox ===== */
	create_groupbox(L"CAN 参数", gx, 240, gw, 50);
	create_label(L"CAN ID:", gx + 12, 264, 48, 14);
	g_cfg.hCanId = create_edit(gx + 64, 260, 60, 22, IDC_CFG_CAN_ID, ES_NUMBER);
	create_label(L"波特率(k):", gx + 140, 264, 64, 14);
	g_cfg.hCanBaud = create_edit(gx + 204, 260, 60, 22, IDC_CFG_CAN_BAUD, ES_NUMBER);
	create_button(L"应用", gx + 300, 260, 70, 24, IDC_CFG_CAN_APPLY);
	create_button(L"读取", gx + 380, 260, 70, 24, IDC_CFG_CAN_READ);

	/* ===== 出厂重置 (右对齐) ===== */
	create_button(L"出厂重置", gx + gw - 110, 300, 100, 28, IDC_CFG_FACTORY);

	/* ===== 操作日志 groupbox + 多行只读 EDIT ===== */
	create_groupbox(L"操作日志", gx, 338, gw, 160);
	g_cfg.hLog = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
		WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY |
		ES_AUTOVSCROLL | WS_VSCROLL,
		gx + 12, 358, gw - 24, 132,
		hWnd, (HMENU)(INT_PTR)IDC_CFG_LOG, g_hInst, NULL);
	SendMessageW(g_cfg.hLog, WM_SETFONT, (WPARAM)g_hFont, TRUE);
}

/* ===== WM_COMMAND: 按钮处理 ===== */

/* 设备下拉选择变更: 从 "io-edge-hub <a.b.c.d> v..." 解析 IP 自动填入. */
static void on_devlist_changed(void)
{
	int sel = (int)SendMessageW(g_cfg.hDevList, CB_GETCURSEL, 0, 0);
	if (sel < 0) return;
	wchar_t wentry[160] = {0};
	SendMessageW(g_cfg.hDevList, CB_GETLBTEXT, sel, (LPARAM)wentry);
	wchar_t *p1 = wcschr(wentry, L'<');
	wchar_t *p2 = p1 ? wcschr(p1, L'>') : NULL;
	if (!p1 || !p2) return;
	*p2 = L'\0';
	int ip[4];
	if (swscanf(p1 + 1, L"%d.%d.%d.%d", &ip[0], &ip[1], &ip[2], &ip[3]) == 4) {
		wchar_t buf[8];
		for (int i = 0; i < 4; i++) {
			swprintf(buf, 8, L"%d", ip[i]);
			SetWindowTextW(g_cfg.hIp[i], buf);
		}
		log_append(L"已从设备列表回填目标 IP");
	}
}

/* 发现设备: 调 DISCOVER, 拆分结果填下拉. */
static void on_discover(void)
{
	char buf[2048];
	int cnt = 0;
	SendMessageW(g_cfg.hDevList, CB_RESETCONTENT, 0, 0);
	log_append(L"正在发现设备...");
	if (UdpManager_Discover(g_cfg.udp, buf, sizeof(buf), &cnt)) {
		char *p = strtok(buf, "\n");
		while (p) {
			wchar_t w[160];
			MultiByteToWideChar(CP_UTF8, 0, p, -1, w, 160);
			SendMessageW(g_cfg.hDevList, CB_ADDSTRING, 0, (LPARAM)w);
			p = strtok(NULL, "\n");
		}
		if (cnt > 0) SendMessageW(g_cfg.hDevList, CB_SETCURSEL, 0, 0);
		wchar_t m[64];
		swprintf(m, 64, L"发现 %d 台设备", cnt);
		log_append(m);
	} else {
		log_append(L"未发现设备");
	}
}

/* 应用 IP (SET_IP 0x10): 目标 IP + 新 IP, 按结果分支. */
static void on_apply_ip(void)
{
	char ip[32];
	current_target_ip(ip, sizeof(ip));
	uint8_t nip[4];
	if (!read_ip4(g_cfg.hNip, nip)) {
		MessageBoxW(g_cfg.hSelf, L"新 IP 各段必须在 0-255", L"输入错误",
		            MB_ICONERROR);
		return;
	}
	uint8_t ok = 0;
	if (UdpManager_SetIp(g_cfg.udp, ip, nip, &ok)) {
		if (ok) {
			log_append(L"SET_IP 成功, 设备将重启");
			MessageBoxW(g_cfg.hSelf, L"IP 已设置, 设备将重启", L"成功",
			            MB_ICONINFORMATION);
		} else {
			log_append(L"SET_IP 被拒绝 (IP 末段 0/255 或首段 224-239)");
			MessageBoxW(g_cfg.hSelf, L"设备拒绝该 IP", L"警告",
			            MB_ICONWARNING);
		}
	} else {
		show_transport_error(L"SET_IP");
	}
}

/* 查询版本 (0x04). */
static void on_get_version(void)
{
	char ip[32];
	current_target_ip(ip, sizeof(ip));
	char ver[64] = {0};
	if (UdpManager_GetVersion(g_cfg.udp, ip, ver, sizeof(ver))) {
		wchar_t wver[64];
		MultiByteToWideChar(CP_UTF8, 0, ver, -1, wver, 64);
		SetWindowTextW(g_cfg.hVersion, wver);
		wchar_t m[128];
		swprintf(m, 128, L"GET_VERSION 成功: %hs", ver);
		log_append(m);
	} else {
		SetWindowTextW(g_cfg.hVersion, L"(查询失败)");
		show_transport_error(L"GET_VERSION");
	}
}

/* 重启 (0x05). 设备收到即重启, 回复不可靠. */
static void on_reboot(void)
{
	char ip[32];
	current_target_ip(ip, sizeof(ip));
	if (MessageBoxW(g_cfg.hSelf, L"确认重启目标设备?", L"确认",
	                MB_YESNO | MB_ICONQUESTION) != IDYES) {
		return;
	}
	UdpManager_Reboot(g_cfg.udp, ip);
	log_append(L"REBOOT 已发送");
	MessageBoxW(g_cfg.hSelf, L"重启命令已发送", L"提示", MB_ICONINFORMATION);
}

/* 应用 Modbus 参数 (SET_MODBUS 0x12). */
static void on_apply_modbus(void)
{
	char ip[32];
	current_target_ip(ip, sizeof(ip));
	wchar_t ws[8];
	GetWindowTextW(g_cfg.hMbSlave, ws, 8);
	int slave = _wtoi(ws);
	if (slave < 1 || slave > 247) {
		MessageBoxW(g_cfg.hSelf, L"从机地址应在 1-247", L"输入错误",
		            MB_ICONERROR);
		return;
	}
	int bsel = (int)SendMessageW(g_cfg.hMbBaud, CB_GETCURSEL, 0, 0);
	uint16_t baud = (bsel >= 0 && bsel < BAUD_COUNT) ? (uint16_t)g_bauds[bsel] : 9600;
	uint8_t ok = 0;
	if (UdpManager_SetModbus(g_cfg.udp, ip, (uint8_t)slave, baud, &ok)) {
		if (ok) {
			wchar_t m[64];
			swprintf(m, 64, L"SET_MODBUS 成功 (slave=%d, baud=%u)", slave, baud);
			log_append(m);
			MessageBoxW(g_cfg.hSelf, L"Modbus 参数已应用", L"成功",
			            MB_ICONINFORMATION);
		} else {
			log_append(L"SET_MODBUS 被拒绝");
			MessageBoxW(g_cfg.hSelf, L"设备拒绝该参数", L"警告",
			            MB_ICONWARNING);
		}
	} else {
		show_transport_error(L"SET_MODBUS");
	}
}

/* 读取 Modbus 参数 (GET_MODBUS 0x13). */
static void on_read_modbus(void)
{
	char ip[32];
	current_target_ip(ip, sizeof(ip));
	uint8_t slave = 0;
	uint16_t baud = 0;
	if (UdpManager_GetModbus(g_cfg.udp, ip, &slave, &baud)) {
		wchar_t buf[16];
		swprintf(buf, 16, L"%u", slave);
		SetWindowTextW(g_cfg.hMbSlave, buf);
		select_baud(baud);
		log_append(L"GET_MODBUS 成功");
	} else {
		show_transport_error(L"GET_MODBUS");
	}
}

/* 应用 CAN 参数 (SET_CAN 0x16). */
static void on_apply_can(void)
{
	char ip[32];
	current_target_ip(ip, sizeof(ip));
	wchar_t wid[16];
	GetWindowTextW(g_cfg.hCanId, wid, 16);
	int can_id = _wtoi(wid);
	wchar_t wb[16];
	GetWindowTextW(g_cfg.hCanBaud, wb, 16);
	int baud_k = _wtoi(wb);
	if (can_id < 0 || can_id > 0x7FF) {
		MessageBoxW(g_cfg.hSelf, L"CAN ID 应在 0-2047 (11 位标准 ID)",
		            L"输入错误", MB_ICONERROR);
		return;
	}
	if (baud_k <= 0) {
		MessageBoxW(g_cfg.hSelf, L"CAN 波特率(k) 必须为正数", L"输入错误",
		            MB_ICONERROR);
		return;
	}
	uint8_t ok = 0;
	if (UdpManager_SetCan(g_cfg.udp, ip, (uint16_t)can_id, (uint16_t)baud_k, &ok)) {
		if (ok) {
			wchar_t m[96];
			swprintf(m, 96, L"SET_CAN 成功 (id=0x%X, baud=%dk)", can_id, baud_k);
			log_append(m);
			MessageBoxW(g_cfg.hSelf, L"CAN 参数已应用", L"成功",
			            MB_ICONINFORMATION);
		} else {
			log_append(L"SET_CAN 被拒绝");
			MessageBoxW(g_cfg.hSelf, L"设备拒绝该参数", L"警告",
			            MB_ICONWARNING);
		}
	} else {
		show_transport_error(L"SET_CAN");
	}
}

/* 读取 CAN 参数 (GET_CAN 0x17). */
static void on_read_can(void)
{
	char ip[32];
	current_target_ip(ip, sizeof(ip));
	uint16_t can_id = 0, baud_k = 0;
	if (UdpManager_GetCan(g_cfg.udp, ip, &can_id, &baud_k)) {
		wchar_t buf[16];
		swprintf(buf, 16, L"%u", can_id);
		SetWindowTextW(g_cfg.hCanId, buf);
		swprintf(buf, 16, L"%u", baud_k);
		SetWindowTextW(g_cfg.hCanBaud, buf);
		log_append(L"GET_CAN 成功");
	} else {
		show_transport_error(L"GET_CAN");
	}
}

/* 出厂重置 (0x19): 需 MB_YESNO 警告确认 (擦除存储分区). */
static void on_factory_reset(void)
{
	if (MessageBoxW(g_cfg.hSelf,
	                L"确认出厂重置?\n将擦除所有参数 (IP/Modbus/CAN) 并重启设备",
	                L"危险操作", MB_YESNO | MB_ICONWARNING) != IDYES) {
		return;
	}
	char ip[32];
	current_target_ip(ip, sizeof(ip));
	uint8_t ok = 0;
	if (UdpManager_FactoryReset(g_cfg.udp, ip, &ok)) {
		if (ok) {
			log_append(L"出厂重置已执行, 设备将重启");
			MessageBoxW(g_cfg.hSelf, L"出厂重置已执行, 设备将重启", L"完成",
			            MB_ICONINFORMATION);
		} else {
			log_append(L"出厂重置被设备拒绝");
			MessageBoxW(g_cfg.hSelf, L"设备拒绝出厂重置", L"警告",
			            MB_ICONWARNING);
		}
	} else {
		show_transport_error(L"FACTORY_RESET");
	}
}

/* WM_COMMAND 总分发. */
static void on_command(WPARAM wParam, LPARAM lParam)
{
	WORD id = LOWORD(wParam);
	WORD code = HIWORD(wParam);

	/* 设备下拉选择变更: 自动回填目标 IP */
	if (id == IDC_CFG_DEVLIST && code == CBN_SELCHANGE) {
		on_devlist_changed();
		return;
	}
	if (code != BN_CLICKED) return;
	(void)lParam;

	switch (id) {
	case IDC_CFG_DISCOVER_BTN: on_discover(); break;
	case IDC_CFG_GETVER:       on_get_version(); break;
	case IDC_CFG_REBOOT:       on_reboot(); break;
	case IDC_CFG_NIP_APPLY:    on_apply_ip(); break;
	case IDC_CFG_MB_APPLY:     on_apply_modbus(); break;
	case IDC_CFG_MB_READ:      on_read_modbus(); break;
	case IDC_CFG_CAN_APPLY:    on_apply_can(); break;
	case IDC_CFG_CAN_READ:     on_read_can(); break;
	case IDC_CFG_FACTORY:      on_factory_reset(); break;
	}
}

/* ===== 窗口过程 ===== */

static LRESULT CALLBACK cfg_wndproc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg) {
	case WM_CREATE:
		g_hInst = ((LPCREATESTRUCT)lParam)->hInstance;
		create_controls(hWnd);
		g_cfg.udp = UdpManager_Create();
		if (!g_cfg.udp) {
			log_append(L"错误: UdpManager 创建失败");
		} else {
			log_append(L"就绪. 请先发现设备或填写目标 IP");
		}
		return 0;
	case WM_COMMAND:
		on_command(wParam, lParam);
		return 0;
	case WM_SIZE:
		/* 控件保持固定位置 (与 handler-receiver 一致). */
		return 0;
	case WM_CTLCOLORDLG:
	case WM_CTLCOLORSTATIC:
		/* 静态控件透明背景 + 对话框 BTNFACE 底色 (视觉与父窗口一致). */
		SetBkMode((HDC)wParam, TRANSPARENT);
		return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
	case WM_DESTROY:
		if (g_cfg.udp) {
			UdpManager_Destroy(g_cfg.udp);
			g_cfg.udp = NULL;
		}
		return 0;
	}
	return DefWindowProcW(hWnd, msg, wParam, lParam);
}

/* ===== 公共 API ===== */

HWND ConfigTab_Create(HWND hParent, HINSTANCE hInst)
{
	g_hInst = hInst;

	if (!g_classRegistered) {
		WNDCLASSW wc = {0};
		wc.lpfnWndProc = cfg_wndproc;
		wc.hInstance = hInst;
		wc.hCursor = LoadCursor(NULL, IDC_ARROW);
		wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
		wc.lpszClassName = CONFIG_TAB_CLASS;
		RegisterClassW(&wc);
		g_classRegistered = TRUE;
	}

	HWND h = CreateWindowExW(0, CONFIG_TAB_CLASS, L"",
		WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
		0, 0, 700, 500, hParent, NULL, hInst, NULL);
	return h;
}
