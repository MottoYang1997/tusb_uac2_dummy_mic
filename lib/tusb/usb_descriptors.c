#include "pico/stdlib.h"
#include "tusb_config.h"
#include "tusb.h"
#include "usb_descriptors.h"

// ---- 小工具：16字节一行的 hexdump ----
static void dump_hex(const void* data, uint16_t len, const char* tag)
{
  const uint8_t* p = (const uint8_t*)data;
  printf("[DUMP] %s, %u bytes\n", tag, len);
  for (uint16_t i = 0; i < len; i += 16) {
    printf("  %04x: ", i);
    for (uint16_t j = 0; j < 16 && i + j < len; ++j) printf("%02X ", p[i + j]);
    printf("\n");
  }
}

// ---- 解析并打印 Config 头部几个关键字段 ----
static void explain_cfg_header(const uint8_t* p)
{
  uint16_t wTotal = p[2] | (p[3] << 8);
  uint8_t  nItf   = p[4];
  uint8_t  attr   = p[7];
  uint8_t  maxPwr = p[8];
  printf("[CHK ] CFG: wTotal=%u, bNumInterfaces=%u, bmAttr=0x%02X, bMaxPower=%u (%umA)\n",
         wTotal, nItf, attr, maxPwr, maxPwr*2);
}
// ---- 简单解析 AC Header 的 wTotalLength（类特定 AC Header 的第二第三字节）----
// 要求：AC 接口在 IAD 后，应先出现标准 AC 接口，再跟 Class-Specific AC Header (HEADER subtype)
static void explain_ac_header(const uint8_t* p, uint16_t len)
{
  // 粗略扫描：找第一个 0x24(=CS_INTERFACE), 子类型=0x01(HEADER)
  for (uint16_t i=0; i+3 < len; ) {
    uint8_t bLen  = p[i+0];
    uint8_t bType = p[i+1];
    uint8_t bSub  = p[i+2];
    if (bLen==0 || i + bLen > len) break;
    if (bType==0x24 && bSub==0x01 && bLen >= 9) { // UAC2 HEADER 至少 9B（包含 bcdADC 和 wTotalLength 等）
      uint16_t acTotal = p[i+6] | (p[i+7] << 8);
      printf("[CHK ] AC Header: wTotalLength=%u (class-specific part)\n", acTotal);
      return;
    }
    i += bLen;
  }
  printf("[WARN] AC Header not found or too short\n");
}

//--------------------------------------------------------------------+
// 设备描述符
//--------------------------------------------------------------------+
static const tusb_desc_device_t desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200, // UAC2 仍可在 USB2.0 设备上
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = 0xCafe,
    .idProduct          = 0x4002,
    .bcdDevice          = 0x0100,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01
};

uint8_t const * tud_descriptor_device_cb(void) {
  printf("[DESC] device requested, %u bytes\n", (unsigned)sizeof(desc_device));
  return (uint8_t const *) &desc_device;
}

//--------------------------------------------------------------------+
// 字符串描述符（最小）
//--------------------------------------------------------------------+
char const* string_desc_arr[] = {
  (const char[]){ 0x09, 0x04 },     // 0: 语言 ID (English US)
  "TinyUSB UAC2 Mic",               // 1: Manufacturer
  "RP2040 Mono Mic",                // 2: Product
  "123456",                         // 3: Serial
  "UAC2"                            // 4: Audio Interface
};

static uint16_t _desc_str[32];

uint16_t const* tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
  (void) langid;
  uint8_t chr_count;
  printf("[DESC] string index=%u requested\n", index);

  if ( index == 0 ) {
    memcpy(&_desc_str[1], string_desc_arr[0], 2);
    chr_count = 1;
  } else {
    if ( !(index < sizeof(string_desc_arr)/sizeof(string_desc_arr[0])) ) return NULL;
    const char* str = string_desc_arr[index];
    chr_count = (uint8_t) strlen(str);
    if ( chr_count > 31 ) chr_count = 31;
    for(uint8_t i=0; i<chr_count; i++) _desc_str[1+i] = str[i];
  }

  _desc_str[0] = (TUSB_DESC_STRING << 8 ) | (2*chr_count + 2);
  return _desc_str;
}

//--------------------------------------------------------------------+
// 配置 + 音频描述符
//--------------------------------------------------------------------+

// 96 kHz, 单声道。本工程提供 Alt1=16bit 与 Alt2=24bit 两套格式。
#define CHANNELS          1
#define MAX_FREQ_HZ       96000

// 另准备 24bit 的参数
#define BYTES_PER_SAMPLE_16   2
#define BITS_USED_16          16
#define BYTES_PER_SAMPLE_24   3
#define BITS_USED_24          24

