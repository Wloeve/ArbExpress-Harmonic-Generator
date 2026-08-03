# ArbExpress 谐波波形生成器

> ArbExpress Harmonic Waveform Generator by.Wloeve

一个用于生成谐波信号波形的 Windows 桌面工具，生成可直接导入 Tektronix ArbExpress 波形编辑器的 CSV 文件，并给出函数发生器与示波器的推荐设置。

![界面预览](CialloSystem_ba-style@nulla.top.png)

## 功能特性

- **谐波合成**：基波 + 1~2 个谐波（阶次 1~50 可选）叠加
- **干扰噪声**：可选叠加 200mV 固定 Vpp 的正弦噪声，频率自定义
- **参数范围**：信号总 Vpp 限定在 50~250mV 之间
- **CSV 输出**：生成无表头、两列（时间 / 电压）的标准 CSV，兼容 ArbExpress 导入格式
- **AFG1000 推荐设置**：自动给出函数发生器的频率、Vpp、偏置推荐值
- **示波器理论值**：显示时域（频率、Vpp、Vrms）和 FFT 频谱（基波及各次谐波的频率、幅值）
- **DPI 自适应**：适配 2K 27 英寸与 2.5K 16 英寸屏幕
- **资源内嵌**：Logo 与图标嵌入 EXE，移动位置不影响显示
- **静态链接**：单文件 EXE，无需额外运行时依赖

## 使用方法

### 1. 运行程序

双击 `ArbExpressGenerator.exe` 启动（无需安装）。

### 2. 设置波形参数

- **基波**：设置频率（Hz/kHz/MHz）、Vpp（50~250mV）、相位（°）、周期数
- **谐波 1/2**（可选）：勾选启用，选择阶次（1~50），设置 Vpp 与相位
- **干扰噪声**（可选）：勾选启用，设置噪声频率（Vpp 固定 200mV）
- **其他设置**：直流偏置（mV）、采样点数（默认 8192）

### 3. 生成波形

点击「生成波形」按钮：
- CSV 文件生成在 **桌面**：`harmonic_waveform.csv`
- 程序界面显示推荐设置、示波器理论值、执行状态

### 4. 导入 ArbExpress

1. 打开 ArbExpress 波形编辑器
2. 导入桌面上的 `harmonic_waveform.csv`
3. 按界面显示的推荐设置配置 AFG1000 函数发生器

### 5. 打开文件位置

点击「打开文件位置」直接定位到桌面上的 CSV 文件。

## 采样点数说明

采样点数影响波形精度，尤其在高频噪声场景下：

| 基波频率 | 噪声频率 | 推荐采样点数 | 说明 |
|---------|---------|------------|------|
| 1 MHz   | 1 MHz   | 8192       | 默认值足够 |
| 1 kHz   | 1 MHz   | ≥ 65536    | 需增大采样点数避免波形失真 |
| 1 Hz    | 1 MHz   | ≥ 65536    | 需增大采样点数避免波形失真 |

**原则**：每个噪声周期建议至少 20 个采样点，采样点数 = 20 × 噪声频率 × 总时长。

## 技术栈

- **语言**：C++（Win32 API）
- **图形**：GDI+（Logo、图标加载）
- **编译**：MinGW-w64（g++ -O2 -static -mwindows -municode）
- **资源**：windres 嵌入图标、PNG、JPG 资源

## 项目结构

```
ArbExpress-Harmonic-Generator/
├── src/
│   ├── main.cpp              # GUI 主程序
│   ├── waveform.cpp          # 波形计算核心
│   ├── waveform.h            # 数据结构定义
│   ├── app.rc                # 资源文件（图标/图片嵌入）
│   └── app.manifest          # DPI 感知清单
├── build.bat                 # 编译脚本
├── ArbExpressGenerator.exe   # 可执行文件
├── CialloSystem_*.png        # Logo 图片（嵌入资源）
├── ciallo.jpg                # 图标源图（嵌入资源）
├── ciallo.ico                # 图标文件（嵌入资源）
└── AFG1000-*.pdf             # AFG1000 仪器手册
```

## 编译方法

依赖 MinGW-w64（含 g++、windres），执行：

```bat
build.bat
```

输出 `ArbExpressGenerator.exe`。

## 验证方法

1. 运行 EXE，设置基波 1kHz / Vpp 100mV，勾选谐波 1（3 次阶，50mV）
2. 点击「生成波形」，输出结果应显示 `done`
3. 桌面应生成 `harmonic_waveform.csv`
4. 导入 ArbExpress 应显示清晰的谐波叠加波形

## 作者

**Wloeve**

---

本程序用于生成可导入 ArbExpress 的谐波波形数据，配合 Tektronix AFG1000 系列函数发生器使用。
