﻿#include <atomic>
#include <memory>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>

#include <cstdio>
#include <cwchar>
#include <cassert>

#include <windows.h>

using namespace std::literals;

#pragma warning(disable : 4996)

wchar_t* Titlename;
int isfound=0;

wchar_t* rtrim(wchar_t* str)
{
    wchar_t* ptr = str;
    str += wcslen(str);
    while (iswspace(*(--str))) *str = 0;
    return ptr;
}
wchar_t* ltrim(wchar_t* str)
{
    while (iswspace(*(str++)));
    return str - 1;
}
wchar_t* trim(wchar_t* str)
{
    return ltrim(rtrim(str));
}

namespace {

    // Global variables are necessary, because Windows hooks have a terrible API.
    HHOOK MouseHook;
    HHOOK KeyboardHook;

    constexpr unsigned SpamDownBit = 0x1;
    constexpr unsigned SpamUpBit = 0x2;
    std::atomic<unsigned> SpamFlags(0);

    DWORD DownKeyCode = 0;
    DWORD UpKeyCode = 0;

    bool IsGameInFocus() {
        auto Window = GetForegroundWindow();

        wchar_t* str;


        int len;
        len = wcslen(Titlename) + 1;

        str = (wchar_t*)std::malloc(len * sizeof(uint16_t));

        GetClassNameW(Window, str, len);
        //std::wcout << str << '\n';
        if (wcscmp(trim(str), Titlename) != 0) {
            GetWindowTextW(Window, str, len);

            //std::fputws(Titlename, stdout);
            //std::fputs("||\n", stdout);

            //std::fputws(str, stdout);
            //std::fputs("|\n", stdout);

            //std::wcout << Titlename << '\n';
            if (wcscmp(trim(str), Titlename) != 0) {
                std::free(str);
                return false;
            }
        }

        std::free(str);

        if (!isfound) {
         isfound = 1;
         std::fputs("Windows is found....\n", stdout);
        } 

        POINT Point;

        [[maybe_unused]]
        auto r = GetCursorPos(&Point);
        assert(r);

        if (Window != WindowFromPoint(Point)) {
            return false;
        }

        return true;
    }

    bool HandleKey(DWORD KeyCode, bool IsDown) {
        unsigned Mask = 0;

        if (KeyCode == DownKeyCode) {
            Mask = SpamDownBit;
        }
        else if (KeyCode == UpKeyCode) {
            Mask = SpamUpBit;
        }

        if (Mask == 0 || !IsGameInFocus()) {
            return false;
        }

        if (IsDown) {
            SpamFlags |= Mask;
        }
        else {
            SpamFlags &= ~Mask;
        }

        return true;
    }

    LRESULT CALLBACK LowLevelMouseProc(int Code, WPARAM WParam, LPARAM LParam) {
        if (Code < HC_ACTION) {
            return CallNextHookEx(MouseHook, Code, WParam, LParam);
        }

        assert(Code == HC_ACTION);

        DWORD KeyCode = 0;
        bool IsDown = false;
        switch (WParam) {
        case WM_LBUTTONDOWN: {
            IsDown = true;

            [[fallthrough]];
        }
        case WM_LBUTTONUP: {
            KeyCode = VK_LBUTTON;
            break;
        }
        case WM_RBUTTONDOWN: {
            IsDown = true;

            [[fallthrough]];
        }
        case WM_RBUTTONUP: {
            KeyCode = VK_RBUTTON;
            break;
        }
        case WM_MBUTTONDOWN: {
            IsDown = true;

            [[fallthrough]];
        }
        case WM_MBUTTONUP: {
            KeyCode = VK_MBUTTON;
            break;
        }
        case WM_XBUTTONDOWN: {
            IsDown = true;

            [[fallthrough]];
        }
        case WM_XBUTTONUP: {
            auto& Info = *reinterpret_cast<MSLLHOOKSTRUCT*>(LParam);
            KeyCode = VK_XBUTTON1 + HIWORD(Info.mouseData) - XBUTTON1;

            break;
        }
        }

        if (HandleKey(KeyCode, IsDown)) {
            return 1;
        }

        return CallNextHookEx(MouseHook, Code, WParam, LParam);
    }

    LRESULT CALLBACK LowLevelKeyboardProc(int Code, WPARAM WParam, LPARAM LParam) {
        if (Code < HC_ACTION) {
            return CallNextHookEx(KeyboardHook, Code, WParam, LParam);
        }

        assert(Code == HC_ACTION);

        auto& Info = *reinterpret_cast<KBDLLHOOKSTRUCT*>(LParam);

        auto IsDown = (WParam == WM_KEYDOWN || WParam == WM_SYSKEYDOWN);

        if (HandleKey(Info.vkCode, IsDown)) {
            return 1;
        }

        return CallNextHookEx(KeyboardHook, Code, WParam, LParam);
    }

    DWORD GetWheelDelta(unsigned Flags) {
        DWORD r = 0;

        if ((Flags & SpamDownBit) != 0) {
            r -= WHEEL_DELTA;
        }

        if ((Flags & SpamUpBit) != 0) {
            r += WHEEL_DELTA;
        }

        return r;
    }

    void CALLBACK TimerProc(void*, BOOLEAN) {
        if (IsGameInFocus()) {
            auto WheelDelta = GetWheelDelta(SpamFlags);

            if (WheelDelta != 0) {
                mouse_event(MOUSEEVENTF_WHEEL, 0, 0, WheelDelta, 0);
            }
        }
        else {
            SpamFlags = 0;
        }
    }