#define EP_SZ_16  TUD_AUDIO_EP_SIZE(MAX_FREQ_HZ, BYTES_PER_SAMPLE_16, CHANNELS) // 96
#define EP_SZ_24  TUD_AUDIO_EP_SIZE(MAX_FREQ_HZ, BYTES_PER_SAMPLE_24, CHANNELS) // 144

static const uint8_t _cfg_audio_dual[] = {
  // Config header
  TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN_DUAL_ALT, 0x80, 250),
// 先放“单位宽”模板（我们选 16bit 做 Alt1）
  TUD_AUDIO_DESC_IAD(ITF_NUM_AUDIO_CONTROL, 0x02, 0x00),
  TUD_AUDIO_DESC_STD_AC(ITF_NUM_AUDIO_CONTROL, 0x00, 0x00),
// AC Header：CLK→IT→FU→OT。totallen = CLK+IT+OT+FU。
  TUD_AUDIO_DESC_CS_AC(/*bcdADC*/0x0200, /*category*/AUDIO_FUNC_MICROPHONE,
                       /*totallen*/ TUD_AUDIO_DESC_CLK_SRC_LEN
                                   + TUD_AUDIO_DESC_INPUT_TERM_LEN
                                   + TUD_AUDIO_DESC_OUTPUT_TERM_LEN
                                   + TUD_AUDIO_DESC_FEATURE_UNIT_ONE_CHANNEL_LEN,
                       /*ctrl*/AUDIO_CS_AS_INTERFACE_CTRL_LATENCY_POS),
  TUD_AUDIO_DESC_CLK_SRC(/*clkid*/UAC2_CLK_ID, /*attr*/AUDIO_CLOCK_SOURCE_ATT_INT_VAR_CLK,
                         /*ctrl*/(AUDIO_CTRL_RW << AUDIO_CLOCK_SOURCE_CTRL_CLK_FRQ_POS),
                         /*assocTerm*/UAC2_IT_ID, /*stridx*/0x00),
  TUD_AUDIO_DESC_INPUT_TERM(/*termid*/UAC2_IT_ID, /*termtype*/AUDIO_TERM_TYPE_IN_GENERIC_MIC,
                            /*assocTerm*/UAC2_OT_ID, /*clkid*/UAC2_CLK_ID,
                            /*nchannelslogical*/0x01, /*channelcfg*/AUDIO_CHANNEL_CONFIG_NON_PREDEFINED,
                            /*idxchannelnames*/0x00, /*ctrl*/(AUDIO_CTRL_RW << AUDIO_IN_TERM_CTRL_CONNECTOR_POS), /*stridx*/0x00),
  TUD_AUDIO_DESC_OUTPUT_TERM(/*termid*/UAC2_OT_ID, /*termtype*/AUDIO_TERM_TYPE_USB_STREAMING,
                             /*assocTerm*/UAC2_IT_ID, /*srcid*/UAC2_FU_ID, /*clkid*/UAC2_CLK_ID, /*ctrl*/0x0000, /*stridx*/0x00),
  TUD_AUDIO_DESC_FEATURE_UNIT_ONE_CHANNEL(/*unitid*/UAC2_FU_ID, /*srcid*/UAC2_IT_ID,
      /*master*/ (AUDIO_CTRL_RW << AUDIO_FEATURE_UNIT_CTRL_MUTE_POS)
               | (AUDIO_CTRL_RW << AUDIO_FEATURE_UNIT_CTRL_VOLUME_POS),
      /*ch1*/    (AUDIO_CTRL_RW << AUDIO_FEATURE_UNIT_CTRL_MUTE_POS)
               | (AUDIO_CTRL_RW << AUDIO_FEATURE_UNIT_CTRL_VOLUME_POS),
      /*str*/0x00),

  // AS Alt0：0 带宽
  TUD_AUDIO_DESC_STD_AS_INT(/*itf*/(uint8_t)(ITF_NUM_AUDIO_STREAMING), /*alt*/0x00, /*nEPs*/0x00, /*str*/0x00),

  // AS Alt1：16-bit
  TUD_AUDIO_DESC_STD_AS_INT(/*itf*/(uint8_t)(ITF_NUM_AUDIO_STREAMING), /*alt*/0x01, /*nEPs*/0x01, /*str*/0x00),
  TUD_AUDIO_DESC_CS_AS_INT(/*termid*/UAC2_OT_ID, /*ctrl*/AUDIO_CTRL_NONE,
                           /*formattype*/AUDIO_FORMAT_TYPE_I,
                           /*formats*/AUDIO_DATA_FORMAT_TYPE_I_PCM,
                           /*nchannelsphysical*/0x01, /*channelcfg*/AUDIO_CHANNEL_CONFIG_NON_PREDEFINED, /*str*/0x00),
  TUD_AUDIO_DESC_TYPE_I_FORMAT(/*subslot*/BYTES_PER_SAMPLE_16, /*bits*/BITS_USED_16),
  // 等时 IN（Iso + Async + Data）
  TUD_AUDIO_DESC_STD_AS_ISO_EP(/*ep*/EPNUM_AUDIO_IN,
      /*attr*/(uint8_t)((uint8_t)TUSB_XFER_ISOCHRONOUS
                      | (uint8_t)TUSB_ISO_EP_ATT_ASYNCHRONOUS
                      | (uint8_t)TUSB_ISO_EP_ATT_DATA),
      /*maxEPsize*/EP_SZ_16, /*interval*/0x01),
  TUD_AUDIO_DESC_CS_AS_ISO_EP(/*attr*/AUDIO_CS_AS_ISO_DATA_EP_ATT_NON_MAX_PACKETS_OK,
      /*ctrl*/AUDIO_CTRL_NONE, /*lockunit*/AUDIO_CS_AS_ISO_DATA_EP_LOCK_DELAY_UNIT_UNDEFINED, /*lockdelay*/0x0000),

  // AS Alt2：24-bit（再追加一套）
  TUD_AUDIO_DESC_STD_AS_INT(/*itf*/(uint8_t)(ITF_NUM_AUDIO_STREAMING), /*alt*/0x02, /*nEPs*/0x01, /*str*/0x00),
  TUD_AUDIO_DESC_CS_AS_INT(/*termid*/UAC2_OT_ID, /*ctrl*/AUDIO_CTRL_NONE,
                           /*formattype*/AUDIO_FORMAT_TYPE_I,
                           /*formats*/AUDIO_DATA_FORMAT_TYPE_I_PCM,
                           /*nchannelsphysical*/0x01, /*channelcfg*/AUDIO_CHANNEL_CONFIG_NON_PREDEFINED, /*str*/0x00),
  TUD_AUDIO_DESC_TYPE_I_FORMAT(/*subslot*/BYTES_PER_SAMPLE_24, /*bits*/BITS_USED_24),
  TUD_AUDIO_DESC_STD_AS_ISO_EP(/*ep*/EPNUM_AUDIO_IN,
      /*attr*/(uint8_t)((uint8_t)TUSB_XFER_ISOCHRONOUS
                      | (uint8_t)TUSB_ISO_EP_ATT_ASYNCHRONOUS
                      | (uint8_t)TUSB_ISO_EP_ATT_DATA),
      /*maxEPsize*/EP_SZ_24, /*interval*/0x01),
  TUD_AUDIO_DESC_CS_AS_ISO_EP(/*attr*/AUDIO_CS_AS_ISO_DATA_EP_ATT_NON_MAX_PACKETS_OK,
      /*ctrl*/AUDIO_CTRL_NONE, /*lockunit*/AUDIO_CS_AS_ISO_DATA_EP_LOCK_DELAY_UNIT_UNDEFINED, /*lockdelay*/0x0000),
};

