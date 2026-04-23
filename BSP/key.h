/*
 * @Author: liyongtai
 * @Date: 2024-11-13 13:50:53
 * @LastEditTime: 2024-11-14 14:06:38
 * @LastEditors: liyongtai
 * @Description:
 * @FilePath: \UMF_SOFTWARE\BSP\key.h
 * Copyright(c) 2020-2024 liyongtai All rights reserved
 */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef _KEY_H_
#define _KEY_H_
/* Includes ------------------------------------------------------------------*/

#include "main.h"
/* Exported constants --------------------------------------------------------*/
/* Exported types ------------------------------------------------------------*/
/* Exported macro ------------------------------------------------------------*/
#define Key_Sub (!(HAL_GPIO_ReadPin(K_MOV_GPIO_Port, K_MOV_Pin)))
#define Key_Add (!(HAL_GPIO_ReadPin(K_ADD_GPIO_Port, K_ADD_Pin)))
#define Key_Set (!(HAL_GPIO_ReadPin(K_SUB_GPIO_Port, K_SUB_Pin)))
/* Exported functions ------------------------------------------------------- */
void keyFunc(void);
void keyscan(void);
#endif /* _KEY_H_ */
