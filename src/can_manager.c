/*
 * io-edge-hub 上位机 CAN 管理器 (Task 4)
 *
 * PCAN-USB 上的 io-edge-hub CAN 固件升级客户端. 帧布局 (固件权威源:
 * libs/can_fw_upgrade/can_fw_upgrade.c):
 *   0x101 主→设 命令: data_32[0]=cmd LE32, data_32[1]=arg LE32 (DLC=8)
 *   0x102 设→主 回复: data_32[0]=code LE32, data_32[1]=offset LE32 (DLC=8)
 *   0x103 主→设 固件数据: ≤8B 原始
 *   0x104 主→设 keyhash 分片: data[0]=seq(0..4), data[1..7]=7B (DLC=8, 共 5 帧)
 *   0x105 设→主 版本分片: data[0]=seq, data[1..7]=ASCII (末帧 '\0' 填充)
 *
 * PCAN 调用框架 (Initialize/Write/Read/LookUpChannel/FilterMessages) 复用
 * handler-receiver src/can_manager.c 的模式, 但改成同步 req/resp 模型
 * (无 RX 线程): 本模块只服务固件升级/版本/重启, 由 Task 7 worker 线程阻塞调用.
 * handler-receiver 的 RX 线程是为了同时分发业务帧 (心跳/RF24), 此处不需要.
 *
 * PCAN API 真实签名/字段名以本工程 include/pcan_loader.h 为准 (Task 2 已迁移),
 * 与 task-4-brief 的示例代码不一致处 (brief 用了 Pcan_Load/Pcan_Available/
 * msg.ID/MSGTYPE/LEN/DATA/0x02, 均不存在或错误) 在此修正.
 */
#include "can_manager.h"
#include "pcan_loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct CanManager {
	bool connected;
	TPCANHandle channel;     /* PCAN 通道句柄 (Connect 后非 0) */
	uint32_t bitrate;
	char last_error[128];
};

/* ================================================================
 * 内部原语: 帧收发
 * ================================================================ */

/* 把 PCAN 状态码格式化进 last_error (优先用 PCAN 自带文本, 退化到 0x%X).
 * 镜像 handler-receiver 的错误格式化思路. */
static void set_pcan_error(CanManager *m, const char *what, TPCANStatus st)
{
	char pcbuf[256] = {0};
	if (Pcan_GetErrorText) {
		Pcan_GetErrorText(st, 0x0409 /* English */, pcbuf);  /* 失败则留空 */
	}
	if (pcbuf[0]) {
		sprintf(m->last_error, "%s: 0x%08X %s", what, st, pcbuf);
	} else {
		sprintf(m->last_error, "%s: 0x%08X", what, st);
	}
}

/* 写一帧 (11-bit standard, DLC 0..8). 失败填 last_error. */
static bool can_write(CanManager *m, uint32_t id, const uint8_t *data, uint8_t dlc)
{
	if (!Pcan_Write) return false;
	TPCANMsg msg;
	msg.id = id;
	msg.msgtype = 0;   /* PCAN_MESSAGE_STANDARD (11-bit), 对齐 handler-receiver */
	if (dlc > 8) dlc = 8;
	msg.len = dlc;
	memcpy(msg.data, data, dlc);

	TPCANStatus st = Pcan_Write(m->channel, &msg);
	if (st != PCAN_ERROR_OK) {
		set_pcan_error(m, "PCAN_Write", st);
		return false;
	}
	return true;
}

/* 读一帧 (轮询直到匹配 expect_id 或超时). 仅 expect_id 的帧返回, 其余丢弃.
 * 超时填 last_error. out_code/out_arg 从 0x102 帧的 data_32[0]/[1] 解出 (LE32). */
