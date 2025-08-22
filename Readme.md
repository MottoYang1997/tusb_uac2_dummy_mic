# RP2040 + TinyUSB UAC2 虚拟麦克风

本说明由ChatGPT修改而成。

> 这是一个面向 **不熟悉 TinyUSB，但对 USB Audio Class 2.0（UAC2）感兴趣** 的入门示例。
> 工程用 **Raspberry Pi RP2040** 跑出一个 **UAC2 单声道虚拟麦克风**，支持 **Alt1=16-bit**、**Alt2=24-bit**，采样率 **44.1 kHz / 96 kHz**，端点为 **Isochronous + Async IN**。
>
> ⚠️ 强烈建议：**把 TinyUSB 手动更新到最新版本（master 或最新 release）**。老版本在音频类的 `SET_INTERFACE`、流控上有已知问题，会导致“只能发 0 字节”“切 alt 不稳定”等诡异现象。
> 且务必在配置里 **开启帧长流控**：`CFG_TUD_AUDIO_EP_IN_FLOW_CONTROL=1`（见下文）。

---

## 目录结构

```
.
├─ lib/tusb/
│  ├─ tusb_config.h          # TinyUSB 配置（音频功能、缓冲区、流控等）
│  ├─ usb_descriptors.c      # UAC2 描述符（AC/AS、实体拓扑、端点等）
│  └─ usb_descriptors.h      # 接口号、端点号、实体 ID 等
├─ src/
│  └─ tusb_uac2_dummy_mic.c  # 业务逻辑（控制请求回调 + 音频发送回调）
├─ CMakeLists.txt
└─ pico_sdk_import.cmake
```

---

## 一分钟上手

1. **准备环境**

   * 安装 **Pico SDK**、CMake、ARM 工具链。
   * 把 **TinyUSB 更新到最新版**（submodule 或直接替换为官方最新）。

2. **编译**

   ```bash
   cmake -S . -B build -DPICO_SDK_PATH=/path/to/pico-sdk
   cmake --build build -j
   ```

3. **烧录**

   * BOOTSEL 插入电脑，把 `build/*.uf2` 拖进 U 盘，或用 `picotool load -x xxx.uf2`。

4. **验证**

   * Windows 11 / Linux 能看到一个“USB Microphone (UAC2)”。
   * 录音软件里切到 **16/24-bit、44.1/96 kHz** 试采；日志会打印 alt 切换与采样率。

---

## UAC2 的“两个面”：控制面 & 数据面

UAC2 分两部分：

* **控制面（Audio Control, AC）**：时钟、端口拓扑、音量/静音等控制请求。
* **数据面（Audio Streaming, AS）**：按采样节拍把音频样本塞进 **等时异步 IN 端点**。

### AC 拓扑（本工程）

```
Clock Source (CLK, 可变 & 采样率可读写)
    │
    ▼
Input Terminal (IT, 麦克风)
    │
    ▼
Feature Unit (FU, Mute/Volume)
    │
    ▼
Output Terminal (OT, USB Streaming)
```

* `IT.assocTerm = OT`，`OT.assocTerm = IT`（两端成对）。
* `FU.srcid = IT`，`OT.srcid = FU`（处理链路）。
* Clock Source 设为 **INT\_VAR\_CLK + RW**（主机可下发采样率）。

### AS 接口与端点

* **Alt0**：零带宽（关流）。
* **Alt1**：16-bit / 单声道 / Type-I PCM / **Iso + Async + Data**。
* **Alt2**：24-bit / 单声道 / Type-I PCM / **Iso + Async + Data**。
* 端点最大包长按 **96 kHz** 峰值计算，软件 FIFO 预留 ≥10ms 缓冲。

> Windows 在“改采样率/位宽”时常见序列：**Alt1/2 → Alt0 →（可能 SET\_CUR 采样率）→ Alt1/2**。
> 所以日志看到 alt 在 **0 与 1/2** 之间跳是正常的。

---

## 文件讲解与关键代码

### 1) `lib/usb_descriptors.c`：描述符是“设备的身份证”

你能在这里看到**完整的 UAC2 拓扑**与 AS 端点配置：

* **IAD + AC 标准接口 + AC 头**（把 **CLK、IT、OT、FU** 四块“拼”到一起）。
* **Clock Source**：可变 + RW；主机可对设备下发 `SET_CUR(SAM_FREQ)`。
* **Input/Output Terminal**：用 `assocTerm` 互相指向（成对），并用 `srcid` 把 FU 挂到中间。
* **Feature Unit**：Master 与 ch1 的 **Mute/Volume** 都标注为 **RW**。
* **AS Alt0/1/2**：每个 Alt 都包含：标准 AS 接口 → 类特定 AS 接口 → Type-I Format → 等时 IN 端点（及类特定端点）。

### 2) `lib/usb_descriptors.h`：把接口号/端点号/实体 ID 固定下来

* `ITF_NUM_AUDIO_CONTROL / ITF_NUM_AUDIO_STREAMING`
* `EPNUM_AUDIO_IN`（一般是 `0x81`）
* `UAC2_CLK_ID / UAC2_IT_ID / UAC2_FU_ID / UAC2_OT_ID`

> 统一 ID 让“描述符 ↔ 回调里的分发逻辑”一一对应，不会串。

### 3) `lib/tusb_config.h`：TinyUSB 的“总开关”

* **一定开启**：`#define CFG_TUD_AUDIO_EP_IN_FLOW_CONTROL 1`
  44.1kHz 每毫秒样本不是整数（44/45 交替），**没有帧长流控就会时不时空帧**。
