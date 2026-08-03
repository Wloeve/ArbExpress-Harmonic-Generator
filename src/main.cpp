#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <string>
#include <sstream>
#include <iomanip>
#include <vector>
#include <ctime>
#include "waveform.h"
#include <gdiplus.h>

// ─── GDI+ 初始化辅助 ───────────────────────
class GdiplusInit {
    Gdiplus::GdiplusStartupInput input;
    ULONG_PTR token;
public:
    GdiplusInit() { Gdiplus::GdiplusStartup(&token, &input, NULL); }
    ~GdiplusInit() { Gdiplus::GdiplusShutdown(token); }
};
static GdiplusInit g_gdiplusInit;  // 全局实例, 进程生命周期内保持

// ─── 从资源加载 GDI+ Image ──────────────────
// resourceType: 资源类型字符串 (如 L"PNG", L"JPG")
// resourceId: 资源 ID (如 100, 101)
static Gdiplus::Image* LoadImageFromResource(HMODULE hModule,
    const wchar_t* resourceType, int resourceId) {
    HRSRC hRes = FindResourceW(hModule, MAKEINTRESOURCEW(resourceId), resourceType);
    if (!hRes) return NULL;
    DWORD dataSize = SizeofResource(hModule, hRes);
    HGLOBAL hData = LoadResource(hModule, hRes);
    if (!hData) return NULL;
    const void* pData = LockResource(hData);
    if (!pData || dataSize == 0) return NULL;

    // 分配全局内存并复制资源数据
    HGLOBAL hCopy = GlobalAlloc(GMEM_MOVEABLE, dataSize);
    if (!hCopy) return NULL;
    void* pCopy = GlobalLock(hCopy);
    if (!pCopy) { GlobalFree(hCopy); return NULL; }
    memcpy(pCopy, pData, dataSize);
    GlobalUnlock(hCopy);

    // 创建 IStream
    IStream* pStream = NULL;
    if (CreateStreamOnHGlobal(hCopy, TRUE, &pStream) != S_OK) {
        GlobalFree(hCopy);
        return NULL;
    }

    // GDI+ 从 IStream 创建 Image
    Gdiplus::Image* pImage = Gdiplus::Image::FromStream(pStream, FALSE);
    pStream->Release();
    if (!pImage || pImage->GetLastStatus() != Gdiplus::Ok) {
        delete pImage;
        return NULL;
    }
    return pImage;
}

// ─── 控件 ID ───────────────────────────────
enum {
    IDC_FREQ_EDIT   = 1001,
    IDC_FREQ_UNIT   = 1002,
    IDC_FVPP_EDIT   = 1003,
    IDC_FPHASE_EDIT = 1004,
    IDC_CYCLES_EDIT = 1005,
    IDC_RANGE_INFO  = 1006,

    IDC_H1_CHECK = 1010, IDC_H1_ORDER = 1011,
    IDC_H1_VPP = 1012,    IDC_H1_PHASE = 1013,

    IDC_H2_CHECK = 1020, IDC_H2_ORDER = 1021,
    IDC_H2_VPP = 1022,    IDC_H2_PHASE = 1023,

    IDC_NOISE_CHECK = 1030, IDC_NOISE_FREQ = 1031,
    IDC_NOISE_UNIT  = 1032,

    IDC_DC_EDIT     = 1040,
    IDC_POINTS_EDIT = 1041,

    IDC_GENERATE_BTN = 1050,
    IDC_COPY_BTN     = 1051,
    IDC_OPEN_BTN     = 1052,

    IDC_RECOMMEND_EDIT = 1060,
    IDC_SCOPE_EDIT     = 1061,
    IDC_OUTPUT_EDIT    = 1062,
};

// ─── 全局变量 ───────────────────────────────
static HFONT  g_hFont    = NULL;
static HFONT  g_hBoldFont = NULL;
static HFONT  g_hMonoFont = NULL;
static HWND   g_hMainWnd  = NULL;
static std::string g_lastEquation;
static std::string g_exeDir;
static int   g_dpi = 96;
static double g_scale = 1.0;  // 缩放因子 (基于 DPI)
static Gdiplus::Image* g_pLogo = NULL;  // logo 图片
static HBRUSH g_hWhiteBrush = NULL;      // 白色画刷 (用于控件背景)
static int g_yOffset = 0;                // logo 占用的纵向偏移 (设计像素)
static int g_logoDisplayW = 0;           // logo 显示宽度 (设计像素)
static int g_logoDisplayH = 0;           // logo 显示高度 (设计像素)
static HICON g_hAppIcon = NULL;          // 应用图标 (从 logo 生成)

// 缩放辅助: 将设计像素值转换为实际像素值
static int SC(int v) { return (int)(v * g_scale); }

