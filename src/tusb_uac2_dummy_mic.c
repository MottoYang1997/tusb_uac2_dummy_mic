#include <math.h>
#include <string.h>
#include "pico/stdlib.h"
#include "bsp/board.h"

#include "tusb_config.h"
#include "tusb.h"
#include "usb_descriptors.h"

// ===== 本文件职责 =====
// 1) 处理 UAC2 控制面（GET/SET Entity：采样率、音量/静音、IT Connector）
// 2) 音频数据面：在 tud_audio_tx_done_isr() 中按“每毫秒样本数”写入 EP FIFO
// 3) ★重要：请手动将 TinyUSB 更新到最新版（master 或最新 release）。
//    本工程依赖其 Audio 类在 SET_INTERFACE/流控上的修复；并确保 lib/tusb/tusb_config.h 中
//    CFG_TUD_AUDIO_EP_IN_FLOW_CONTROL=1 以支持 44.1kHz 抖包。

// 正弦波发生器状态
static float phase = 0.0f;

// USB States and Variables
static uint8_t g_cur_alt = AS_ALT0_STOP;              // 0/1/2  Alt1=16, Alt2=24
static volatile uint32_t g_sample_rate = 44100;              // 当前采样率（Hz）
static volatile uint8_t  g_mute_cur = 0;                     // 0/1
static volatile int16_t  g_vol_min  = (-60) * 256;           // -60 dB
static volatile int16_t  g_vol_max  = (  0) * 256;           //  0 dB
static volatile int16_t  g_vol_res  = (  1) * 256;           //  1 dB 步进
static volatile int16_t  g_vol_cur  = ( -6) * 256;           // -6 dB

// 将线性音量（dB/256）应用到样本上
static inline float volume_scale(void) {
  float db = ((float)g_vol_cur) / 256.0f;
  return powf(10.0f, db / 20.0f);
}

// 仅用于打印友好名称（便于调试）
static const char* entity_name(uint8_t id) {
  switch (id) {
    case UAC2_CLK_ID: return "CLK_SRC";
    case UAC2_IT_ID:  return "IT (Mic)";
    case UAC2_FU_ID:  return "FU (Mute/Vol)";
    case UAC2_OT_ID:  return "OT (USB)";
    default: return "UNKNOWN";
  }
}

// UAC Entity Getter and Setter Callback
bool tud_audio_get_req_entity_cb(uint8_t rhport, tusb_control_request_t const * p_request) {
  uint8_t  entityID = TU_U16_HIGH(p_request->wIndex);
  uint8_t  ctrlSel  = TU_U16_HIGH(p_request->wValue);
  uint8_t  channel  = TU_U16_LOW (p_request->wValue);
  uint8_t  req      = p_request->bRequest;

  printf("[CTL ][GET ] ent=0x%02X(%s) sel=0x%02X ch=%u req=0x%02X wLen=%u\n",
         entityID, entity_name(entityID), ctrlSel, channel, req, p_request->wLength);
 
  // Clock Source（采样率范围 / 当前值 / 有效位）
  if (entityID == UAC2_CLK_ID) {
    if (ctrlSel == AUDIO_CS_CTRL_SAM_FREQ) {
      if (req == AUDIO_CS_REQ_RANGE) {
        // 构造 RANGE 返回：2 个子区间，分别固定 44100 和 96000
        struct TU_ATTR_PACKED {
          uint16_t wNumSubRanges;
          struct { uint32_t bMin, bMax, bRes; } sub[2];
        } range;

        range.wNumSubRanges = 2;
        range.sub[0].bMin = range.sub[0].bMax = 44100; range.sub[0].bRes = 0;
        range.sub[1].bMin = range.sub[1].bMax = 96000; range.sub[1].bRes = 0;

        return tud_audio_buffer_and_schedule_control_xfer(
          rhport, p_request, &range, sizeof(uint16_t) + 2 * sizeof(range.sub[0]));
      }
      else if (req == AUDIO_CS_REQ_CUR) {
        uint32_t cur = g_sample_rate;
        return tud_audio_buffer_and_schedule_control_xfer(
          rhport, p_request, &cur, sizeof(cur));
      }
    }
    else if (ctrlSel == AUDIO_CS_CTRL_CLK_VALID && req == AUDIO_CS_REQ_CUR) {
      uint8_t valid = 1; // 我们内部总能生成对应采样率
      return tud_audio_buffer_and_schedule_control_xfer(
        rhport, p_request, &valid, sizeof(valid));
    }
  }

  // Feature Unit（静音 & 音量）
  if (entityID == UAC2_FU_ID) {
    if (ctrlSel == AUDIO_FU_CTRL_MUTE && req == AUDIO_CS_REQ_CUR) {
      return tud_audio_buffer_and_schedule_control_xfer(
        rhport, p_request, (void*)&g_mute_cur, sizeof(g_mute_cur));
    }
    if (ctrlSel == AUDIO_FU_CTRL_VOLUME) {
      if (req == AUDIO_CS_REQ_RANGE) {
        // 按 UAC2 通用 RANGE 返回：wNumSubRanges(2) + 1 个 {int16_t min,max,res}(6) = 8 字节
        struct TU_ATTR_PACKED {
          uint16_t wNumSubRanges;
          struct { int16_t bMin, bMax, bRes; } sub[1];
        } vr;
        vr.wNumSubRanges   = 1;
        vr.sub[0].bMin     = g_vol_min;
        vr.sub[0].bMax     = g_vol_max;
        vr.sub[0].bRes     = g_vol_res;
        return tud_audio_buffer_and_schedule_control_xfer(
          rhport, p_request, &vr, sizeof(vr));
      } else if (req == AUDIO_CS_REQ_CUR) {
        return tud_audio_buffer_and_schedule_control_xfer(
          rhport, p_request, (void*)&g_vol_cur, sizeof(g_vol_cur));
      }
    }
  }

  if (entityID == UAC2_IT_ID && ctrlSel == AUDIO_TE_CTRL_CONNECTOR && req == AUDIO_CS_REQ_CUR) {
    audio_desc_channel_cluster_t ret;
    ret.bNrChannels    = 1;
    ret.bmChannelConfig= (audio_channel_config_t)0;
    ret.iChannelNames  = 0;
    return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &ret, sizeof(ret));
  }
  
  // 其他未实现 -> 让驱动 stall
  return false;
}