    struct file_deleter {
        void operator()(std::FILE* File) const noexcept {
            std::fclose(File);
        }
    };

    using unique_file = std::unique_ptr<std::FILE, file_deleter>;

    void ReadTitlename(const char* Filename) {

        unique_file File;
        uint16_t w;
        wchar_t* name;

        File = unique_file(std::fopen(Filename, "rb"));

        if (!File) {
            throw std::runtime_error("Unable to open configuration file \""s + Filename + "\".");
            return;
        }

        w = 0;
        if (std::fread(&w, sizeof(uint16_t), 1, File.get()) != 1) {
            throw std::runtime_error("Unable to read configuration file \""s + Filename + "\".");
        }
       
        if (w!=0xFEFF) {
            throw std::runtime_error("Unable to read configuration file \""s + Filename + "\".");
        }

        fseek(File.get(), 0, SEEK_END);
        unsigned long len = (unsigned long)ftell(File.get());
        fseek(File.get(), sizeof(uint16_t), SEEK_SET);

        name = (wchar_t*)std::malloc(len);

        if (std::fread(name, len- sizeof(uint16_t), 1, File.get()) != 1) {
            throw std::runtime_error("Unable to read configuration file \""s + Filename + "\".");
        }
        name[(len-1) / sizeof(uint16_t)] = 0;

        Titlename = trim(name);

        std::fputws(Titlename,stdout);
        std::fputs("\n",stdout);

    }


    // There are a few valid possible configurations:
    // <num>: <num> is the scroll down key.
    // <num> <c>: <num> is the key for either scroll up or down depending on whether or not <c> is `u` or not.
    // <num1> <num2>: <num1> is the scroll down key. <num2> is the scroll up key.
    void ReadConfiguration(const char* Filename) {

        unique_file File;

        File= unique_file(std::fopen(Filename, "rb"));

        if (!File) {
            throw std::runtime_error("Unable to open configuration file \""s + Filename + "\".");
            return;
        }

        auto KeyCodesRead = std::fscanf(File.get(), "%li %li", &DownKeyCode, &UpKeyCode);

        if (KeyCodesRead < 1) {
            throw std::runtime_error("Unable to read key code from configuration file \""s + Filename + "\".");
        }

        if (KeyCodesRead < 2) {
            char WheelDirection = 'd';

            if (std::fscanf(File.get(), " %c", &WheelDirection) == 1) {
                // `|32` converts ASCII letters to lower case.
                switch (WheelDirection | 32) {
                case 'd': break;
                case 'u': {
                    UpKeyCode = DownKeyCode;
                    DownKeyCode = 0;

                    break;
                }
                default: {
                    throw std::runtime_error("Invalid wheel direction '+"s + WheelDirection + "+' in configuration file \"" + Filename + "\".");
                }
                }
            }
        }

        if (DownKeyCode > VK_OEM_CLEAR) {
            throw std::runtime_error("Invalid key code " + std::to_string(DownKeyCode) + " in configuration file \""s + Filename + "\".");
        }

        if (UpKeyCode > VK_OEM_CLEAR) {
            throw std::runtime_error("Invalid key code " + std::to_string(UpKeyCode) + " in configuration file \""s + Filename + "\".");
        }

        if (DownKeyCode == UpKeyCode) {
            if (DownKeyCode == 0) {
                throw std::runtime_error("No key was bound to either scroll direction in configuration file \""s + Filename + "\".");
            }
            else {
                throw std::runtime_error("Scroll up and scroll down both have the same binding in configuration file \""s + Filename + "\".");
            }
        }



    }

    bool IsMouseButton(DWORD KeyCode) {
        return (VK_LBUTTON <= KeyCode && KeyCode <= VK_RBUTTON) || (VK_MBUTTON <= KeyCode && KeyCode <= VK_XBUTTON2);
    }

    bool IsKeyboardKey(DWORD KeyCode) {
        return KeyCode != 0 && !IsMouseButton(KeyCode);
    }

}

int main(int argc, char** argv) {
    try {
        auto ConfigFilename = "Dish2Macro.txt";
        auto TitleFilename  = "Titlename.txt";

        if (argc >= 2) {
            ConfigFilename = argv[1];
        }

        if (argc >= 3) {
            TitleFilename = argv[2];
        }

        ReadConfiguration(ConfigFilename);

        ReadTitlename(TitleFilename);

        if (IsMouseButton(DownKeyCode) || IsMouseButton(UpKeyCode)) {
            MouseHook = SetWindowsHookExW(WH_MOUSE_LL, LowLevelMouseProc, nullptr, 0);

            if (!MouseHook) {
                throw std::system_error(GetLastError(), std::system_category());
            }
        }

        if (IsKeyboardKey(DownKeyCode) || IsKeyboardKey(UpKeyCode)) {
            KeyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, nullptr, 0);

            if (!KeyboardHook) {
                throw std::system_error(GetLastError(), std::system_category());
            }
        }

        // Execute every 5 milliseconds, 200 times a second.
        DWORD Period = 5;

        HANDLE Timer;
        if (!CreateTimerQueueTimer(&Timer, nullptr, TimerProc, nullptr, 0, Period, WT_EXECUTEDEFAULT)) {
            throw std::system_error(GetLastError(), std::system_category());
        }

        for (;;) {
            MSG Message;
            while (GetMessageW(&Message, 0, 0, 0)) {
                TranslateMessage(&Message);
                DispatchMessageW(&Message);
            }
        }
    }
    catch (std::exception& e) {
        std::fprintf(stderr, "Error: %s\n", e.what());
        std::getchar();

        return 1;
    }
}