// ─── 辅助函数 ───────────────────────────────

static std::wstring s2ws(const std::string& s) {
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, NULL, 0);
    std::wstring ws(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &ws[0], len);
    if (len > 0) ws.resize(len - 1);
    return ws;
}

static std::string ws2s(const std::wstring& ws) {
    int len = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, NULL, 0, NULL, NULL);
    std::string s(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, &s[0], len, NULL, NULL);
    if (len > 0) s.resize(len - 1);
    return s;
}

static double GetEditDouble(HWND hwnd, int id) {
    wchar_t buf[256];
    GetDlgItemTextW(hwnd, id, buf, 256);
    return _wtof(buf);
}

static int GetEditInt(HWND hwnd, int id) {
    wchar_t buf[256];
    GetDlgItemTextW(hwnd, id, buf, 256);
    return _wtoi(buf);
}

static int GetComboSel(HWND hwnd, int id) {
    return (int)SendDlgItemMessageW(hwnd, id, CB_GETCURSEL, 0, 0);
}

static double UnitMultiplier(int sel) {
    switch (sel) {
        case 1:  return 1e3;
        case 2:  return 1e6;
        default: return 1.0;
    }
}

// 创建子控件辅助函数
static HWND Crt(const wchar_t* type, HWND parent, const wchar_t* text,
                int x, int y, int w, int h, int id,
                DWORD style = 0, DWORD exStyle = 0, HFONT font = NULL) {
    // EDIT 控件添加凹陷边框 (文本框样式)
    if (wcscmp(type, L"EDIT") == 0) {
        exStyle |= WS_EX_CLIENTEDGE;
    }
    HWND hCtrl = CreateWindowExW(exStyle, type, text,
        WS_CHILD | WS_VISIBLE | style,
        SC(x), SC(y + g_yOffset), SC(w), SC(h),
        parent, (HMENU)(INT_PTR)id, NULL, NULL);
    HFONT f = font ? font : g_hFont;
    if (f) SendMessageW(hCtrl, WM_SETFONT, (WPARAM)f, TRUE);
    return hCtrl;
}

