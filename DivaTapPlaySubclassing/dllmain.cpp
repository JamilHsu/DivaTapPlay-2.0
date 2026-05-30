#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <windowsx.h>
#include <wincodec.h>
#include <d3d11.h>
#include <dxgi.h>
#include <commctrl.h>
#include <comdef.h>
#include <fstream>
#include <array>
#include <span>
#include <cassert>
#include <chrono>

#include "PrecompiledBlob.h"

#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "windowscodecs.lib")

//簡單的線性查找Map。
//迭代器在有元素移除時會失效
//移除與clear時不一定會呼叫析構函數
template <class Key, class T, size_t Capacity>
class LinearMap {
public:
    using value_type = std::pair<Key, T>;
    using size_type = size_t;
    using iterator = std::array<value_type, Capacity>::iterator;
    using const_iterator = std::array<value_type, Capacity>::const_iterator;

    static_assert(Capacity <= 64, "The capacity is too large to be suitable for using LinearMap.");
    static_assert(std::is_trivially_destructible_v<Key>);
    static_assert(std::is_trivially_destructible_v<T>);
private:
    std::array<value_type, Capacity> m_data;
    size_type m_size = 0;

public:
    size_type size() const noexcept { return m_size; }
    bool empty() const noexcept { return m_size == 0; }
    constexpr size_type capacity() const noexcept { return Capacity; }

    iterator begin() noexcept { return m_data.begin(); }
    iterator end()   noexcept { return m_data.begin() + m_size; }
    const_iterator begin() const noexcept { return m_data.begin(); }
    const_iterator end()   const noexcept { return m_data.begin() + m_size; }
    const_iterator cbegin() const noexcept { return begin(); }
    const_iterator cend()   const noexcept { return end(); }

    iterator find(const Key& key) noexcept {
        iterator it = begin();
        for (iterator endIt = end(); it != endIt; ++it)
            if (it->first == key)
                return it;
        return it;
    }

    const_iterator find(const Key& key) const noexcept {
        const_iterator it = begin();
        for (const_iterator endIt = end(); it != endIt; ++it)
            if (it->first == key)
                return it;
        return it;
    }

    T& at(const Key& key) {
        if (auto it = find(key); it != end())
            return it->second;
        throw std::out_of_range("LinearMap.at() key not found");
    }

    const T& at(const Key& key) const {
        if (auto it = find(key); it != end())
            return it->second;
        throw std::out_of_range("LinearMap.at() key not found");
    }

    // --- 插入 insert ---
    // 回傳：pair<iterator, bool>   bool=true 表示成功新插入
    std::pair<iterator, bool> insert(const value_type& value) {
        auto it = find(value.first);
        if (it != end()) {
            // key 已存在 → 不新增
            return { it, false };
        }
        if (m_size >= Capacity)
            throw std::out_of_range("LinearMap capacity exceeded");

        m_data[m_size] = value;
        return { begin() + m_size++, true };
    }
    T& operator[](const Key& key) {
        return insert({ key, T{} }).first->second;
    }
    // --- 移除 erase(iterator) ---
    // 用最後元素移來補位
    void erase(iterator pos) {
        assert(begin() <= pos && pos < end());
        if (pos < begin() || end() <= pos) return;

        --m_size;

        if (pos - begin() != m_size)
            *pos = std::move(m_data[m_size]);
    }

    // --- 移除 erase(key) ---
    size_type erase(const Key& key) {
        if (auto it = find(key); it != end()) {
            erase(it);
            return 1;
        }
        return 0;
    }

    void clear() noexcept { m_size = 0; }
};

ULONGLONG frequency_micro;
static std::chrono::microseconds PerformanceCountToMicrosecond(UINT64 PerfCount) {
    // This optimization mimics the implementation of std::chrono::high_resolution_clock::now() in __msvc_chrono.hpp.
    
    // The compiler recognizes the constants for frequency and time period and uses shifts and
    // multiplies instead of divides to calculate the value.
    constexpr long long TenMHz = 10'000'000 / std::chrono::microseconds::period::den;
    if (frequency_micro == TenMHz) [[likely]] {
        // 10 MHz is a very common QPC frequency on modern x86/x64 PCs.
        return std::chrono::microseconds(PerfCount / TenMHz);
    }
    // 24 MHz is a common frequency on ARM/ARM64, and I decide ignore it.
    return std::chrono::microseconds(PerfCount / frequency_micro);
}
std::array<BYTE, 8> vk_button{
    'I',
    'J',
    'K',
    'L',
    'W',
    'A',
    'S',
    'D',
};
auto vk_stick = [vk_s = std::array<BYTE, 5>{ 'Q', 'U', '\0', 'E', 'O' }]
(int stick) mutable ->BYTE& {
    return vk_s.at(stick + 2);
    };

