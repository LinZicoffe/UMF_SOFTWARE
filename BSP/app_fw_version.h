/**
 * @file    app_fw_version.h
 * @brief   App 固件版本标识（.fw_header 静态字段来源，A6 双版本构建只改本文件）
 *
 * app_ver 编码: (major << 16) | (minor << 8) | patch
 *   例 0x00010700 = 1.7.0；上位机显示口径见 FirmwareImage.AppVersionText。
 * build_id: 32 位自由标识，BL 邮箱 last_verified_build_id 与 G3 重置以其为键
 * （D3/S9：build_id 变化 ⇒ G3 重新起算）。
 */
#ifndef __APP_FW_VERSION_H
#define __APP_FW_VERSION_H

#define APP_FW_VERSION     0x00010700u  /* v1.7.0 */
#define APP_BUILD_ID       0x20260918u  /* 2026-09-18 构建 */

/* A6 双版本测试构建开关（param_storage_init 末尾仅 RAM 覆盖流量系数，不落盘）：
 *   0.0f  = 正式版（不覆盖，仓库默认提交状态）
 *   1.0f  = 测试镜像 k1（配合 APP_FW_VERSION 0x00010700 / BUILD 0x2026A001）
 *   2.0f  = 测试镜像 k2（配合 APP_FW_VERSION 0x00010701 / BUILD 0x2026A002）
 * 上位机读 PDU 24/25（4x 40025~40026 流量系数）即可分辨烧录的是哪一份。*/
#define APP_FORCE_METER_COEFF  0.0f

#endif /* __APP_FW_VERSION_H */