// ─── 创建所有控件 ───────────────────────────
static void CreateControls(HWND hwnd) {
    // 使用更大的控件尺寸 (基于 96DPI 设计, 缩放后在高分屏正常)
    int cy = 26;   // 输入框高度
    int sy = 22;   // 静态文字高度

    // ── 标题 ──
    Crt(L"STATIC", hwnd, L"ArbExpress 谐波波形生成器",
        130, 8, 280, 24, 0, SS_CENTER, 0, g_hBoldFont);

    // ── 基波 ──
    Crt(L"BUTTON", hwnd, L"基波",
        8, 36, 540, 105, 0, BS_GROUPBOX);
    Crt(L"STATIC", hwnd, L"频率:", 20, 62, 35, sy, 0);
    Crt(L"EDIT", hwnd, L"1", 58, 58, 60, cy, IDC_FREQ_EDIT,
        ES_AUTOHSCROLL | ES_NUMBER);
    Crt(L"COMBOBOX", hwnd, L"", 122, 58, 60, 300, IDC_FREQ_UNIT,
        CBS_DROPDOWNLIST | WS_VSCROLL);
    Crt(L"STATIC", hwnd, L"Vpp:", 190, 62, 30, sy, 0);
    Crt(L"EDIT", hwnd, L"100", 222, 58, 45, cy, IDC_FVPP_EDIT, ES_AUTOHSCROLL);
    Crt(L"STATIC", hwnd, L"mV", 271, 62, 22, sy, 0);
    Crt(L"STATIC", hwnd, L"相位:", 300, 62, 32, sy, 0);
    Crt(L"EDIT", hwnd, L"0", 334, 58, 32, cy, IDC_FPHASE_EDIT, ES_AUTOHSCROLL);
    Crt(L"STATIC", hwnd, L"°", 370, 62, 14, sy, 0);
    Crt(L"STATIC", hwnd, L"周期数:", 20, 92, 42, sy, 0);
    Crt(L"EDIT", hwnd, L"1", 64, 88, 32, cy, IDC_CYCLES_EDIT,
        ES_AUTOHSCROLL | ES_NUMBER);
    Crt(L"STATIC", hwnd, L"", 105, 92, 280, sy, IDC_RANGE_INFO);

    // ── 谐波1 ──
    Crt(L"BUTTON", hwnd, L"谐波 1", 8, 148, 540, 62, 0, BS_GROUPBOX);
    Crt(L"BUTTON", hwnd, L"", 20, 170, 18, 22, IDC_H1_CHECK, BS_AUTOCHECKBOX);
    Crt(L"STATIC", hwnd, L"阶次:", 44, 174, 32, sy, 0);
    Crt(L"COMBOBOX", hwnd, L"", 78, 170, 55, 400, IDC_H1_ORDER,
        CBS_DROPDOWNLIST | WS_VSCROLL);
    Crt(L"STATIC", hwnd, L"Vpp:", 140, 174, 28, sy, 0);
    Crt(L"EDIT", hwnd, L"50", 170, 170, 45, cy, IDC_H1_VPP, ES_AUTOHSCROLL);
    Crt(L"STATIC", hwnd, L"mV", 220, 174, 22, sy, 0);
    Crt(L"STATIC", hwnd, L"相位:", 250, 174, 32, sy, 0);
    Crt(L"EDIT", hwnd, L"0", 284, 170, 32, cy, IDC_H1_PHASE, ES_AUTOHSCROLL);
    Crt(L"STATIC", hwnd, L"°", 320, 174, 14, sy, 0);

    // ── 谐波2 ──
    Crt(L"BUTTON", hwnd, L"谐波 2", 8, 217, 540, 62, 0, BS_GROUPBOX);
    Crt(L"BUTTON", hwnd, L"", 20, 239, 18, 22, IDC_H2_CHECK, BS_AUTOCHECKBOX);
    Crt(L"STATIC", hwnd, L"阶次:", 44, 243, 32, sy, 0);
    Crt(L"COMBOBOX", hwnd, L"", 78, 239, 55, 400, IDC_H2_ORDER,
        CBS_DROPDOWNLIST | WS_VSCROLL);
    Crt(L"STATIC", hwnd, L"Vpp:", 140, 243, 28, sy, 0);
    Crt(L"EDIT", hwnd, L"50", 170, 239, 45, cy, IDC_H2_VPP, ES_AUTOHSCROLL);
    Crt(L"STATIC", hwnd, L"mV", 220, 243, 22, sy, 0);
    Crt(L"STATIC", hwnd, L"相位:", 250, 243, 32, sy, 0);
    Crt(L"EDIT", hwnd, L"0", 284, 239, 32, cy, IDC_H2_PHASE, ES_AUTOHSCROLL);
    Crt(L"STATIC", hwnd, L"°", 320, 243, 14, sy, 0);

    // ── 噪声 ──
    Crt(L"BUTTON", hwnd, L"干扰噪声 (Vpp 固定 200mV)",
        8, 286, 540, 62, 0, BS_GROUPBOX);
    Crt(L"BUTTON", hwnd, L"", 20, 308, 18, 22, IDC_NOISE_CHECK, BS_AUTOCHECKBOX);
    Crt(L"STATIC", hwnd, L"频率:", 44, 312, 32, sy, 0);
    Crt(L"EDIT", hwnd, L"1", 78, 308, 60, cy, IDC_NOISE_FREQ,
        ES_AUTOHSCROLL | ES_NUMBER);
    Crt(L"COMBOBOX", hwnd, L"", 142, 308, 60, 300, IDC_NOISE_UNIT,
        CBS_DROPDOWNLIST | WS_VSCROLL);
    Crt(L"STATIC", hwnd, L"Vpp: 200mV (固定)", 210, 312, 140, sy, 0);

    // ── 其他 ──
    Crt(L"BUTTON", hwnd, L"其他设置", 8, 355, 540, 62, 0, BS_GROUPBOX);
    Crt(L"STATIC", hwnd, L"直流偏置:", 20, 380, 55, sy, 0);
    Crt(L"EDIT", hwnd, L"0", 78, 376, 50, cy, IDC_DC_EDIT, ES_AUTOHSCROLL);
    Crt(L"STATIC", hwnd, L"mV", 132, 380, 22, sy, 0);
    Crt(L"STATIC", hwnd, L"采样点数:", 165, 380, 55, sy, 0);
    Crt(L"EDIT", hwnd, L"8192", 223, 376, 60, cy, IDC_POINTS_EDIT,
        ES_AUTOHSCROLL | ES_NUMBER);

    // ── 生成按钮 + 打开文件位置 ──
    Crt(L"BUTTON", hwnd, L"生成波形",
        130, 428, 140, 38, IDC_GENERATE_BTN, BS_PUSHBUTTON, 0, g_hBoldFont);
    Crt(L"BUTTON", hwnd, L"打开文件位置",
        280, 428, 140, 38, IDC_OPEN_BTN, BS_PUSHBUTTON, 0, g_hBoldFont);

    // ── AFG1000 仪器推荐设置 (1份) ──
    Crt(L"BUTTON", hwnd, L"★ AFG1000 仪器推荐设置 ★",
        8, 478, 540, 100, 0, BS_GROUPBOX, 0, g_hBoldFont);
    Crt(L"EDIT", hwnd, L"点击「生成波形」后此处显示推荐设置",
        16, 505, 524, 72, IDC_RECOMMEND_EDIT,
        ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
        0, g_hBoldFont);

    // ── 示波器显示理论值 (2份) ──
    Crt(L"BUTTON", hwnd, L"★ 示波器显示理论值 ★",
        8, 588, 540, 200, 0, BS_GROUPBOX, 0, g_hBoldFont);
    Crt(L"EDIT", hwnd, L"点击「生成波形」后此处显示示波器理论值",
        16, 615, 524, 160, IDC_SCOPE_EDIT,
        ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
        0, g_hBoldFont);

    // ── 输出结果 (与推荐设置相同) ──
    Crt(L"BUTTON", hwnd, L"输出结果", 8, 798, 540, 100, 0, BS_GROUPBOX);
    Crt(L"EDIT", hwnd, L"",
        16, 825, 524, 72, IDC_OUTPUT_EDIT,
        ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
        0, g_hMonoFont);

    // ── 初始化下拉框 ──
    const wchar_t* units[] = { L"Hz", L"kHz", L"MHz" };
    for (int i = 0; i < 3; i++) {
        SendDlgItemMessageW(hwnd, IDC_FREQ_UNIT, CB_ADDSTRING, 0, (LPARAM)units[i]);
        SendDlgItemMessageW(hwnd, IDC_NOISE_UNIT, CB_ADDSTRING, 0, (LPARAM)units[i]);
    }
    SendDlgItemMessageW(hwnd, IDC_FREQ_UNIT, CB_SETCURSEL, 1, 0);   // kHz
    SendDlgItemMessageW(hwnd, IDC_NOISE_UNIT, CB_SETCURSEL, 2, 0); // MHz

    // 阶次下拉框 1-50
    wchar_t buf[16];
    for (int i = 1; i <= 50; i++) {
        swprintf(buf, 16, L"%d", i);
        SendDlgItemMessageW(hwnd, IDC_H1_ORDER, CB_ADDSTRING, 0, (LPARAM)buf);
        SendDlgItemMessageW(hwnd, IDC_H2_ORDER, CB_ADDSTRING, 0, (LPARAM)buf);
    }
    // 默认: 谐波1=3, 谐波2=5
    SendDlgItemMessageW(hwnd, IDC_H1_ORDER, CB_SETCURSEL, 2, 0);  // 3
    SendDlgItemMessageW(hwnd, IDC_H2_ORDER, CB_SETCURSEL, 4, 0);  // 5

    // ── 默认勾选 ──
    CheckDlgButton(hwnd, IDC_H1_CHECK, BST_CHECKED);
    // 干扰噪声默认关闭
    CheckDlgButton(hwnd, IDC_NOISE_CHECK, BST_UNCHECKED);

    // ── 显式设置默认值 ──
    SetDlgItemTextW(hwnd, IDC_FREQ_EDIT,    L"1");
    SetDlgItemTextW(hwnd, IDC_FVPP_EDIT,    L"100");
    SetDlgItemTextW(hwnd, IDC_FPHASE_EDIT,  L"0");
    SetDlgItemTextW(hwnd, IDC_CYCLES_EDIT,  L"1");
    SetDlgItemTextW(hwnd, IDC_H1_VPP,       L"50");
    SetDlgItemTextW(hwnd, IDC_H1_PHASE,     L"0");
    SetDlgItemTextW(hwnd, IDC_H2_VPP,       L"50");
    SetDlgItemTextW(hwnd, IDC_H2_PHASE,     L"0");
    SetDlgItemTextW(hwnd, IDC_NOISE_FREQ,   L"1");
    SetDlgItemTextW(hwnd, IDC_DC_EDIT,     L"0");
    SetDlgItemTextW(hwnd, IDC_POINTS_EDIT, L"8192");
}

