#ifndef __USB_DESCRIPTORS_H__
#define __USB_DESCRIPTORS_H__
#include "pico/stdlib.h"

// —— 接口号（示例，按你工程里实际为准）——
enum {
  ITF_NUM_AUDIO_CONTROL = 0,
  ITF_NUM_AUDIO_STREAMING,
  ITF_NUM_TOTAL
};

// —— 端点号（示例，按你工程里实际为准）——
#define EPNUM_AUDIO_IN  0x81

// —— UAC2 实体 ID：保持与描述符一致 —— 
#define UAC2_CLK_ID  0x10  // Clock Source ID
#define UAC2_FU_ID   0x20  // Feature Unit ID（含 Mute/Volume）
#define UAC2_IT_ID   0x30  // Input Terminal
#define UAC2_OT_ID   0x40  // Output Terminal

// --AS Alt和传输位数的关系
enum {
  AS_ALT0_STOP = 0,
  AS_ALT1_16BIT,
  AS_ALT2_24BIT
};

#define ALT2_BLOCK_LEN  ( TUD_AUDIO_DESC_STD_AS_INT_LEN \
                        + TUD_AUDIO_DESC_CS_AS_INT_LEN \
                        + TUD_AUDIO_DESC_TYPE_I_FORMAT_LEN \
                        + TUD_AUDIO_DESC_STD_AS_ISO_EP_LEN \
                        + TUD_AUDIO_DESC_CS_AS_ISO_EP_LEN )

#define CONFIG_TOTAL_LEN_DUAL_ALT (TUD_CONFIG_DESC_LEN \
                                 + TUD_AUDIO_MIC_ONE_CH_DESC_LEN \
                                 + ALT2_BLOCK_LEN)

// 配置描述符回调
extern const uint8_t* tud_descriptor_configuration_cb(uint8_t index);
extern const uint16_t TUD_AUDIO_MIC_DESC_LEN;
#endif