static bool can_read_resp(CanManager *m, uint32_t expect_id, int timeout_ms,
                          uint32_t *out_code, uint32_t *out_arg)
{
	if (!Pcan_Read) return false;
	DWORD end = GetTickCount() + (DWORD)timeout_ms;
	for (;;) {
		TPCANMsg msg;
		TPCANTimestampMsg ts;
		TPCANStatus st = Pcan_Read(m->channel, &msg, &ts);
		if (st == PCAN_ERROR_OK) {
			if (msg.id == expect_id) {
				/* 0x102 帧布局: data[0..3]=code LE32, data[4..7]=offset/arg LE32.
				 * 即便 DLC<8 也按已收字节解 (固件恒发 8B). */
				uint32_t code = 0, arg = 0;
				uint8_t have = msg.len;
				if (have > 8) have = 8;
				if (have >= 4) {
					code = (uint32_t)msg.data[0] |
					       ((uint32_t)msg.data[1] << 8) |
					       ((uint32_t)msg.data[2] << 16) |
					       ((uint32_t)msg.data[3] << 24);
				}
				if (have >= 8) {
					arg = (uint32_t)msg.data[4] |
					      ((uint32_t)msg.data[5] << 8) |
					      ((uint32_t)msg.data[6] << 16) |
					      ((uint32_t)msg.data[7] << 24);
				}
				if (out_code) *out_code = code;
				if (out_arg) *out_arg = arg;
				return true;
			}
			/* 其他 ID 的帧: 丢弃继续读 (硬件过滤器已基本屏蔽) */
		}
		/* 非成功 (含接收队列空 0x00020): 让出 CPU 继续轮询 */
		if ((long)(end - GetTickCount()) <= 0) {
			sprintf(m->last_error, "CAN 回复超时 (等 0x%03X, %dms)", expect_id, timeout_ms);
			return false;
		}
		Sleep(1);
	}
}

/* ================================================================
 * 生命周期
 * ================================================================ */

CanManager *CanManager_Create(void)
{
	return (CanManager *)calloc(1, sizeof(CanManager));
}

void CanManager_Destroy(CanManager *m)
{
	if (!m) return;
	CanManager_Disconnect(m);
	free(m);
}

const char *CanManager_GetLastError(CanManager *m)
{
	return m ? m->last_error : "NULL manager";
}

/* ================================================================
 * 设备探测与连接
 *
 * 镜像 handler-receiver 的 Pcan_LookUpChannel 模式: 按 "devicetype=pcan_usb,
 * controllernumber=N" 查询 USB 通道句柄. 不用 brief 里的 Initialize 探测循环
 * (那会真实 open/close 通道, 可能与正在用的实例冲突).
 * ================================================================ */

bool CanManager_DetectDevice(CanManager *m, int *out_channel)
{
	if (!m) return false;
	if (!PcanLoader_Load() || !Pcan_LookUpChannel) {
		sprintf(m->last_error, "未加载 PCANBasic.dll, 请安装 PCAN-Basic 驱动");
		return false;
	}

	for (uint32_t i = 0; i < 16; i++) {
		TPCANHandle ch = PCAN_NONEBUS;
		char szLookup[64];
		sprintf(szLookup, "devicetype=pcan_usb,controllernumber=%u", i);
		if (Pcan_LookUpChannel(szLookup, &ch) == PCAN_ERROR_OK && ch != PCAN_NONEBUS) {
			if (out_channel) *out_channel = (int)ch;
			return true;
		}
	}
	sprintf(m->last_error, "未检测到 PCAN-USB 设备");
	return false;
}

bool CanManager_Connect(CanManager *m, int channel, uint32_t bitrate)
{
	if (!m) return false;
	if (!PcanLoader_Load() || !Pcan_Initialize) {
		sprintf(m->last_error, "未加载 PCANBasic.dll, 请安装 PCAN-Basic 驱动");
		return false;
	}
	/* 若已在连接, 先断开避免重复 Initialize 同一通道 */
	if (m->connected) {
		CanManager_Disconnect(m);
	}

	TPCANStatus st = Pcan_Initialize((uint32_t)channel, bitrate, 0, 0, 0);
	if (st != PCAN_ERROR_OK) {
		set_pcan_error(m, "PCAN_Initialize", st);
		return false;
	}
	m->channel = (TPCANHandle)channel;
	m->bitrate = bitrate;
	m->connected = true;

	/* 配置接收过滤器: 仅放行固件回复(0x102)与版本分片(0x105), 屏蔽总线其他流量,
	 * 避免升级期间 RX 队列被无关帧灌满. 多次 FilterMessages 调用为累加 (OR).
	 * (对齐 handler-receiver Connect 的过滤器设置思路) */
	if (Pcan_FilterMessages) {
		Pcan_FilterMessages(m->channel, CAN_ID_IO_RESP, CAN_ID_IO_RESP, 0);
		Pcan_FilterMessages(m->channel, CAN_ID_IO_VERSION, CAN_ID_IO_VERSION, 0);
	}
	return true;
}