// ─── 更新 range 信息 ────────────────────────
static void UpdateRangeInfo(HWND hwnd) {
    double freq = GetEditDouble(hwnd, IDC_FREQ_EDIT) *
                  UnitMultiplier(GetComboSel(hwnd, IDC_FREQ_UNIT));
    int cycles = GetEditInt(hwnd, IDC_CYCLES_EDIT);
    if (freq <= 0 || cycles <= 0) return;
    double period = 1.0 / freq * cycles;
    std::string rangeStr = formatPeriod(period);
    std::wstring msg = L"range: (0, " + s2ws(rangeStr) + L")";
    SetDlgItemTextW(hwnd, IDC_RANGE_INFO, msg.c_str());
}

// ─── 生成波形 ───────────────────────────────
static void OnGenerate(HWND hwnd) {
    // 读取参数
    double freq = GetEditDouble(hwnd, IDC_FREQ_EDIT) *
                  UnitMultiplier(GetComboSel(hwnd, IDC_FREQ_UNIT));
    double fundVpp = GetEditDouble(hwnd, IDC_FVPP_EDIT) / 1000.0; // mV → V
    double fundPhase = GetEditDouble(hwnd, IDC_FPHASE_EDIT);
    int cycles = GetEditInt(hwnd, IDC_CYCLES_EDIT);

    bool h1En = IsDlgButtonChecked(hwnd, IDC_H1_CHECK) == BST_CHECKED;
    int h1Order = GetComboSel(hwnd, IDC_H1_ORDER) + 1;  // 0-indexed → 1-50
    double h1Vpp = GetEditDouble(hwnd, IDC_H1_VPP) / 1000.0;
    double h1Phase = GetEditDouble(hwnd, IDC_H1_PHASE);

    bool h2En = IsDlgButtonChecked(hwnd, IDC_H2_CHECK) == BST_CHECKED;
    int h2Order = GetComboSel(hwnd, IDC_H2_ORDER) + 1;
    double h2Vpp = GetEditDouble(hwnd, IDC_H2_VPP) / 1000.0;
    double h2Phase = GetEditDouble(hwnd, IDC_H2_PHASE);

    bool noiseEn = IsDlgButtonChecked(hwnd, IDC_NOISE_CHECK) == BST_CHECKED;
    double noiseFreq = GetEditDouble(hwnd, IDC_NOISE_FREQ) *
                        UnitMultiplier(GetComboSel(hwnd, IDC_NOISE_UNIT));

    double dcOffset = GetEditDouble(hwnd, IDC_DC_EDIT) / 1000.0;
    int numPoints = GetEditInt(hwnd, IDC_POINTS_EDIT);

    // 验证
    if (freq <= 0) {
        MessageBoxW(hwnd, L"基波频率必须大于 0!", L"错误", MB_OK | MB_ICONERROR);
        return;
    }
    if (fundVpp < 0.05 || fundVpp > 0.25) {
        MessageBoxW(hwnd, L"基波 Vpp 必须在 50-250mV 范围内!",
                    L"错误", MB_OK | MB_ICONERROR);
        return;
    }
    // 检查信号总 Vpp (基波+谐波)
    double signalVpp = fundVpp;
    if (h1En && h1Vpp > 0) signalVpp += h1Vpp;
    if (h2En && h2Vpp > 0) signalVpp += h2Vpp;
    if (signalVpp < 0.05 || signalVpp > 0.25) {
        wchar_t msg[256];
        swprintf(msg, 256,
            L"信号总 Vpp = %.0fmV, 超出 50-250mV 范围!\n请调整谐波幅度。",
            signalVpp * 1000);
        MessageBoxW(hwnd, msg, L"警告", MB_OK | MB_ICONWARNING);
    }
    if (cycles <= 0) {
        MessageBoxW(hwnd, L"周期数必须大于 0!", L"错误", MB_OK | MB_ICONERROR);
        return;
    }
    if (numPoints < 2) {
        MessageBoxW(hwnd, L"采样点数必须 >= 2!", L"错误", MB_OK | MB_ICONERROR);
        return;
    }

    // 填充参数
    WaveformParams p = {};
    p.fundamentalFreq  = freq;
    p.fundamentalVpp   = fundVpp;
    p.fundamentalPhase = fundPhase;
    p.cycles           = cycles;

    p.harm1Enable = h1En;
    p.harm1Order  = h1Order;
    p.harm1Vpp    = h1Vpp;
    p.harm1Phase  = h1Phase;

    p.harm2Enable = h2En;
    p.harm2Order  = h2Order;
    p.harm2Vpp    = h2Vpp;
    p.harm2Phase  = h2Phase;

    p.noiseEnable = noiseEn;
    p.noiseFreq   = noiseFreq;
    p.noiseVpp    = 0.2;  // 200mV 固定

    p.dcOffset   = dcOffset;
    p.numPoints  = numPoints;

    // 计算
    WaveformResult r = generateWaveform(p);
    g_lastEquation = "#Change the range according to your settings\n"
                     "range(0," + r.rangeStr + ")\n"
                     "#Your equation goes here\n" + r.equation;

    // ── 噪声频率整数倍检查 (影响 FFT 精度) ──
    bool noiseLeak = false;
    if (noiseEn && freq > 0) {
        double ratio = noiseFreq / freq;
        double nearest = std::round(ratio);
        if (std::abs(ratio - nearest) > 1e-6) noiseLeak = true;
    }

    // ══════════════════════════════════════════
    //  ★ AFG1000 仪器推荐设置 ★
    // ══════════════════════════════════════════
    std::ostringstream osRec;
    osRec << std::fixed << std::setprecision(3);
    osRec << "频率 (Frequency): " << formatFreq(r.recommendedFreq) << "\r\n";
    osRec << "Vpp:              " << (r.recommendedVpp * 1000) << "mV\r\n";
    osRec << "偏置 (Offset):    " << (dcOffset * 1000) << "mV";
    SetDlgItemTextW(hwnd, IDC_RECOMMEND_EDIT, s2ws(osRec.str()).c_str());

    // ══════════════════════════════════════════
    //  ★ 示波器显示理论值 (9行) ★
    // ══════════════════════════════════════════
    std::ostringstream osScope;
    osScope << std::fixed << std::setprecision(3);
    osScope << "【时域显示】\r\n";
    osScope << "  频率: " << formatFreq(freq) << "\r\n";
    osScope << "  Vpp:  " << (r.totalVpp * 1000) << "mV\r\n";
    osScope << "  Vrms: " << (r.vrms * 1000) << "mV\r\n";
    osScope << "【FFT 频谱】\r\n";
    osScope << "  基波  : " << formatFreq(freq) << "  幅值=" << (fundVpp * 1000) << "mV\r\n";
    if (h1En && h1Vpp > 0) {
        osScope << "  谐波" << h1Order << ": " << formatFreq(freq * h1Order)
                << "  幅值=" << (h1Vpp * 1000) << "mV\r\n";
    }
    if (h2En && h2Vpp > 0) {
        osScope << "  谐波" << h2Order << ": " << formatFreq(freq * h2Order)
                << "  幅值=" << (h2Vpp * 1000) << "mV\r\n";
    }
    if (noiseEn) {
        osScope << "  噪声  : " << formatFreq(noiseFreq) << "  幅值=200mV";
        if (noiseLeak) {
            osScope << "\r\n  *** 频率非整数倍, FFT 泄漏! ***";
        }
    } else {
        osScope << "  噪声  : 无";
    }
    SetDlgItemTextW(hwnd, IDC_SCOPE_EDIT, s2ws(osScope.str()).c_str());

    // ══════════════════════════════════════════
    //  输出结果框 (时间 + 状态 + CSV路径)
    // ══════════════════════════════════════════
    // 获取当前时间
    char timeBuf[64];
    time_t now = time(NULL);
    struct tm* tmNow = localtime(&now);
    strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", tmNow);

    // 只输出 CSV, 输出到桌面 (避免 exe 在只读目录时写入失败)
    char desktopPath[MAX_PATH];
    BOOL hasDesktop = SHGetSpecialFolderPathA(NULL, desktopPath, CSIDL_DESKTOP, FALSE);
    std::string csvPath;
    if (hasDesktop) {
        csvPath = std::string(desktopPath) + "\\harmonic_waveform.csv";
    } else {
        // 回退到 exe 目录
        csvPath = g_exeDir + "\\harmonic_waveform.csv";
    }
    bool csvOk = writeCSV(csvPath, r);

    std::ostringstream os;
    os << timeBuf << " - " << (csvOk ? "done" : "fail") << "\r\n";

    // 追加历史记录 (保留最近10条)
    static std::vector<std::string> history;
    history.push_back(os.str());
    if (history.size() > 10) history.erase(history.begin());

    std::ostringstream osAll;
    for (const auto& s : history) osAll << s;

    SetDlgItemTextW(hwnd, IDC_OUTPUT_EDIT, s2ws(osAll.str()).c_str());
}