// ========== SET Entity 回调：主机写配置 ==========
bool tud_audio_set_req_entity_cb(uint8_t rhport, tusb_control_request_t const * p_request, uint8_t *pBuff) {
  (void) rhport;
  uint8_t entityID = TU_U16_HIGH(p_request->wIndex);
  uint8_t ctrlSel  = TU_U16_HIGH(p_request->wValue);
  uint8_t req      = p_request->bRequest;

  printf("[CTL ][SET ] ent=0x%02X(%s) sel=0x%02X req=0x%02X wLen=%u\n",
         entityID, entity_name(entityID), ctrlSel, req, p_request->wLength);
 
  if (entityID == UAC2_CLK_ID && ctrlSel == AUDIO_CS_CTRL_SAM_FREQ && req == AUDIO_CS_REQ_CUR) {
    // 主机下发新的采样率（4 字节）
    uint32_t new_fs;
    memcpy(&new_fs, pBuff, sizeof(new_fs));
    g_sample_rate = new_fs; // 记录到应用侧
    printf("New Sample Rate: %d Hz.\n", g_sample_rate);
    // TinyUSB 已在 DATA 阶段把 sample_rate_tx 写回内部，然后会重新计算包长
    // （需要 CFG_TUD_AUDIO_EP_IN_FLOW_CONTROL=1）
    // 这里不需要再改驱动内部状态。
    return true;
  }

  if (entityID == UAC2_FU_ID) {
    if (ctrlSel == AUDIO_FU_CTRL_MUTE && req == AUDIO_CS_REQ_CUR) {
      g_mute_cur = pBuff[0] ? 1 : 0;
      printf("Set Mute: %d\n", g_mute_cur);
      return true;
    }
    if (ctrlSel == AUDIO_FU_CTRL_VOLUME && req == AUDIO_CS_REQ_CUR) {
      int16_t v; memcpy(&v, pBuff, sizeof(v));
      // 夹到范围
      if (v < g_vol_min) v = g_vol_min;
      if (v > g_vol_max) v = g_vol_max;
      g_vol_cur = v;
      printf("Set Volume: %d\n", g_vol_cur);
      return true;
    }
  }

  return false; // 其他未实现，stall
}