* 配置音频功能的 **描述符长度**、**AS 接口数量**、**端点尺寸**、**软件 FIFO 大小**等。
* 调试量可用 `CFG_TUSB_DEBUG` 控制（0/1/2）。

> 再次强调：**TinyUSB 升级到最新版本**。新版音频栈对 `SET_INTERFACE`、流控与回调时序的修复很关键。

### 4) `src/tusb_uac2_dummy_mic.c`：所有“交互”都在这里

#### 控制请求回调（AC 面）

* `tud_audio_get_req_entity_cb()`

  * **Clock Source**：返回 **采样率 RANGE**（44.1k / 96k）与 **CUR**。
  * **Feature Unit**：返回 **Volume/Mute 的 RANGE/CUR**。
  * **Input Terminal**：返回 **Connector 的 CUR**（主机探测拓扑时会问）。

* `tud_audio_set_req_entity_cb()`

  * **SET\_CUR(SAM\_FREQ)**：主机下发新采样率 → 记录到 `g_sample_rate`。

    * 若使用真实 ADC：**此处重配 I2S/PLL** 并清 ring buffer/累加器。
  * **SET\_CUR(VOLUME/MUTE)**：写回 `g_vol_cur / g_mute_cur`（注意单位 **dB/256**）。

> 备注：Windows 对多Alt切换采样率的“麦克风”常用**软件增益**，调系统音量**不一定**下发 `SET_CUR(VOLUME)`。

#### 数据发送回调（AS 面）

* **必须实现**：`tud_audio_tx_done_isr(rhport, n_bytes_sent, func_id, ep_in, cur_alt_setting)`

  * 每发完一帧，TinyUSB 就调它；在这里**准备下一帧**。
  * **关键算法**：

    * `per_ms = fs / 1000` 的整数部分；
    * 用一个 **0..999 的小数累加器**累加 `fs % 1000`，**满 1000 就本帧 +1 个样本**。

      * 44.1k → 44 与 45 交替，**与驱动的帧长流控完全对齐**；
      * 从而消除 16-bit 下常见的“偶发 0 字节平地”。
  * 本示例在回调里生成正弦并 `tud_audio_write()`。

* `tud_audio_set_itf_cb()` / `tud_audio_set_itf_close_ep_cb()`

  * **解析 alt 用低字节**：`TU_U16_LOW(p_request->wValue)`；
  * 切 alt 时**清空 IN FIFO**（避免残留、快切时“EP 已激活”）。
  * 典型日志：`[ITF] set interface=1 alt=0/1/2`

---

## 构建参数与常见问答

### 端点尺寸和软件 FIFO

* 端点 `wMaxPacketSize` 需覆盖 **96 kHz / 位宽 / 通道**的最坏值；
* 软件 FIFO 建议 ≥ **10ms**（例如 `10 * max_packet_size`），避免系统抖动。

### 44.1 kHz 为啥总出坑？

* 因为“每毫秒 44.1 个样本”不是整数。
* 正确做法：**帧长流控 + 小数累加**，按 44/45 交替。
* 工程里已内置该算法；把 TinyUSB 升到最新并开 `CFG_TUD_AUDIO_EP_IN_FLOW_CONTROL=1` 就稳。

### 为什么改采样率会看到 alt 在 0 和 1/2 之间跳？

* 正常：主机**先关流（alt0）**，**再改时钟**，**再开流（alt1/alt2）**。

### 调系统麦克风音量没触发 `SET_CUR`？

* Windows 可能使用**软件增益**（对“输入设备”）。
* 在 Linux 上用 `alsamixer`/`amixer` 改音量，可能看到 `SET_CUR(VOLUME)` 的回调。

---

## 把示例换成“真实 ADC”

1. 决定谁做“时钟主”：

   * **主机控采样率**：Clock Source = **VAR\_CLK + RW**（本工程默认）。收到 `SET_CUR` 后 **重配 I2S/PLL**。
   * **ADC 固定采样率**：Clock Source = **FIX\_CLK + R**；`GET_RANGE` 只报 ADC 真能跑的频率，端点仍用 **Async IN**。

2. 建一个 **环形缓冲（≥6–10ms）**，用 DMA 把 ADC 样本写入；

3. 在 `tud_audio_tx_done_isr()` 里按上面“每毫秒样本数”从 ring 读出、`tud_audio_write()`；

4. 切 alt/变采样率时，**停 DMA→重配→清 ring/累加器→复位 DMA**。

---

## 构建与烧录（示例命令）

本项目使用VSCode的PicoSDK插件控制编译和烧录。

## 你应该能在日志里看到

* `New Sample Rate: 44100 Hz.`（或 96000）
* `[ITF] set interface=1 alt=1/2/0`（随主机切换）
* （在 Linux 上）`[CTL][SET] FU VOLUME ...`（更改音量时）

---

## 复盘与延伸

* 这个示例的价值在于：**把“描述符（静态）与回调（动态）”贯通**。
* 你可以很容易地换成多通道、增加 OUT 端点（扬声器）、做同步端点等；只要把 **AC/AS 描述符** 和 **GET/SET 回调** 一一对应好，主机就能正确识别。

---

## 最后再强调一次

* **把 TinyUSB 更新到最新版（master 或最新 release）**。
* **开启 `CFG_TUD_AUDIO_EP_IN_FLOW_CONTROL=1`**。
* 44.1 kHz 用“**小数累加**”法（本工程已实现）。