void CanManager_Disconnect(CanManager *m)
{
	if (!m || !m->connected) return;
	if (Pcan_Uninitialize) {
		Pcan_Uninitialize(m->channel);
	}
	m->connected = false;
	m->channel = 0;
}

bool CanManager_IsConnected(const CanManager *m)
{
	return m && m->connected;
}

/* ================================================================
 * 固件升级
 *
 * 流程 (对齐固件 can_fw_upgrade.c handle_platform_rx / handle_fw_data):
 *  1. keyhash (可选): 5 帧 0x104, data[0]=seq(0..4), data[1..7]=7B chunk.
 *     固件 handle_keyhash_frame 累积 rx_keybuf, START 时校验.
 *  2. START (0x101 cmd=0, arg=size): 固件擦 slot1 flash 后回 OFFSET(0),
 *     keyhash 不符回 KEYHASH_ERROR(6), flash 擦失败回 FLASH_ERROR(4).
 *     注: 固件校验 DLC==8, 故 START 帧 DLC 必须为 8.
 *  3. 流式 0x103 (8B/帧): 固件每写 64B 回 OFFSET 做流控, 写满总量回 UPDATE_SUCCESS.
 *     主机每 8 帧 (64B) 或最后 1 帧读一次回复 (drain OFFSET + 捕获 UPDATE_SUCCESS).
 *  4. CONFIRM (0x101 cmd=1, arg=permanent?1:0): 固件 boot_request_upgrade 后回
 *     CONFIRM(3, arg=0x55AA55AA); 写入量不符或 boot 失败回 TRANSFER_ERROR(5).
 *
 * brief 把数据流的 UPDATE_SUCCESS 等待从循环里分出去 (循环后再读一次), 会与
 * "末帧后立即读" 重复消费 → 第二次必超时. 这里按 handler-receiver 的合并写法:
 * ack_count%8==0 || off>=size 时读, 末次读即 UPDATE_SUCCESS.
 * ================================================================ */

/* 发 keyhash 5 帧 (32B = 7*4 + 4). 失败填 last_error 并返回 false. */
static bool send_keyhash(CanManager *m, const uint8_t keyhash[32])
{
	for (int seq = 0; seq < 5; seq++) {
		uint8_t fr[8] = {0};
		fr[0] = (uint8_t)seq;
		int rem = 32 - seq * 7;
		int chunk = (rem > 7) ? 7 : rem;
		memcpy(fr + 1, keyhash + seq * 7, chunk);
		if (!can_write(m, CAN_ID_IO_KEYHASH, fr, 8)) {
			return false;
		}
	}
	return true;
}

