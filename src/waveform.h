#ifndef WAVEFORM_H
#define WAVEFORM_H

#include <string>
#include <vector>

// ─── 波形参数 ───
struct WaveformParams {
    // 基波
    double fundamentalFreq;     // Hz
    double fundamentalVpp;      // V (峰峰值)
    double fundamentalPhase;    // 度
    int    cycles;              // 包含的基波周期数

    // 谐波1
    bool   harm1Enable;
    int    harm1Order;          // 阶次 (2,3,4,...)
    double harm1Vpp;           // V
    double harm1Phase;         // 度

    // 谐波2
    bool   harm2Enable;
    int    harm2Order;
    double harm2Vpp;
    double harm2Phase;

    // 干扰噪声
    bool   noiseEnable;
    double noiseFreq;           // Hz
    double noiseVpp;            // V (固定 0.2 = 200mV)

    // 直流偏置
    double dcOffset;            // V

    // 采样
    int    numPoints;           // 采样点数
};

// ─── 波形结果 ───
struct WaveformResult {
    std::string equation;           // ArbExpress 公式
    std::string rangeStr;           // range 字符串, 如 "20ms"
    std::vector<double> timeValues;  // 时间数组 (秒)
    std::vector<double> voltageValues; // 电压数组 (V)

    // 波形信息
    double fundamentalPeriod;   // 基波周期 (秒)
    double totalDuration;       // 总时长 (秒)
    double sampleInterval;      // 采样间隔 (秒)
    double signalVpp;           // 信号部分 (基波+谐波) Vpp
    double totalVpp;            // 合成波形 (含噪声) 总 Vpp
    double actualPeak;          // 实际峰值
    double vrms;                // 有效值 (V)

    // 推荐函数发生器设置
    double recommendedFreq;        // AFG 频率 (Hz)
    double recommendedSampleRate;   // AFG 采样率 (Hz)
    double recommendedVpp;          // AFG 峰峰值 (V)

    // 状态
    bool exceedsRange;          // 总 Vpp 超出 250mV?
    std::string warningMsg;     // 警告信息
};

// ─── 核心函数 ───

// 生成波形 (计算公式 + 采样数据 + 推荐值)
WaveformResult generateWaveform(const WaveformParams& params);

// 写 CSV 文件 (时间,电压 两列, 无 header)
bool writeCSV(const std::string& filepath, const WaveformResult& result);

// 写公式文件
bool writeEquationFile(const std::string& filepath, const WaveformResult& result);

// 格式化辅助
std::string formatFreq(double hz);
std::string formatPeriod(double seconds);
std::string formatNum(double n);

#endif // WAVEFORM_H
