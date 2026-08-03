#include "waveform.h"
#include <cmath>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ─── 格式化辅助 ────────────────────────────────

std::string formatNum(double n) {
    if (n == 0) return "0";
    double absN = std::abs(n);
    if (absN >= 1.0 && n == (int)n) return std::to_string((long long)n);
    std::ostringstream ss;
    ss << std::setprecision(6) << std::noshowpoint << n;
    // 移除尾随零
    std::string s = ss.str();
    if (s.find('.') != std::string::npos) {
        size_t lastNonZero = s.find_last_not_of('0');
        if (s[lastNonZero] == '.') lastNonZero--;
        s = s.substr(0, lastNonZero + 1);
    }
    return s;
}

std::string formatPeriod(double seconds) {
    double absS = std::abs(seconds);
    std::ostringstream ss;
    ss << std::setprecision(6);
    if (absS >= 1.0)      ss << seconds << "s";
    else if (absS >= 1e-3) ss << (seconds * 1e3) << "ms";
    else if (absS >= 1e-6) ss << (seconds * 1e6) << "us";
    else if (absS >= 1e-9) ss << (seconds * 1e9) << "ns";
    else                   ss << (seconds * 1e12) << "ps";
    std::string s = ss.str();
    // 移除尾随零
    if (s.find('.') != std::string::npos) {
        size_t lastNonZero = s.find_last_not_of('0');
        if (s[lastNonZero] == '.') lastNonZero--;
        s = s.substr(0, lastNonZero + 1);
    }
    return s;
}

std::string formatFreq(double hz) {
    std::ostringstream ss;
    ss << std::setprecision(6);
    if (hz >= 1e6)      ss << (hz / 1e6) << " MHz";
    else if (hz >= 1e3) ss << (hz / 1e3) << " kHz";
    else                ss << hz << " Hz";
    std::string s = ss.str();
    if (s.find('.') != std::string::npos) {
        size_t lastNonZero = s.find_last_not_of('0');
        if (s[lastNonZero] == '.') lastNonZero--;
        s = s.substr(0, lastNonZero + 1);
    }
    return s;
}

// ─── 公式构建 ────────────────────────────────

static std::string buildPhaseStr(double deg) {
    if (std::abs(deg) < 1e-12) return "";
    while (deg > 180) deg -= 360;
    while (deg <= -180) deg += 360;
    if (std::abs(deg) < 1e-12) return "";
    if (std::abs(std::abs(deg) - 180) < 1e-12)
        return (deg < 0) ? "-pi" : "+pi";
    std::string sign = (deg > 0) ? "+" : "-";
    return sign + formatNum(std::abs(deg)) + "*pi/180";
}

static std::string buildEquation(const WaveformParams& p) {
    std::vector<std::string> terms;

    // 基波: Sin(cycles*w + phase)
    {
        std::string arg = (p.cycles != 1) ?
            (std::to_string(p.cycles) + "*w") : "w";
        std::string ph = buildPhaseStr(p.fundamentalPhase);
        terms.push_back("Sin(" + arg + ph + ")");
    }

    // 谐波1
    if (p.harm1Enable && p.harm1Vpp > 0) {
        double relAmp = p.harm1Vpp / p.fundamentalVpp;
        int coef = p.harm1Order * p.cycles;
        std::string arg = (coef != 1) ? (std::to_string(coef) + "*w") : "w";
        std::string ph = buildPhaseStr(p.harm1Phase);
        terms.push_back(formatNum(relAmp) + "*Sin(" + arg + ph + ")");
    }

    // 谐波2
    if (p.harm2Enable && p.harm2Vpp > 0) {
        double relAmp = p.harm2Vpp / p.fundamentalVpp;
        int coef = p.harm2Order * p.cycles;
        std::string arg = (coef != 1) ? (std::to_string(coef) + "*w") : "w";
        std::string ph = buildPhaseStr(p.harm2Phase);
        terms.push_back(formatNum(relAmp) + "*Sin(" + arg + ph + ")");
    }

    // 噪声: Sin(noiseCoef*w)
    if (p.noiseEnable && p.noiseVpp > 0) {
        double relAmp = p.noiseVpp / p.fundamentalVpp;
        double period = 1.0 / p.fundamentalFreq;
        double noiseCoef = p.noiseFreq * period * p.cycles;
        std::string arg = (std::abs(noiseCoef - 1.0) > 1e-12) ?
            (formatNum(noiseCoef) + "*w") : "w";
        terms.push_back(formatNum(relAmp) + "*Sin(" + arg + ")");
    }

    // 合并
    std::string eq;
    for (size_t i = 0; i < terms.size(); i++) {
        if (i > 0) eq += "+";
        eq += terms[i];
    }

    // 直流偏置
    if (std::abs(p.dcOffset) > 1e-12) {
        double fundPeak = p.fundamentalVpp / 2.0;
        double dcRel = p.dcOffset / fundPeak;
        std::string sign = (dcRel > 0) ? "+" : "-";
        eq = sign + formatNum(std::abs(dcRel)) +
             (eq.empty() ? "" : "+" + eq);
        if (!eq.empty() && eq[0] == '+') eq = eq.substr(1);
    }

    return eq;
}