bool CanManager_FirmwareUpgrade(CanManager *m, const uint8_t *img, uint32_t size,
                                const uint8_t keyhash[32], bool permanent,
                                can_progress_cb progress, void *user)
{
	if (!m || !m->connected) {
		if (m) sprintf(m->last_error, "CAN 未连接");
		return false;
	}
	if (!img || size == 0) {
		sprintf(m->last_error, "镜像为空");
		return false;
	}

	/* 1. keyhash (可选) */
	if (keyhash) {
		if (!send_keyhash(m, keyhash)) {
			return false;
		}
	}

	/* 2. START (cmd=0, arg=size LE32, DLC=8) */
	{
		uint8_t fr[8] = {0};
		fr[0] = IO_FW_CMD_START_UPDATE;
		uint32_t sz = size;
		memcpy(fr + 4, &sz, 4);   /* data_32[1] = size LE32 */
		if (!can_write(m, CAN_ID_IO_CMD, fr, 8)) {
			return false;
		}
		uint32_t code = 0, arg = 0;
		/* START 含 flash 整区擦除 (512KB-1MB), 给 15s (与 handler-receiver 一致) */
		if (!can_read_resp(m, CAN_ID_IO_RESP, 15000, &code, &arg)) {
			return false;
		}
		switch (code) {
		case IO_FW_CODE_OFFSET:
			break;   /* OK: 进入数据流 */
		case IO_FW_CODE_KEYHASH_ERROR:
			sprintf(m->last_error, "固件拒绝: keyhash 校验失败");
			return false;
		case IO_FW_CODE_FLASH_ERROR:
			sprintf(m->last_error, "固件拒绝: FLASH 擦除失败");
			return false;
		default:
			sprintf(m->last_error, "START 未知回复 code=%u arg=0x%08X", code, arg);
			return false;
		}
	}

	/* 3. 流式 0x103 (8B/帧). 每 8 帧 (64B) 或最后 1 帧读一次回复:
	 *   - 平时为 OFFSET 流控, 直接 drain;
	 *   - 末帧后固件回 UPDATE_SUCCESS;
	 *   - 任何 FLASH_ERROR 即中止.
	 * 流式占进度 0-90%. */
	{
		uint32_t off = 0;
		int ack_count = 0;
		int last_pct = -1;
		while (off < size) {
			uint32_t n = (size - off > 8) ? 8 : (size - off);
			if (!can_write(m, CAN_ID_IO_DATA, img + off, (uint8_t)n)) {
				return false;
			}
			off += n;
			ack_count++;

			if (ack_count % 8 == 0 || off >= size) {
				uint32_t code = 0, arg = 0;
				if (!can_read_resp(m, CAN_ID_IO_RESP, 2000, &code, &arg)) {
					/* 流控超时: 容错继续, 最终失败由 CONFIRM 捕获 (对齐 handler-receiver) */
				} else if (code == IO_FW_CODE_FLASH_ERROR) {
					sprintf(m->last_error, "FLASH 写入错误 @%lu", (unsigned long)arg);
					return false;
				}
				/* OFFSET / UPDATE_SUCCESS 均视为流控通过 */
			}

			if (progress) {
				int pct = (int)((uint64_t)off * 90 / size);
				if (pct != last_pct) {
					progress(pct, user);
					last_pct = pct;
				}
			}
		}
	}

	/* 4. CONFIRM (cmd=1, arg=permanent?1:0 LE32, DLC=8).
	 * 固件 boot_request_upgrade 后回 CONFIRM(3, arg=0x55AA55AA). */
	{
		uint8_t fr[8] = {0};
		fr[0] = IO_FW_CMD_CONFIRM;
		uint32_t perm = permanent ? 1u : 0u;
		memcpy(fr + 4, &perm, 4);
		if (!can_write(m, CAN_ID_IO_CMD, fr, 8)) {
			return false;
		}
		uint32_t code = 0, arg = 0;
		/* CONFIRM 含 boot_request_upgrade (写 flash trailer), 给 30s */
		if (!can_read_resp(m, CAN_ID_IO_RESP, 30000, &code, &arg)) {
			return false;
		}
		if (code == IO_FW_CODE_TRANSFER_ERROR) {
			sprintf(m->last_error, "固件确认失败: 传输/BOOT 错误");
			return false;
		}
		if (code != IO_FW_CODE_CONFIRM || arg != IO_FW_CONFIRM_MAGIC) {
			sprintf(m->last_error, "CONFIRM 失败 code=%u arg=0x%08X", code, arg);
			return false;
		}
	}

	if (progress) progress(100, user);
	return true;
}

/* ================================================================
 * 版本查询 (0x101 cmd=2 → 0x102 code=2 + N 帧 0x105)
 *
 * 固件 fw_can_reply(VERSION, total_len) 后 fw_can_send_version_string 分帧发.
 * 主机: 收 0x102 (code=VERSION, arg=字符串总长) → 按 ceil(len/7) 收 0x105 帧,
 * 按 seq 拼接 7B 文本, 遇 '\0' 截断.
 * ================================================================ */