// ─── 复制到剪贴板 ───────────────────────────
static void OnCopy(HWND hwnd) {
    if (g_lastEquation.empty()) return;
    std::wstring ws = s2ws(g_lastEquation);
    if (OpenClipboard(hwnd)) {
        EmptyClipboard();
        size_t bytes = (ws.size() + 1) * sizeof(wchar_t);
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (hMem) {
            wchar_t* p = (wchar_t*)GlobalLock(hMem);
            memcpy(p, ws.c_str(), bytes);
            GlobalUnlock(hMem);
            SetClipboardData(CF_UNICODETEXT, hMem);
        }
        CloseClipboard();
        MessageBoxW(hwnd, L"公式代码已复制到剪贴板!", L"提示", MB_OK | MB_ICONINFORMATION);
    }
}

// ─── 打开文件位置 ───────────────────────────
static void OnOpenFolder() {
    // 打开 CSV 所在目录 (桌面), 选中 CSV 文件
    char desktopPath[MAX_PATH];
    wchar_t desktopW[MAX_PATH];
    if (SHGetSpecialFolderPathA(NULL, desktopPath, CSIDL_DESKTOP, FALSE)) {
        MultiByteToWideChar(CP_ACP, 0, desktopPath, -1, desktopW, MAX_PATH);
        std::wstring params = L"/select,\"" + std::wstring(desktopW) +
                               L"\\harmonic_waveform.csv\"";
        ShellExecuteW(NULL, L"open", L"explorer.exe", params.c_str(),
                      NULL, SW_SHOWNORMAL);
    } else {
        // 回退: 打开 exe 目录
        std::wstring dir = s2ws(g_exeDir);
        ShellExecuteW(NULL, L"explore", dir.c_str(), NULL, NULL, SW_SHOWNORMAL);
    }
}

