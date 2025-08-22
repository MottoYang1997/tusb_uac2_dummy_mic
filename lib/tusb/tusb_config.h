#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_
#include "usb_decsriptors.h"

// RP2040: 双核但 USB 在 core0 上跑即可
#define CFG_TUSB_MCU                OPT_MCU_RP2040
#define BOARD_TUD_RHPORT            0
#define CFG_TUSB_RHPORT0_MODE       (OPT_MODE_DEVICE)
#define TUD_OPT_RP2040_USB_DEVICE_UFRAME_FIX 1

// 日志可关 2==info redirected to printf
#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG              2
#endif
#define CFG_TUSB_DEBUG_PRINTF       printf

//--------------------------------------------------------------------+
// 设备配置
//--------------------------------------------------------------------+
#define CFG_TUD_ENDPOINT0_SIZE      64

//--------------------------------------------------------------------+
// 类驱动：只开 Audio（UAC2）
//--------------------------------------------------------------------+
#define CFG_TUD_AUDIO               1
#define CFG_TUD_AUDIO_ENABLE_EP_IN  1     // 作为麦克风（IN 传到主机）
#define CFG_TUD_AUDIO_ENABLE_EP_OUT 0

// 支持帧大小流控（44.1 kHz 需要）
#define CFG_TUD_AUDIO_EP_IN_FLOW_CONTROL 1

// 只用中间软件 FIFO（环形缓冲）
#define CFG_TUD_MEM_SECTION
#define CFG_TUD_MEM_ALIGN           __attribute__((aligned(4)))

// ------- 必填：告知音频函数描述符长度与接口数量（见 usb_descriptors.c） -------
#define CFG_TUD_AUDIO_FUNC_1_DESC_LEN        (TUD_AUDIO_MIC_ONE_CH_DESC_LEN + ALT2_BLOCK_LEN)
#define CFG_TUD_AUDIO_FUNC_1_N_AS_INT        3   // AS 接口：Alt0(关闭) + Alt1(16bit) + Alt2(24bit)

// EP IN 最大包长（按 96kHz, 24bit, mono 算：96k * 3 / 1000 = 288 字节）
#define CFG_TUD_AUDIO_FUNC_1_EP_IN_SZ_MAX    288

// EP 软件缓冲至少与 max EP size 相同
#define CFG_TUD_AUDIO_FUNC_1_EP_IN_SW_BUF_SZ (10*CFG_TUD_AUDIO_FUNC_1_EP_IN_SZ_MAX)

// 控制缓冲（用于音量/静音等控制请求）
#define CFG_TUD_AUDIO_FUNC_1_CTRL_BUF_SZ     64

// 不做软件编码/解码
#define CFG_TUD_AUDIO_ENABLE_ENCODING        0
#define CFG_TUD_AUDIO_ENABLE_DECODING        0
#define CFG_TUD_AUDIO_ENABLE_TYPE_I_ENCODING 0
#define CFG_TUD_AUDIO_ENABLE_TYPE_I_DECODING 0

#endif