/* 版本分片缓冲上限: 32 帧 × 7B = 224B (固件实际 ~17B = 3 帧, 余量充足) */
#define VER_MAX_FRAMES 32

bool CanManager_GetVersion(CanManager *m, char *out_ver, int out_cap)
{
	if (!m || !m->connected) {
		if (m) sprintf(m->last_error, "CAN 未连接");
		return false;
	}
	if (!out_ver || out_cap <= 0) {
		sprintf(m->last_error, "版本缓冲非法");
		return false;
	}
	out_ver[0] = '\0';

	/* 发 VERSION 命令 (cmd=2) */
	uint8_t fr[8] = {0};
	fr[0] = IO_FW_CMD_VERSION;
	if (!can_write(m, CAN_ID_IO_CMD, fr, 8)) {
		return false;
	}

	/* 收 0x102: 期望 code=VERSION, arg=字符串总长 */
	uint32_t code = 0, total_len = 0;
	if (!can_read_resp(m, CAN_ID_IO_RESP, 2000, &code, &total_len)) {
		return false;
	}
	if (code != IO_FW_CODE_VERSION) {
		sprintf(m->last_error, "VERSION 意外回复 code=%u", code);
		return false;
	}
	if (total_len == 0) {
		return true;   /* 空字符串 */
	}

	uint8_t total_frames = (uint8_t)((total_len + 6) / 7);   /* ceil(len/7) */
	if (total_frames > VER_MAX_FRAMES) total_frames = VER_MAX_FRAMES;

	/* 按 seq 收集分片. 轮询直到全部到齐或超时 (2s). */
	char ver_text[VER_MAX_FRAMES][7];
	bool ver_got[VER_MAX_FRAMES];
	memset(ver_got, 0, sizeof(ver_got));

	DWORD end = GetTickCount() + 2000;
	for (;;) {
		bool all = true;
		for (int i = 0; i < total_frames; i++) {
			if (!ver_got[i]) { all = false; break; }
		}
		if (all) break;
		if ((long)(end - GetTickCount()) <= 0) break;

		TPCANMsg msg;
		TPCANTimestampMsg ts;
		TPCANStatus st = Pcan_Read(m->channel, &msg, &ts);
		if (st == PCAN_ERROR_OK && msg.id == CAN_ID_IO_VERSION && msg.len >= 1) {
			uint8_t seq = msg.data[0];
			if (seq < total_frames && !ver_got[seq]) {
				uint8_t txt = msg.len - 1;
				if (txt > 7) txt = 7;
				memcpy(ver_text[seq], msg.data + 1, txt);
				if (txt < 7) {
					memset(ver_text[seq] + txt, 0, 7 - txt);   /* 末帧 '\0' 填充 */
				}
				ver_got[seq] = true;
				continue;   /* 收到一帧立即重检是否齐全, 不 Sleep */
			}
		}
		Sleep(1);
	}

	/* 按 seq 拼接, 遇 '\0' 截断 */
	int out = 0;
	for (int i = 0; i < total_frames && out + 1 < out_cap; i++) {
		if (!ver_got[i]) break;   /* 帧缺失, 截断 */
		for (int j = 0; j < 7 && out + 1 < out_cap; j++) {
			if (ver_text[i][j] == '\0') { out = -1; break; }   /* 命中终止符 */
			out_ver[out++] = ver_text[i][j];
		}
		if (out < 0) break;
	}
	out_ver[(out < 0) ? 0 : out] = '\0';
	return (out > 0);
}

/* ================================================================
 * 重启 (0x101 cmd=3)
 *
 * 固件 sys_reboot(SYS_REBOOT_WARM) 立即执行, 不发回复 → 不等待.
 * ================================================================ */

bool CanManager_Reboot(CanManager *m)
{
	if (!m || !m->connected) {
		if (m) sprintf(m->last_error, "CAN 未连接");
		return false;
	}
	uint8_t fr[8] = {0};
	fr[0] = IO_FW_CMD_REBOOT;
	if (!can_write(m, CAN_ID_IO_CMD, fr, 8)) {
		return false;
	}
	return true;   /* reboot 回复不可靠, 不强求 */
}