// ─── 窗口过程 ────────────────────────────────
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        CreateControls(hwnd);
        UpdateRangeInfo(hwnd);
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_GENERATE_BTN:
            OnGenerate(hwnd);
            break;
        case IDC_COPY_BTN:
            OnCopy(hwnd);
            break;
        case IDC_OPEN_BTN:
            OnOpenFolder();
            break;
        case IDC_FREQ_EDIT:
        case IDC_FREQ_UNIT:
        case IDC_CYCLES_EDIT:
            if (HIWORD(wParam) == EN_CHANGE || HIWORD(wParam) == CBN_SELCHANGE)
                UpdateRangeInfo(hwnd);
            break;
        }
        return 0;

    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = (MINMAXINFO*)lParam;
        RECT rc = { 0, 0, SC(560), SC(910) + SC(g_yOffset) };
        AdjustWindowRect(&rc, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
        mmi->ptMinTrackSize.x = rc.right - rc.left;
        mmi->ptMinTrackSize.y = rc.bottom - rc.top;
        mmi->ptMaxTrackSize.x = rc.right - rc.left;
        mmi->ptMaxTrackSize.y = rc.bottom - rc.top;
        return 0;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        if (g_pLogo) {
            // logo 显示在窗口顶部居中
            int dispW = SC(g_logoDisplayW);
            int dispH = SC(g_logoDisplayH);
            int xPos = (SC(560) - dispW) / 2;
            if (xPos < 0) xPos = 0;
            Gdiplus::Graphics gfx(hdc);
            gfx.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            gfx.DrawImage(g_pLogo, xPos, 0, dispW, dispH);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        // 去掉静态文字/分组框的灰底, 统一使用白色背景
        HDC hdcCtl = (HDC)wParam;
        SetTextColor(hdcCtl, RGB(0, 0, 0));
        SetBkColor(hdcCtl, RGB(255, 255, 255));
        return (INT_PTR)g_hWhiteBrush;
    }

    case WM_ERASEBKGND: {
        // 用白色填充背景, 避免闪烁
        HDC hdc = (HDC)wParam;
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, g_hWhiteBrush);
        return 1;
    }

    case WM_DESTROY:
        if (g_pLogo) { delete g_pLogo; g_pLogo = NULL; }
        if (g_hAppIcon) { DestroyIcon(g_hAppIcon); g_hAppIcon = NULL; }
        if (g_hWhiteBrush) { DeleteObject(g_hWhiteBrush); g_hWhiteBrush = NULL; }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ─── WinMain ────────────────────────────────
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    // 获取可执行文件目录
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    std::wstring exeW(exePath);
    size_t pos = exeW.find_last_of(L"\\/");
    if (pos != std::wstring::npos) exeW = exeW.substr(0, pos);
    g_exeDir = ws2s(exeW);

    // ── 加载顶部大图 logo (PNG, 从资源加载) ──
    {
        g_pLogo = LoadImageFromResource(hInstance, L"PNG", 100);
        if (g_pLogo && g_pLogo->GetLastStatus() == Gdiplus::Ok) {
            // 显示高度固定 80 设计像素, 宽度按图像原始比例
            const int logoDisplayH = 80;
            int imgW = (int)g_pLogo->GetWidth();
            int imgH = (int)g_pLogo->GetHeight();
            int logoDisplayW = (imgH > 0)
                ? (int)((INT64)logoDisplayH * imgW / imgH)
                : logoDisplayH;
            g_logoDisplayW = logoDisplayW;
            g_logoDisplayH = logoDisplayH;
            g_yOffset = logoDisplayH + 12;  // logo 高度 + 间距
        } else {
            delete g_pLogo;
            g_pLogo = NULL;
            g_yOffset = 0;
        }
        g_hWhiteBrush = CreateSolidBrush(RGB(255, 255, 255));
    }

    // ── 加载窗口图标 (从资源加载 JPG) ──
    {
        Gdiplus::Image* pIconSrc = LoadImageFromResource(hInstance, L"JPG", 101);
        if (pIconSrc && pIconSrc->GetLastStatus() == Gdiplus::Ok) {
            int imgW = (int)pIconSrc->GetWidth();
            int imgH = (int)pIconSrc->GetHeight();
            int iconSize = 32;
            Gdiplus::Bitmap* pBmp = new Gdiplus::Bitmap(iconSize, iconSize,
                PixelFormat32bppARGB);
            Gdiplus::Graphics gfx(pBmp);
            gfx.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            // 居中绘制 (保持比例)
            int drawW = iconSize, drawH = iconSize;
            if (imgW > 0 && imgH > 0) {
                double scale = (double)iconSize / (imgW > imgH ? imgW : imgH);
                drawW = (int)(imgW * scale);
                drawH = (int)(imgH * scale);
            }
            gfx.DrawImage(pIconSrc, (iconSize - drawW) / 2,
                          (iconSize - drawH) / 2, drawW, drawH);
            HICON hIcon = NULL;
            pBmp->GetHICON(&hIcon);
            if (hIcon) {
                g_hAppIcon = hIcon;
            }
            delete pBmp;
        }
        delete pIconSrc;
    }

    // ── DPI 感知: 使 GetDpiForWindow 返回真实 DPI ──
    // 方法1: manifest (最可靠), 方法2: 运行时调用
    typedef BOOL (WINAPI *PFN_SetProcessDpiAwarenessContext)(HANDLE);
    typedef BOOL (WINAPI *PFN_SetProcessDPIAware)(void);
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (hUser32) {
        auto pSetCtx = (PFN_SetProcessDpiAwarenessContext)
            GetProcAddress(hUser32, "SetProcessDpiAwarenessContext");
        if (pSetCtx) {
            // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 = ((DPI_CONTEXT_HANDLE)-4)
            pSetCtx((HANDLE)-4);
        } else {
            auto pSetAware = (PFN_SetProcessDPIAware)
                GetProcAddress(hUser32, "SetProcessDPIAware");
            if (pSetAware) pSetAware();
        }
    }

    // 创建临时窗口测量 DPI
    HDC hdc = GetDC(NULL);
    g_dpi = GetDeviceCaps(hdc, LOGPIXELSY);
    ReleaseDC(NULL, hdc);
    if (g_dpi < 96) g_dpi = 96;
    g_scale = g_dpi / 96.0;

    // 创建字体 (随 DPI 缩放)
    int fontSize = (int)(18 * g_scale);      // 主字体
    int boldSize = (int)(18 * g_scale);      // 粗体
    int monoSize = (int)(16 * g_scale);      // 等宽字体
    g_hFont = CreateFontW(fontSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, FF_DONTCARE, L"Microsoft YaHei");
    g_hBoldFont = CreateFontW(boldSize, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, FF_DONTCARE, L"Microsoft YaHei");
    g_hMonoFont = CreateFontW(monoSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, FF_DONTCARE, L"Consolas");

    // 注册窗口类
    const wchar_t* className = L"ArbExpressGenerator";
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance      = hInstance;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground  = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = className;
    wc.hIcon         = LoadIcon(NULL, IDI_APPLICATION);
    RegisterClassExW(&wc);

    // 创建主窗口 (尺寸随 DPI 缩放, 含 logo 占用的纵向偏移)
    // 用 AdjustWindowRect 计算包含标题栏/边框的完整窗口尺寸
    RECT rc = { 0, 0, SC(560), SC(910) + SC(g_yOffset) };
    AdjustWindowRect(&rc, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
    int winW = rc.right - rc.left;
    int winH = rc.bottom - rc.top;
    g_hMainWnd = CreateWindowExW(0, className,
        L"ArbExpress 谐波波形生成器 by.Wloeve",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, winW, winH,
        NULL, NULL, hInstance, NULL);

    // 设置窗口图标 (标题栏 + 任务栏 + Alt+Tab)
    if (g_hAppIcon) {
        SendMessageW(g_hMainWnd, WM_SETICON, ICON_BIG,   (LPARAM)g_hAppIcon);
        SendMessageW(g_hMainWnd, WM_SETICON, ICON_SMALL, (LPARAM)g_hAppIcon);
    }

    ShowWindow(g_hMainWnd, nCmdShow);
    UpdateWindow(g_hMainWnd);

    // 消息循环
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        if (!IsDialogMessageW(g_hMainWnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (g_hFont)    DeleteObject(g_hFont);
    if (g_hBoldFont) DeleteObject(g_hBoldFont);
    if (g_hMonoFont) DeleteObject(g_hMonoFont);
    return (int)msg.wParam;
}