bool tud_audio_tx_done_isr(uint8_t rhport, uint16_t n_bytes_sent,
                           uint8_t func_id, uint8_t ep_in, uint8_t g_cur_alt_setting)
{
  (void)rhport; (void)n_bytes_sent; (void)func_id; (void)ep_in;

  // Alt0 = 停流
  if (g_cur_alt_setting == 0) return true;

  g_cur_alt  = g_cur_alt_setting;

  // 每 1ms 需要的样本数：
  //  44.1kHz → 44 与 45 交替（小数累加满 1000 补 1）
  static uint16_t frac = 0;   // 0..999
  static uint32_t last_fs = 0;
  if (last_fs != g_sample_rate) { frac = 0; last_fs = g_sample_rate; }
  uint32_t per_ms = g_sample_rate / 1000;
  frac += (uint16_t)(g_sample_rate % 1000);
  if (frac >= 1000) { per_ms++; frac -= 1000; }

  float amp = 0.5f * volume_scale();
  float step = 2.0f * (float)M_PI * 440.0f / (float)g_sample_rate;

  if (g_cur_alt == AS_ALT1_16BIT) {  
    static int16_t buf16[192]; // 96k/1ms 上限
    for (uint32_t i=0; i<per_ms; i++) {
      float s = sinf(phase) * amp;
      phase += step; if (phase > 2.0f*(float)M_PI) phase -= 2.0f*(float)M_PI;
      int32_t v = (int32_t)lrintf(s * 32767.0f);
      buf16[i] = (int16_t)v;
    }
    tud_audio_write((uint8_t const*)buf16, per_ms * 2);
  } else if (g_cur_alt == AS_ALT2_24BIT) {
    static uint8_t buf24[288]; // 96k*3/1000 = 288
    for (uint32_t i=0; i<per_ms; i++) {
      float s = sinf(phase) * amp;
      phase += step; if (phase > 2.0f*(float)M_PI) phase -= 2.0f*(float)M_PI;
      int32_t v = (int32_t)lrintf(s * 8388607.0f);
      // 24-bit little-endian
      buf24[3*i+0] = (uint8_t)(v & 0xFF);
      buf24[3*i+1] = (uint8_t)((v >> 8) & 0xFF);
      buf24[3*i+2] = (uint8_t)((v >> 16) & 0xFF);
    }
    tud_audio_write(buf24, per_ms * 3);
  }
  return true;
}


// TinyUSB 要求：当 AS Alt 切换时解析参数，可通过 set_itf 回调获知
bool tud_audio_set_itf_cb(uint8_t rhport, tusb_control_request_t const *p_request) {
  (void)rhport;
  // p_request->wIndexL = 接口号，wValueH = 备用设置值
  uint8_t itf = TU_U16_LOW(p_request->wIndex);
  uint8_t alt = TU_U16_LOW(p_request->wValue);
  if (itf == 1) { // 我们的 AS 接口号
    g_cur_alt = alt;
  }
  printf("[ITF ] set interface=%u alt=%u\n", itf, alt);
  tud_audio_clear_ep_in_ff();
  return true;
}

bool tud_audio_set_itf_close_ep_cb(uint8_t rhport, tusb_control_request_t const *p_request) {
  (void)rhport;
  uint8_t itf = TU_U16_LOW(p_request->wIndex);
  printf("[ITF ] close EP on interface=%u (switching alt)\n", itf);
  // 关键：清空 EP IN 的软件 FIFO，丢弃残留，避免 alt 快速切换导致“EP 已激活”
  // 需要包含 usb_decsriptors.h 里定义的 EPNUM_AUDIO_IN（0x81）
  tud_audio_clear_ep_in_ff();
  // 也可以顺带复位你自己的波形状态（可选）：
  // phase = 0.0f;
  // （如果 pre_load 里有静态累加器，也建议提供一个复位函数来清它）
  return true;
}

void tud_mount_cb(void)     { printf("[BUS ] mounted\n"); }
void tud_umount_cb(void)    { printf("[BUS ] unmounted\n"); }
void tud_suspend_cb(bool remote_wakeup_en) { printf("[BUS ] suspend rw=%d\n", remote_wakeup_en); }
void tud_resume_cb(void)    { printf("[BUS ] resume\n"); }

int main(void) {
  board_init();
  tusb_init();
  printf("USB Init Complete.\n");
  while (true) {
    tud_task(); // TinyUSB 轮询
  }
}
