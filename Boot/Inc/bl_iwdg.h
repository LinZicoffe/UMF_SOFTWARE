/**
 * @file    bl_iwdg.h
 * @brief   BL 独立看门狗管理（方案 v3.2 §4.6）
 *
 * 实测现状（§1.3）：App 的 IWDG = 分频4 + 重载999 ≈ 100ms（v3.1 后放宽为 ~1s），
 * 暖复位（App→BL）后仍在运行 ⇒ BL 入口必须立即喂狗。
 * 会话期改 5s：分频64 + 重载 3124（64×3125/40kHz ≈ 5.0s，LSI 30~60kHz）。
 * 改 PR/RLR 后必须立即写 KR=0xAAAA 重载（否则按旧 RLR 继续计数）。
 */
#ifndef BL_IWDG_H
#define BL_IWDG_H

#include "bl_common.h"

/* 喂狗。IWDG 未启动时写入无害（KR 仅在启动后有意义）。*/
void bl_iwdg_feed(void);

/* 配置并启动 5s 会话期看门狗（已运行时安全重配：先喂狗→解锁→等 PVU/RVU→
 * 写 PR/RLR → 再等同步 → 重载 → 启动）。
 * 契约：从调用本函数起，调用方必须已在旧周期（可能仅 ~100ms/1s）内保持
 * 喂狗节奏（入口一次喂狗 + 主循环周期性喂狗，方案 §4.6）。*/
void bl_iwdg_start_5s(void);

#endif /* BL_IWDG_H */
