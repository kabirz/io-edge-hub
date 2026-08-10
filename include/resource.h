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

/* tab2 控件 ID (2xxx) — Task 7 扩展 */
/* tab3 控件 ID (3xxx) — Task 9 扩展 */

#endif /* RESOURCE_H */