// 返回配置描述符
const uint8_t* tud_descriptor_configuration_cb(uint8_t index) {
  (void)index;
  uint16_t len = sizeof(_cfg_audio_dual);
  printf("[DESC] config requested, total_len=%u bytes\n", len);
  // 仅第一次返回时 dump（避免刷屏）。用一个静态标志。
  static bool dumped = false;
  if (!dumped) {
    // 打印前 9 字节 Config 头，确认 wTotalLength / bNumInterfaces
    explain_cfg_header(_cfg_audio_dual);
    // 打印整个配置 + 解释 AC Header 的 wTotalLength
    dump_hex(_cfg_audio_dual, len, "CONFIG+IAD+AC+AS");
    explain_ac_header(_cfg_audio_dual, len);
    // 基本一致性检查
    uint16_t wTotal = _cfg_audio_dual[2] | (_cfg_audio_dual[3] << 8);
    if (wTotal != len) {
      printf("[ERR ] Config.wTotalLength(%u) != builder_len(%u)\n", wTotal, len);
    }
    // Audio 函数期望长度（供驱动内部使用）是否与实际一致
#ifdef CFG_TUD_AUDIO_FUNC_1_DESC_LEN
  if (CFG_TUD_AUDIO_FUNC_1_DESC_LEN != len - TUD_CONFIG_DESC_LEN) {
    printf("[WARN] CFG_TUD_AUDIO_FUNC_1_DESC_LEN=%u != function_len=%u\n",
           (unsigned)CFG_TUD_AUDIO_FUNC_1_DESC_LEN, len - TUD_CONFIG_DESC_LEN);
  }
#endif
    dumped = true;
 }
  (void)len;
  return _cfg_audio_dual;
}