int g_UI_type = 1; //0=No UI; 1=△□×◯; 2=XYBA
double g_opacity = 0.5;

int g_slider_height = 25; //(0~100)
double g_slide_require = 5;
double g_slide_requireS = 5;
double g_reduce_ratio = 0.001 * 0.2;
double g_reduce_ratioS = 0.001 * 0.2;
bool g_running_on_real_Windows = false; // Using GetPointerFrameInfo on Linux will causes a crash.
bool g_enable_mouse_in_pointer = false;

bool g_mod_active = true;
static PCWSTR ReadSettings() {
    std::ifstream file("DivaTapPlaySettings.txt");
    if (file.is_open()) {
        std::string str;
        auto SetVk = [&str,&file](BYTE& value) ->BOOL {
            if (std::getline(file, str)) {
                if (str.size() >= 1 && (str.size()==1 || isspace(str[1]))) {
                    if (isupper(str[0])) {
                        value = str[0];
                        return 0;
                    }
                    else {
                        return -1;
                    }
                }
                else {
                    int vk = atoi(str.c_str());
                    if (vk <= 0 || vk > 255) {
                        return -1;
                    }
                    else {
                        value = vk;
                        return 0;
                    }
                }
            }
            else {
                return -1;
            }
            };
        for (int i = 0; i < 8; ++i)
        {
            if (SetVk(vk_button[i])) {
                goto err_when_read;
            }
        }
        //這裡使用了or的短路求值
        if (!(SetVk(vk_stick(-1))
            || SetVk(vk_stick(1))
            || SetVk(vk_stick(-2))
            || SetVk(vk_stick(2)))) {
            if (std::getline(file, str) && str.size() >= 1) {
                g_UI_type = atoi(str.c_str());
                if (std::getline(file, str) && str.size() >= 1) {
                    g_opacity = atof(str.c_str());
                    if (g_opacity == 0.0) {
                        g_UI_type = 0;
                    }
                    if (std::getline(file, str) && str.size() >= 1) {
                        g_slider_height = atoi(str.c_str());
                        if (std::getline(file, str) && str.size() >= 1) {
                            g_slide_require = atof(str.c_str());
                            if (std::getline(file, str) && str.size() >= 1) {
                                g_reduce_ratio = atof(str.c_str());
                                if (std::getline(file, str) && str.size() >= 1) {
                                    g_slide_requireS = atof(str.c_str());
                                    if (std::getline(file, str) && str.size() >= 1) {
                                        g_reduce_ratioS = atof(str.c_str());
                                        if (std::getline(file, str) && str.size() >= 1) {
                                            g_running_on_real_Windows = atoi(str.c_str());
                                            if (std::getline(file, str) && str.size() >= 1) {
                                                g_enable_mouse_in_pointer = atoi(str.c_str());
                                                return nullptr;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    err_when_read:
        return L"An error occurred while reading settings from \"DivaTapPlaySettings.txt\"\n"
            "The settings that could not be read will be replaced with the default settings.";
    }
    return L"Failed to open \"DivaTapPlaySettings.txt\"\n"
        "Use the default settings.";
}
class Controller {
public:
    struct PointerInfo {
        double x = 0;
        int y = 0;
        bool InSliderOnly = false;
        bool start_in_top = false;
        int8_t pressingButton = 0;// 0保留給未按下 // 1/2/3/4 / 5/6/7/8
        int8_t pressingDirectionalButton = 0; //負代表向左，正代表向右  1是左邊的搖桿；2是右邊
        double momentum = 0;
        std::chrono::microseconds last_updata = {};
    };
    
    struct {
        std::array<bool, 8> buttons;
        std::array<ULONGLONG, 4> button_up_time;//主要按鍵上次抬起的時間；0代表最後抬起的是次要按鍵
        std::array<int16_t, 2>sticks;
    }keybd_state{};
    int originX = 0;
    int originY = 0;
    int m_width = 1;
    int m_height = 1;
    int m_slider_height = 0;
    UINT32 last_frameId = -1;
    LinearMap<WORD, PointerInfo, 16> pointerCache;
    void OnPointerDown(WORD pointerId, double x, int y, std::chrono::microseconds timestamp) {
        x -= originX;
        y -= originY;
        auto [it, inserted] = pointerCache.insert({ pointerId, {x,y} });
        if (!inserted) {
            OnPointerUp(it->first, 0);
            it = pointerCache.insert({ pointerId, {x,y} }).first;
        }
        it->second.last_updata = timestamp;
        if (y < m_height / 16) {
            it->second.start_in_top = true;
        }
        if (!g_mod_active) {
            return;
        }
        if (y < m_slider_height) {
            it->second.InSliderOnly = true;
            return;
        }
        int button_index = x * 4 / m_width;
        assert(0 <= button_index && button_index < 4);
        if (button_index >= 4) {
            button_index = 3;
        }
        else if (button_index < 0) {
            button_index = 0;
        }
        if (keybd_state.buttons[button_index] && keybd_state.buttons[button_index + 4]) {
            //兩個按鍵都已經按下去了，忽略這第三根手指
            return;
        }
        else {
            if (!keybd_state.buttons[button_index] && !keybd_state.buttons[button_index + 4]) {
                //兩個按鍵都沒被按著
                //優先選擇主要按鍵，除非上一個抬起來的是主要按鍵，且才剛被抬起來
                if (keybd_state.button_up_time[button_index] != 0
                    && GetTickCount64() - keybd_state.button_up_time[button_index] < 85) {
                    button_index += 4;
                }
            }
            else {
                //只有一個按鍵按著；選另一個
                if (keybd_state.buttons[button_index]) {
                    button_index += 4;
                }
            }
            if (!keybd_state.buttons[button_index]) {
                //按下按鍵
                keybd_state.buttons[button_index] = true;
                it->second.pressingButton = button_index + 1;
                SendKeybdInput(vk_button[button_index]);
            }
        }
    }
    void OnPointerUp(WORD pointerId,int y) {
        y -= originY;
        auto it = pointerCache.find(pointerId);
        if (it == pointerCache.end()) {
            return;
        }
        if (it->second.pressingButton) {
            int button_index = it->second.pressingButton - 1;
            keybd_state.buttons.at(button_index) = false;
            SendKeybdInput(vk_button[button_index], KEYEVENTF_KEYUP);
            if (button_index < 4) {
                keybd_state.button_up_time[button_index] = GetTickCount64();
            }
            else {
                keybd_state.button_up_time[button_index - 4] = 0;
            }
        }
        if (it->second.pressingDirectionalButton) {
            keybd_state.sticks.at(abs(it->second.pressingDirectionalButton) - 1) = 0;
            SendKeybdInput(vk_stick(it->second.pressingDirectionalButton), KEYEVENTF_KEYUP);
        }
        if (it->second.start_in_top
            && pointerCache.size() == 1
            && (m_height - y) < m_height / 16) {
            g_mod_active = !g_mod_active;
        }
        pointerCache.erase(it);
        return;
    }
    void OnPointerUpdate(WORD pointerId, double x,int y, std::chrono::microseconds timestamp) {
        x -= originX;
        y -= originY;
        auto it = pointerCache.find(pointerId);
        if (it == pointerCache.end()) {
            return;
        }
        auto duration = timestamp - it->second.last_updata;
        it->second.last_updata = timestamp;
        it->second.momentum = ReduceMomentum(it->second.momentum, duration
            , it->second.InSliderOnly ? g_reduce_ratioS : g_reduce_ratio);//減少動量
        it->second.momentum = AddMomentum(it->second.momentum, x - it->second.x, y - it->second.y); //增加動量
        it->second.x = x;
        it->second.y = y;
        if (it->second.pressingDirectionalButton) {
            if ((it->second.momentum > 0) != (it->second.pressingDirectionalButton > 0)//方向反了
                || it->second.momentum == 0) { //速度太低
                //抬起按鍵
                keybd_state.sticks.at(abs(it->second.pressingDirectionalButton) - 1) = 0;
                SendKeybdInput(vk_stick(it->second.pressingDirectionalButton), KEYEVENTF_KEYUP);
                it->second.pressingDirectionalButton = 0;
            }
        }
        if (it->second.pressingDirectionalButton) {
            return;
        }
        int LR;
        if (keybd_state.sticks[0] == 0) {
            LR = 1;
        }
        else if (keybd_state.sticks[1] == 0) {
            LR = 2;
        }
        else {
            return;
        }
        if (abs(it->second.momentum) > (it->second.InSliderOnly ? g_slide_requireS : g_slide_require)) {
            keybd_state.sticks[LR - 1]
                = it->second.pressingDirectionalButton
                = (it->second.momentum > 0 ? LR : -LR);
            SendKeybdInput(vk_stick(it->second.pressingDirectionalButton));
        }
    }
    void SendKeybdInput(BYTE vk_code, DWORD Flags = NULL) {
        INPUT input{};
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = vk_code;
        input.ki.dwFlags = Flags;
        SendInput(1, &input, sizeof(INPUT));
    }
    static double ReduceMomentum(double momentum, std::chrono::microseconds over_time, double reduce_ratio) noexcept {
        double t = std::abs(momentum) - over_time.count() * reduce_ratio;
        return t < 0 ? 0 : std::copysign(t, momentum);
    }
    static double AddMomentum(double Augend, double diffX, int diffY) noexcept {
        if (diffX == 0.0) {
            return Augend;
        }
        double Addend = diffX * (1 + std::log(std::hypot(diffX, diffY) / std::abs(diffX)));
        if (std::signbit(Augend) == std::signbit(Addend)) {
            return std::copysign(std::sqrt(Augend * Augend + std::abs(Addend)), Augend);
        }
        else {
            double t = Augend + Addend;
            return std::signbit(t) == std::signbit(Augend) ? t : std::copysign(std::sqrt(std::abs(t)), t);
        }
    }
    void clearup() noexcept{
        for (int i = 0; i < keybd_state.buttons.size(); ++i) {
            if (keybd_state.buttons[i]) {
                SendKeybdInput(vk_button[i], KEYEVENTF_KEYUP);
            }
        }
        if (keybd_state.sticks[0]) {
            SendKeybdInput(vk_stick(keybd_state.sticks[0]), KEYEVENTF_KEYUP);
        }
        if (keybd_state.sticks[1]) {
            SendKeybdInput(vk_stick(keybd_state.sticks[1]), KEYEVENTF_KEYUP);
        }
        keybd_state = {};
        pointerCache.clear();
    }
};
Controller g_controller;
HWND g_DivaWindow;
static void OnPointer(WORD pointerId) {
    struct pos_scale_factor {
        HANDLE sourceDevice;
        int srcleft;
        int dstleft;
        //int srctop;
        //int dsttop;
        double rx;
        //double ry;
    };
    static pos_scale_factor affine_transform_cache;
    POINTER_INFO pointerFrame[16];
    UINT32 pointerCount = 16;
    if (GetPointerFrameInfo(pointerId, &pointerCount, pointerFrame) && pointerCount > 0) {
        if (pointerFrame->frameId == g_controller.last_frameId) {
            return;
        }
        g_controller.last_frameId = pointerFrame->frameId;
        SkipPointerFrameMessages(pointerId);

        pos_scale_factor* paffine_transform = nullptr;
        {
            if (affine_transform_cache.sourceDevice == pointerFrame[0].sourceDevice) {
                paffine_transform = &affine_transform_cache;
            }
            else{
                RECT source, dest;
                if (GetPointerDeviceRects(pointerFrame[0].sourceDevice, &source, &dest)) {
                    double rx = static_cast<double>(dest.right - dest.left) / (source.right - source.left);
                    affine_transform_cache = { .sourceDevice = pointerFrame[0].sourceDevice, .srcleft = source.left, .dstleft = dest.left, .rx = rx };
                    paffine_transform = &affine_transform_cache;
                }
            }
        }

        std::chrono::microseconds timestamp = PerformanceCountToMicrosecond(pointerFrame->PerformanceCount);
        for (UINT32 i = 0; i < pointerCount; ++i) {
            if (pointerFrame[i].pointerFlags & (POINTER_FLAG_UP | POINTER_FLAG_CANCELED | POINTER_FLAG_CAPTURECHANGED)) {
                g_controller.OnPointerUp(pointerFrame[i].pointerId, pointerFrame[i].ptPixelLocation.y);
            }
            else {
                float x = paffine_transform ?
                    (pointerFrame[i].ptHimetricLocationRaw.x - paffine_transform->srcleft) * paffine_transform->rx + paffine_transform->dstleft
                    : pointerFrame[i].ptPixelLocationRaw.x;

                float diff = x - pointerFrame[i].ptPixelLocationRaw.x;
                
                if (std::abs(diff) > 1.0) {                    
                    x = pointerFrame[i].ptPixelLocationRaw.x;
                    paffine_transform = nullptr;
                    affine_transform_cache = {};
                }

                if (pointerFrame[i].pointerFlags & POINTER_FLAG_UPDATE) {
                    if (pointerFrame[i].pointerFlags & POINTER_FLAG_INCONTACT) {
                        g_controller.OnPointerUpdate(pointerFrame[i].pointerId, x, pointerFrame[i].ptPixelLocationRaw.y,
                            timestamp);
                    }
                }
                else if (pointerFrame[i].pointerFlags & POINTER_FLAG_DOWN) {
                    g_controller.OnPointerDown(pointerFrame[i].pointerId, x, pointerFrame[i].ptPixelLocationRaw.y,
                        timestamp);
                }
            }
        }
    }
}
LRESULT Subclassproc(
    HWND hWnd,
    UINT uMsg,
    WPARAM wParam,
    LPARAM lParam,
    UINT_PTR uIdSubclass,
    DWORD_PTR dwRefData
)
{
    switch (uMsg) {
    case WM_WINDOWPOSCHANGED: {
        RECT rect;
        GetClientRect(hWnd, &rect);
        const int newW = rect.right;
        const int newH = rect.bottom;
        g_controller.m_width = newW;
        g_controller.m_height = newH;
        g_controller.m_slider_height = MulDiv(newH, g_slider_height, 100);
        POINT origin{ 0,0 };
        ClientToScreen(hWnd, &origin);
        g_controller.originX = origin.x;
        g_controller.originY = origin.y;
        break;
    }
    case WM_POINTERUPDATE:
    case WM_POINTERDOWN:
    case WM_POINTERUP:
    case WM_POINTERENTER:
    case WM_POINTERLEAVE:
    case WM_POINTERCAPTURECHANGED:
        if (g_running_on_real_Windows && g_mod_active) {
            OnPointer(GET_POINTERID_WPARAM(wParam));
            return 0;
        }
        else {
            switch (uMsg) {
            case WM_POINTERUPDATE:
                if (g_mod_active) {
                    if (IS_POINTER_INRANGE_WPARAM(wParam) && IS_POINTER_INCONTACT_WPARAM(wParam)) {
                        WORD pointerId = GET_POINTERID_WPARAM(wParam);
                        int x = GET_X_LPARAM(lParam);
                        int y = GET_Y_LPARAM(lParam);
                        LARGE_INTEGER now;
                        QueryPerformanceCounter(&now);
                        g_controller.OnPointerUpdate(pointerId, x, y,
                            PerformanceCountToMicrosecond(now.QuadPart));
                    }
                    return 0;
                }
                break;
            case WM_POINTERDOWN: {
                WORD pointerId = GET_POINTERID_WPARAM(wParam);
                int x = GET_X_LPARAM(lParam);
                int y = GET_Y_LPARAM(lParam);
                LARGE_INTEGER now;
                QueryPerformanceCounter(&now);
                g_controller.OnPointerDown(pointerId, x, y,
                    PerformanceCountToMicrosecond(now.QuadPart));
                if (g_mod_active) {
                    return 0;
                }
                break;
            }
            case WM_POINTERUP: {
                WORD pointerId = GET_POINTERID_WPARAM(wParam);
                g_controller.OnPointerUp(pointerId, GET_Y_LPARAM(lParam));
                if (g_mod_active) {
                    return 0;
                }
                break;
            }
            case WM_POINTERCAPTURECHANGED:
                g_controller.clearup();
                break;
            }
        }
        break;
    case WM_DESTROY:
        g_controller.clearup();
        break;
    }
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

// These Direct3D code was generated by AI.

int g_frameWidth = 0;
int g_frameHeight = 0;

ID3D11Device* gDevice = nullptr;
ID3D11DeviceContext* gContext = nullptr;

ID3D11VertexShader* gVS = nullptr;
ID3D11PixelShader* gPS = nullptr;
ID3D11InputLayout* gLayout = nullptr;

ID3D11Buffer* gVB = nullptr;
ID3D11Buffer* gCB = nullptr;

ID3D11BlendState* gBlendState = nullptr;
ID3D11SamplerState* gSampler = nullptr;

ID3D11ShaderResourceView* gButtonImages[8] = {};

IWICImagingFactory* gWIC = nullptr;

struct Vertex
{
    float x, y;
    float u, v;
};

struct PSConstants
{
    float opacity;
    float pad[3];
};

static HRESULT LoadTextureFromFile(
    const wchar_t* file,
    ID3D11ShaderResourceView** outSRV,
    std::vector<BYTE>& buffer)
{
    *outSRV = nullptr;

    IWICBitmapDecoder* decoder;
    HRESULT hr = gWIC->CreateDecoderFromFilename(
        file,
        nullptr,
        GENERIC_READ,
        WICDecodeMetadataCacheOnLoad,
        &decoder);

    if (SUCCEEDED(hr)) {
        IWICBitmapFrameDecode* frame;
        hr = decoder->GetFrame(0, &frame);
        if (SUCCEEDED(hr)) {
            IWICFormatConverter* converter;
            hr = gWIC->CreateFormatConverter(&converter);
            if (SUCCEEDED(hr)) {
                hr = converter->Initialize(
                    frame,
                    GUID_WICPixelFormat32bppRGBA,
                    WICBitmapDitherTypeNone,
                    nullptr,
                    0,
                    WICBitmapPaletteTypeCustom);
                if (SUCCEEDED(hr)) {
                    UINT width = 0;
                    UINT height = 0;

                    hr=converter->GetSize(&width, &height);
                    if (SUCCEEDED(hr)) {
                        buffer.resize(width * height * 4);
                        BYTE* pixels = buffer.data();

                        hr = converter->CopyPixels(
                            nullptr,
                            width * 4,
                            width * height * 4,
                            pixels);
                        if (SUCCEEDED(hr)) {
                            D3D11_TEXTURE2D_DESC td = {};
                            td.Width = width;
                            td.Height = height;
                            td.MipLevels = 1;
                            td.ArraySize = 1;
                            td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                            td.SampleDesc.Count = 1;
                            td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

                            D3D11_SUBRESOURCE_DATA data = {};
                            data.pSysMem = pixels;
                            data.SysMemPitch = width * 4;

                            ID3D11Texture2D* tex;
                            hr=gDevice->CreateTexture2D(
                                &td,
                                &data,
                                &tex);
                            if (SUCCEEDED(hr)) {
                                hr = gDevice->CreateShaderResourceView(
                                    tex,
                                    nullptr,
                                    outSRV);

                                tex->Release();
                            }
                        }
                    }
                }
                converter->Release();
            }
            frame->Release();
        }
        decoder->Release();
    }

    return hr;
}

//==============================================================

static HRESULT LoadButtonImages() {
    constexpr const wchar_t* files[2][8] = {
    {
        L"BTN_SANKAKU_OFF.png",
        L"BTN_SANKAKU_ON.png",

        L"BTN_SHIKAKU_OFF.png",
        L"BTN_SHIKAKU_ON.png",

        L"BTN_BATSU_OFF.png",
        L"BTN_BATSU_ON.png",

        L"BTN_MARU_OFF.png",
        L"BTN_MARU_ON.png"
    }
        ,
    {
        L"BTN_X_OFF.png",
        L"BTN_X_ON.png",

        L"BTN_Y_OFF.png",
        L"BTN_Y_ON.png",

        L"BTN_B_OFF.png",
        L"BTN_B_ON.png",

        L"BTN_A_OFF.png",
        L"BTN_A_ON.png"
    } };

    int index;
    if (g_UI_type == 1) {
        index = 0;
    }
    else if (g_UI_type == 2) {
        index = 1;
    }
    else {
        return S_OK;
    }
    HRESULT hr = CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&gWIC));

    if (FAILED(hr)) {
        return hr;
    }
    std::vector<BYTE> buffer;
    for (int i = 0; i < 8; i++)
    {
        hr = LoadTextureFromFile(files[index][i], &gButtonImages[i], buffer);
        if (FAILED(hr)) {
            break;
        }
    }

    gWIC->Release();
    gWIC = nullptr;

    return hr;
}

//==============================================================
struct DrawTextureParam {
    ID3D11ShaderResourceView* image;
    float x;
    float y;
    float w;
    float h;
};
static void Draw4Textures(std::span<const DrawTextureParam, 4> params)
{
    constexpr UINT stride = sizeof(Vertex);
    constexpr UINT offset = 0;

    gContext->IASetVertexBuffers(
        0,
        1,
        &gVB,
        &stride,
        &offset);

    static std::array<DrawTextureParam, 4> cachedParams;
    if (memcmp(cachedParams.data(), params.data(), sizeof(cachedParams))) {
        memcpy(cachedParams.data(), params.data(), sizeof(cachedParams));
        std::array<std::array<Vertex, 6>, 4> verts;
        for (size_t i = 0; i < 4; ++i)
        {
            const auto& p = params[i];

            float left = p.x / g_frameWidth * 2.0f - 1.0f;
            float right = (p.x + p.w) / g_frameWidth * 2.0f - 1.0f;
            float top = 1.0f - p.y / g_frameHeight * 2.0f;
            float bottom = 1.0f - (p.y + p.h) / g_frameHeight * 2.0f;

            verts[i] = { {
                { left,  top,    0, 0 },
                { right, top,    1, 0 },
                { left,  bottom, 0, 1 },

                { right, top,    1, 0 },
                { right, bottom, 1, 1 },
                { left,  bottom, 0, 1 }
            } };
        }

        D3D11_MAPPED_SUBRESOURCE map;

        if (SUCCEEDED(
            gContext->Map(
                gVB,
                0,
                D3D11_MAP_WRITE_DISCARD,
                0,
                &map)))
        {
            memcpy(
                map.pData,
                verts.data(),
                sizeof(verts));

            gContext->Unmap(gVB, 0);
        }
    }
    for (UINT i = 0; i < 4; ++i)
    {
        if (params[i].image)
        {
            gContext->PSSetShaderResources(
                0,
                1,
                &params[i].image);

            gContext->Draw(
                6,
                i * 6);
        }
    }
}
extern "C" {
    void __declspec(dllexport) __stdcall Init() {
        PCWSTR errmsg = ReadSettings();
        if (errmsg) {
            MessageBoxW(NULL, errmsg, L"TapPlay 2.0", MB_OK | MB_ICONERROR);
        }
        freopen("CONOUT$", "w", stdout);
        printf("[TapPlay 2.0] Settings:\n"
            "Buttons:\n"
            "%d %d %d %d\n"
            "%d %d %d %d\n"
            "<  >  <  >\n"
            "%d %d %d %d\n"
            "UI_type: %d\n"
            "opacity: %f\n"
            "slider_height: %d\n"
            "slide_require: %f\n"
            "momentum reduce_ratio: %f/microsecond\n"
            "slide_require in slider only area: %f\n"
            "momentum reduce_ratio in slider only area: %f\n"
            "Uses precise touch coordinates: %d\n"
            "enable_mouse_in_pointer: %d\n\n\n",
            vk_button[0], vk_button[1], vk_button[2], vk_button[3],
            vk_button[4], vk_button[5], vk_button[6], vk_button[7],
            vk_stick(-1), vk_stick(1), vk_stick(-2), vk_stick(2),
            g_UI_type,
            g_opacity,
            g_slider_height,
            g_slide_require,
            g_reduce_ratio,
            g_slide_requireS,
            g_reduce_ratioS,
            g_running_on_real_Windows,
            g_enable_mouse_in_pointer
        );
        LARGE_INTEGER f; QueryPerformanceFrequency(&f);
        frequency_micro = f.QuadPart / std::chrono::microseconds::period::den;
        if (frequency_micro == 0) [[unlikely]] {
            MessageBoxW(NULL, L"Fatal error: PerformanceFrequency per microsecond is zero\n"
                "This should never happen on modern computers.\n",
                L"TapPlay 2.0", MB_OK | MB_ICONERROR);
        }
    }
    void __declspec(dllexport) __stdcall OnResize(IDXGISwapChain* swapChain) {
        DXGI_SWAP_CHAIN_DESC desc;
        swapChain->GetDesc(&desc);
        g_frameWidth = desc.BufferDesc.Width;
        g_frameHeight = desc.BufferDesc.Height;
    }
    void __declspec(dllexport) __stdcall D3DInit(IDXGISwapChain* swapChain, ID3D11Device* device, ID3D11DeviceContext* deviceContext) {
        g_DivaWindow = FindWindowW(L"DIVAMIXP", nullptr);
        int res = SetWindowSubclass(g_DivaWindow, Subclassproc, 39, 0);
        if (g_enable_mouse_in_pointer) {
            EnableMouseInPointer(TRUE);
        }

        gDevice = device;
        gContext = deviceContext;

        OnResize(swapChain);
        {
            gDevice->CreateVertexShader(
                g_vsBlobRawdata,
                sizeof(g_vsBlobRawdata),
                nullptr,
                &gVS);

            gDevice->CreatePixelShader(
                g_psBlobRawdata,
                sizeof(g_psBlobRawdata),
                nullptr,
                &gPS);

            D3D11_INPUT_ELEMENT_DESC layout[] =
            {
                {
                    "POSITION", 0,
                    DXGI_FORMAT_R32G32_FLOAT,
                    0, 0,
                    D3D11_INPUT_PER_VERTEX_DATA,
                    0
                },

                {
                    "TEXCOORD", 0,
                    DXGI_FORMAT_R32G32_FLOAT,
                    0, 8,
                    D3D11_INPUT_PER_VERTEX_DATA,
                    0
                }
            };

            gDevice->CreateInputLayout(
                layout,
                2,
                g_vsBlobRawdata,
                sizeof(g_vsBlobRawdata),
                &gLayout);

        }
        D3D11_BUFFER_DESC vb = {};
        vb.ByteWidth = sizeof(Vertex) * 6 * 4;
        vb.Usage = D3D11_USAGE_DYNAMIC;
        vb.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        vb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        gDevice->CreateBuffer(
            &vb,
            nullptr,
            &gVB);

        D3D11_BUFFER_DESC cb = {};
        cb.ByteWidth = sizeof(PSConstants);
        cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

        gDevice->CreateBuffer(
            &cb,
            nullptr,
            &gCB);

        {
            PSConstants cb2 = {};
            cb2.opacity = g_opacity;

            gContext->UpdateSubresource(
                gCB,
                0,
                nullptr,
                &cb2,
                0,
                0);
        }
        D3D11_BLEND_DESC bd = {};

        bd.RenderTarget[0].BlendEnable = TRUE;
        bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
        bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

        gDevice->CreateBlendState(&bd, &gBlendState);

        D3D11_SAMPLER_DESC sd = {};

        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;

        sd.AddressU
            = sd.AddressV
            = sd.AddressW
            = D3D11_TEXTURE_ADDRESS_CLAMP;

        gDevice->CreateSamplerState(&sd, &gSampler);

        HRESULT hr = LoadButtonImages();
        if (FAILED(hr)) {
            _com_error err(hr);
            err.ErrorMessage();
            MessageBoxW(NULL, err.ErrorMessage(), L"DivaTapPlay - LoadButtonImages()", MB_OK | MB_ICONERROR);
        }
    }
    void __declspec(dllexport) __stdcall OnFrame(IDXGISwapChain* swapChain) {
        {
            static int count;
            static bool showing;
            static bool autohidden;
            if (++count > 10) {
                count = 0;
                bool shownow = ShowCursor(TRUE) >= 0; //According to actual tests, the game will quickly cause the Cursor display count to drift to a very large or very small value, so there is no need to worry about affecting the game's Cursor display state.
                if (shownow != showing) {
                    showing = shownow;
                    if (shownow) {
                        if (g_mod_active) {
                            g_mod_active = false;
                            autohidden = true;
                        }
                    }
                    else {
                        if (autohidden) {
                            autohidden = false;
                            g_mod_active = true;
                        }
                    }
                }
            }
        }

        if (g_mod_active && g_UI_type) {
            float blendFactor[4] = {};

            gContext->OMSetBlendState(
                gBlendState,
                blendFactor,
                0xffffffff);

            gContext->IASetInputLayout(gLayout);

            gContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

            gContext->VSSetShader(gVS, nullptr, 0);

            gContext->PSSetShader(gPS, nullptr, 0);

            gContext->PSSetSamplers(0, 1, &gSampler);

            gContext->PSSetConstantBuffers(0, 1, &gCB);

            D3D11_VIEWPORT oldViewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
            UINT oldViewportCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
            gContext->RSGetViewports(&oldViewportCount, oldViewports);
            D3D11_VIEWPORT newViewport = oldViewports[0];
            newViewport.TopLeftX = 0;
            newViewport.TopLeftY = 0;
            newViewport.Width = g_frameWidth;
            newViewport.Height = g_frameHeight;
            gContext->RSSetViewports(1, &newViewport);

            std::array<DrawTextureParam, 4> params;
            for (int i = 0; i < 4; i++)
            {
                int pressing_count =
                    g_controller.keybd_state.buttons[i] +
                    g_controller.keybd_state.buttons[i + 4];

                bool pressed = (pressing_count > 0);

                float x = g_frameWidth * i / 4.0f;

                float width = g_frameWidth / 4.0f;

                if (pressed) {
                    x += width * 0.02f;
                    width -= width * 0.04f;
                }

                float y = g_frameHeight - (width * (0.5f - 0.02f * pressing_count));

                params[i] = {
                    gButtonImages[i * 2 + (pressed ? 1 : 0)],
                        x,
                        y,
                        width,
                        width };
            }
            Draw4Textures(params);

            gContext->RSSetViewports(oldViewportCount, oldViewports);
        }
        // Force pumping message
        // The game seems to be pumping messages slowly, causing input lag.
        // (The game itself doesn't use messages as an input source, but I do.)
        MSG msg;
        while (PeekMessageA(&msg, NULL, NULL, NULL, PM_REMOVE | (QS_POINTER << 16))) {
            DispatchMessageA(&msg);
        }
    }
}