// ─── 核心计算 ────────────────────────────────

WaveformResult generateWaveform(const WaveformParams& p) {
    WaveformResult r;

    // 基本参数
    r.fundamentalPeriod = 1.0 / p.fundamentalFreq;
    r.totalDuration = r.fundamentalPeriod * p.cycles;
    r.sampleInterval = r.totalDuration / p.numPoints;
    r.rangeStr = formatPeriod(r.totalDuration);

    // 构建公式
    r.equation = buildEquation(p);

    // 计算采样数据
    double fundPeak = p.fundamentalVpp / 2.0;
    double fundPhaseRad = p.fundamentalPhase * M_PI / 180.0;
    double h1PhaseRad = p.harm1Phase * M_PI / 180.0;
    double h2PhaseRad = p.harm2Phase * M_PI / 180.0;

    // 噪声系数
    double noiseCoef = 0;
    double noiseRelAmp = 0;
    if (p.noiseEnable && p.noiseVpp > 0) {
        noiseCoef = p.noiseFreq * r.fundamentalPeriod * p.cycles;
        noiseRelAmp = p.noiseVpp / p.fundamentalVpp;
    }

    r.timeValues.resize(p.numPoints);
    r.voltageValues.resize(p.numPoints);

    double vmin = 1e30, vmax = -1e30;

    for (int i = 0; i < p.numPoints; i++) {
        // w 对应 ArbExpress 中的 w (0 → 2π)
        double w = 2.0 * M_PI * i / p.numPoints;
        double val = 0.0;

        // 基波: Sin(cycles*w + phase)
        val += std::sin(p.cycles * w + fundPhaseRad);

        // 谐波1: (h1Vpp/fundVpp) * Sin(order*cycles*w + phase)
        if (p.harm1Enable && p.harm1Vpp > 0) {
            double relAmp = p.harm1Vpp / p.fundamentalVpp;
            val += relAmp * std::sin(p.harm1Order * p.cycles * w + h1PhaseRad);
        }

        // 谐波2
        if (p.harm2Enable && p.harm2Vpp > 0) {
            double relAmp = p.harm2Vpp / p.fundamentalVpp;
            val += relAmp * std::sin(p.harm2Order * p.cycles * w + h2PhaseRad);
        }

        // 噪声: noiseRelAmp * Sin(noiseCoef*w)
        if (p.noiseEnable && p.noiseVpp > 0) {
            val += noiseRelAmp * std::sin(noiseCoef * w);
        }

        // 直流偏置 (归一化)
        if (std::abs(p.dcOffset) > 1e-12) {
            val += p.dcOffset / fundPeak;
        }

        // 转换为实际电压
        val *= fundPeak;

        r.timeValues[i] = i * r.sampleInterval;
        r.voltageValues[i] = val;

        if (val < vmin) vmin = val;
        if (val > vmax) vmax = val;
    }

    r.actualPeak = std::max(std::abs(vmin), std::abs(vmax));
    r.totalVpp = vmax - vmin;

    // 计算 Vrms (有效值)
    double sumSq = 0.0;
    for (double v : r.voltageValues) sumSq += v * v;
    r.vrms = std::sqrt(sumSq / r.voltageValues.size());

    // 信号部分 Vpp (不含噪声, 理论估算)
    r.signalVpp = p.fundamentalVpp;
    if (p.harm1Enable && p.harm1Vpp > 0) r.signalVpp += p.harm1Vpp;
    if (p.harm2Enable && p.harm2Vpp > 0) r.signalVpp += p.harm2Vpp;

    // 推荐函数发生器设置
    r.recommendedFreq = p.fundamentalFreq / p.cycles;
    r.recommendedSampleRate = (double)p.numPoints / r.totalDuration;
    r.recommendedVpp = r.totalVpp;

    // 超出范围检查
    r.exceedsRange = (r.totalVpp * 1000.0 > 250.0);
    if (r.exceedsRange) {
        std::ostringstream ss;
        ss << "总 Vpp = " << (r.totalVpp * 1000.0)
           << "mV, 超过 250mV! 建议减小信号幅度。";
        r.warningMsg = ss.str();
    }

    return r;
}

// ─── 文件输出 ────────────────────────────────

bool writeCSV(const std::string& filepath, const WaveformResult& r) {
    std::ofstream f(filepath);
    if (!f.is_open()) return false;
    f << std::scientific << std::setprecision(9);
    for (size_t i = 0; i < r.timeValues.size(); i++) {
        f << r.timeValues[i] << "," << std::fixed
          << std::setprecision(6) << r.voltageValues[i] << "\n";
        f << std::scientific << std::setprecision(9);
    }
    return true;
}

bool writeEquationFile(const std::string& filepath, const WaveformResult& r) {
    std::ofstream f(filepath);
    if (!f.is_open()) return false;
    f << "#Change the range according to your settings\n"
      << "range(0," << r.rangeStr << ")\n"
      << "#Your equation goes here\n"
      << r.equation << "\n";
    return true;
}
