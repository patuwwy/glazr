#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>

#include <algorithm>
#include <cstdlib>
#include <cwchar>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "resource.h"

#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Dwmapi.lib")
#pragma comment(lib, "UxTheme.lib")
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace
{
    constexpr int IdApplicationList = 1001;
    constexpr int IdOpacitySlider = 1002;
    constexpr int IdOpacityLabel = 1003;
    constexpr int IdStatusLabel = 1004;
    constexpr int IdOpacityPrompt = 1006;
    constexpr int IdRestoreMode = 1008;
    constexpr int IdHelpButton = 1009;
    constexpr UINT_PTR RefreshTimerId = 1;
    constexpr UINT_PTR EventRefreshTimerId = 2;
    constexpr UINT RefreshIntervalMs = 1500;
    constexpr UINT EventRefreshDelayMs = 100;
    constexpr UINT WindowEventMessage = WM_APP + 1;

    struct Application
    {
        std::wstring path;
        std::wstring key;
        std::wstring name;
        std::wstring titles;
        std::vector<HWND> windows;
        std::set<DWORD> processIds;
    };

    struct AppliedOpacity
    {
        std::wstring key;
        BYTE alpha;
    };

    struct SavedOpacity
    {
        std::wstring path;
        int percentage = 100;
        bool always = false;
        std::set<DWORD> processIds;
    };

    HWND g_mainWindow = nullptr;
    HWND g_applicationList = nullptr;
    HWND g_opacitySlider = nullptr;
    HWND g_opacityLabel = nullptr;
    HWND g_statusLabel = nullptr;
    HWND g_restoreMode = nullptr;
    HWINEVENTHOOK g_windowEventHook = nullptr;
    HWND g_helpButton = nullptr;
    std::vector<Application> g_applications;
    std::map<std::wstring, SavedOpacity> g_savedOpacity;
    std::map<HWND, AppliedOpacity> g_appliedOpacity;
    std::wstring g_settingsPath;
    std::wstring g_logPath;
    bool g_updatingList = false;
    int g_editingRestoreRow = -1;
    bool g_darkTheme = false;
    COLORREF g_themeBackground = RGB(255, 255, 255);
    COLORREF g_themeText = RGB(0, 0, 0);
    HBRUSH g_themeBrush = nullptr;

    void CALLBACK WindowEventCallback(
        HWINEVENTHOOK, DWORD, HWND window, LONG objectId, LONG childId, DWORD, DWORD)
    {
        if (window == nullptr || objectId != OBJID_WINDOW || childId != CHILDID_SELF)
        {
            return;
        }

        PostMessageW(g_mainWindow, WindowEventMessage, 0, 0);
    }

    void LogMessage(const std::wstring& message)
    {
        if (g_logPath.empty())
        {
            OutputDebugStringW((L"Glazr: " + message + L"\n").c_str());
            return;
        }

        SYSTEMTIME time{};
        GetLocalTime(&time);
        wchar_t timestamp[32]{};
        swprintf_s(timestamp, L"%04u-%02u-%02u %02u:%02u:%02u.%03u ",
            time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute,
            time.wSecond, time.wMilliseconds);
        const std::wstring line = std::wstring(timestamp) + message + L"\r\n";
        const int required = WideCharToMultiByte(
            CP_UTF8, 0, line.data(), static_cast<int>(line.size()),
            nullptr, 0, nullptr, nullptr);
        if (required <= 0)
        {
            OutputDebugStringW((L"Glazr: " + message + L"\n").c_str());
            return;
        }

        std::vector<char> utf8(static_cast<size_t>(required));
        if (WideCharToMultiByte(CP_UTF8, 0, line.data(), static_cast<int>(line.size()),
                utf8.data(), required, nullptr, nullptr) != required)
        {
            OutputDebugStringW((L"Glazr: " + message + L"\n").c_str());
            return;
        }

        HANDLE file = CreateFileW(g_logPath.c_str(), FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            OutputDebugStringW((L"Glazr: " + message + L"\n").c_str());
            return;
        }

        DWORD bytesWritten = 0;
        const BOOL written = WriteFile(file, utf8.data(),
            static_cast<DWORD>(utf8.size()), &bytesWritten, nullptr);
        CloseHandle(file);
        if (!written || bytesWritten != utf8.size())
        {
            OutputDebugStringW((L"Glazr: " + message + L"\n").c_str());
        }
    }

    LRESULT CALLBACK HeaderWindowProcedure(
        HWND header, UINT message, WPARAM wParam, LPARAM lParam,
        UINT_PTR subclassId, DWORD_PTR referenceData)
    {
        if (message == WM_ERASEBKGND && g_darkTheme)
        {
            RECT client{};
            GetClientRect(header, &client);
            HBRUSH brush = CreateSolidBrush(RGB(40, 40, 40));
            FillRect(reinterpret_cast<HDC>(wParam), &client, brush);
            DeleteObject(brush);
            return 1;
        }

        if (message == WM_PAINT && g_darkTheme)
        {
            PAINTSTRUCT paint{};
            HDC deviceContext = BeginPaint(header, &paint);
            RECT client{};
            GetClientRect(header, &client);
            HBRUSH backgroundBrush = CreateSolidBrush(RGB(40, 40, 40));
            FillRect(deviceContext, &client, backgroundBrush);
            DeleteObject(backgroundBrush);

            const int itemCount = Header_GetItemCount(header);
            for (int index = 0; index < itemCount; ++index)
            {
                RECT itemRect{};
                if (!Header_GetItemRect(header, index, &itemRect))
                {
                    continue;
                }

                HBRUSH separatorBrush = CreateSolidBrush(RGB(68, 68, 68));
                RECT separator = itemRect;
                separator.left = separator.right - 1;
                FillRect(deviceContext, &separator, separatorBrush);
                separator = itemRect;
                separator.top = separator.bottom - 1;
                FillRect(deviceContext, &separator, separatorBrush);
                DeleteObject(separatorBrush);

                wchar_t title[512]{};
                HDITEMW item{};
                item.mask = HDI_TEXT | HDI_FORMAT;
                item.pszText = title;
                item.cchTextMax = static_cast<int>(_countof(title));
                if (Header_GetItem(header, index, &item))
                {
                    RECT textRect = itemRect;
                    textRect.left += 8;
                    textRect.right -= 8;
                    SetBkMode(deviceContext, TRANSPARENT);
                    SetTextColor(deviceContext, RGB(235, 235, 235));
                    UINT format = DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX;
                    if ((item.fmt & HDF_RIGHT) != 0)
                    {
                        format |= DT_RIGHT;
                    }
                    else if ((item.fmt & HDF_CENTER) != 0)
                    {
                        format |= DT_CENTER;
                    }
                    else
                    {
                        format |= DT_LEFT;
                    }
                    DrawTextW(deviceContext, title, -1, &textRect, format);
                }
            }

            EndPaint(header, &paint);
            return 0;
        }

        if (message == WM_THEMECHANGED || message == WM_SETTINGCHANGE ||
            message == WM_SYSCOLORCHANGE)
        {
            InvalidateRect(header, nullptr, TRUE);
        }
        else if (message == WM_NCDESTROY)
        {
            RemoveWindowSubclass(header, HeaderWindowProcedure, subclassId);
        }

        return DefSubclassProc(header, message, wParam, lParam);
    }

    bool IsDarkThemeEnabled()
    {
        DWORD useLightTheme = 1;
        DWORD valueSize = sizeof(useLightTheme);
        const LSTATUS result = RegGetValueW(
            HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &useLightTheme, &valueSize);
        return result == ERROR_SUCCESS && useLightTheme == 0;
    }

    void ApplyWindowTheme(HWND window)
    {
        g_darkTheme = IsDarkThemeEnabled();
        g_themeBackground = g_darkTheme ? RGB(32, 32, 32) : GetSysColor(COLOR_WINDOW);
        g_themeText = g_darkTheme ? RGB(243, 243, 243) : GetSysColor(COLOR_WINDOWTEXT);

        if (g_themeBrush != nullptr)
        {
            DeleteObject(g_themeBrush);
        }
        g_themeBrush = CreateSolidBrush(g_themeBackground);

        SetWindowTheme(g_applicationList, g_darkTheme ? L"DarkMode_Explorer" : nullptr, nullptr);
        SetWindowTheme(g_opacitySlider, g_darkTheme ? L"DarkMode_Explorer" : nullptr, nullptr);
        // Refresh button removed; no theme to apply for it
        if (g_helpButton != nullptr)
        {
            SetWindowTheme(g_helpButton, g_darkTheme ? L"DarkMode_Explorer" : nullptr, nullptr);
        }
        SetWindowTheme(g_restoreMode, g_darkTheme ? L"DarkMode_Explorer" : nullptr, nullptr);

        const BOOL useDarkTitleBar = g_darkTheme ? TRUE : FALSE;
        HRESULT result = DwmSetWindowAttribute(
            window, 20, &useDarkTitleBar, sizeof(useDarkTitleBar));
        if (FAILED(result))
        {
            DwmSetWindowAttribute(window, 19, &useDarkTitleBar, sizeof(useDarkTitleBar));
        }

        ListView_SetBkColor(g_applicationList, g_themeBackground);
        ListView_SetTextBkColor(g_applicationList, g_themeBackground);
        ListView_SetTextColor(g_applicationList, g_themeText);
        ListView_SetExtendedListViewStyleEx(
            g_applicationList, LVS_EX_GRIDLINES, g_darkTheme ? 0 : LVS_EX_GRIDLINES);

        RedrawWindow(window, nullptr, nullptr,
            RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
        RedrawWindow(ListView_GetHeader(g_applicationList), nullptr, nullptr,
            RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
    }

    // DrawRefreshButton removed with Refresh control

    std::wstring NormalizePath(const std::wstring& path)
    {
        std::wstring key = path;
        if (!key.empty())
        {
            CharLowerBuffW(&key[0], static_cast<DWORD>(key.size()));
        }
        return key;
    }

    std::wstring GetFileName(const std::wstring& path)
    {
        const size_t separator = path.find_last_of(L"\\/");
        return separator == std::wstring::npos ? path : path.substr(separator + 1);
    }

    bool GetSettingsPath()
    {
        const DWORD required = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
        if (required == 0)
        {
            return false;
        }

        std::vector<wchar_t> localAppData(required);
        if (GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData.data(), required) == 0)
        {
            return false;
        }

        std::wstring directory(localAppData.data());
        directory += L"\\Glazr";
        if (!CreateDirectoryW(directory.c_str(), nullptr) &&
            GetLastError() != ERROR_ALREADY_EXISTS)
        {
            return false;
        }

        g_settingsPath = directory + L"\\settings.ini";
        g_logPath = directory + L"\\glazr.log";
        LogMessage(L"Logger initialized. Log file: " + g_logPath);
        return true;
    }

    bool LoadSettings()
    {
        std::vector<wchar_t> section(65536);
        const DWORD length = GetPrivateProfileSectionW(
            L"Applications", section.data(), static_cast<DWORD>(section.size()),
            g_settingsPath.c_str());
        if (length >= section.size() - 2)
        {
            return false;
        }

        for (const wchar_t* entry = section.data(); *entry != L'\0'; entry += wcslen(entry) + 1)
        {
            const std::wstring value(entry);
            const size_t equals = value.find(L'=');
            const size_t separator = value.find(L'|', equals == std::wstring::npos ? 0 : equals + 1);
            if (equals == std::wstring::npos || separator == std::wstring::npos)
            {
                continue;
            }

            wchar_t* end = nullptr;
            const long opacity = wcstol(value.c_str() + equals + 1, &end, 10);
            if (end == value.c_str() + equals + 1 || end != value.c_str() + separator ||
                opacity < 10 || opacity > 100)
            {
                continue;
            }

            const std::wstring path = value.substr(separator + 1);
            if (!path.empty())
            {
                g_savedOpacity[NormalizePath(path)] =
                    { path, static_cast<int>(opacity), true, {} };
            }
        }
        LogMessage(L"Loaded " + std::to_wstring(g_savedOpacity.size()) +
            L" persistent opacity setting(s) from " + g_settingsPath);
        return true;
    }

    std::wstring GetWindowsErrorMessage(DWORD error);

    DWORD SaveSettings()
    {
        const std::wstring temporaryPath = g_settingsPath + L".tmp";
        std::wstring contents = L"\xFEFF[Applications]\r\n";

        unsigned int index = 0;
        for (const auto& entry : g_savedOpacity)
        {
            if (!entry.second.always)
            {
                continue;
            }
            const std::wstring key = std::to_wstring(index++);
            const std::wstring value = std::to_wstring(entry.second.percentage) + L"|" + entry.second.path;
            contents += key + L"=" + value + L"\r\n";
        }

        HANDLE file = CreateFileW(temporaryPath.c_str(), GENERIC_WRITE, 0, nullptr,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            const DWORD error = GetLastError();
            LogMessage(L"Settings save failed opening " + temporaryPath + L": " +
                GetWindowsErrorMessage(error) + L" (" + std::to_wstring(error) + L").");
            return error;
        }

        const DWORD byteCount = static_cast<DWORD>(contents.size() * sizeof(wchar_t));
        DWORD bytesWritten = 0;
        DWORD error = ERROR_SUCCESS;
        if (!WriteFile(file, contents.data(), byteCount, &bytesWritten, nullptr))
        {
            error = GetLastError();
        }
        else if (bytesWritten != byteCount)
        {
            error = ERROR_WRITE_FAULT;
        }
        else if (!FlushFileBuffers(file))
        {
            error = GetLastError();
        }

        if (!CloseHandle(file) && error == ERROR_SUCCESS)
        {
            error = GetLastError();
        }

        if (error == ERROR_SUCCESS &&
            !MoveFileExW(temporaryPath.c_str(), g_settingsPath.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            error = GetLastError();
        }

        if (error != ERROR_SUCCESS)
        {
            DeleteFileW(temporaryPath.c_str());
            LogMessage(L"Settings save failed for " + g_settingsPath + L": " +
                GetWindowsErrorMessage(error) + L" (" + std::to_wstring(error) + L").");
        }
        else
        {
            LogMessage(L"Settings saved to " + g_settingsPath + L".");
        }
        return error;
    }

    std::wstring GetWindowsErrorMessage(DWORD error)
    {
        wchar_t* messageBuffer = nullptr;
        const DWORD length = FormatMessageW(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr, error, 0, reinterpret_cast<LPWSTR>(&messageBuffer), 0, nullptr);
        std::wstring message = length != 0 && messageBuffer != nullptr
            ? std::wstring(messageBuffer, length) : L"Unknown Windows error.";
        if (messageBuffer != nullptr)
        {
            LocalFree(messageBuffer);
        }
        while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n' ||
            message.back() == L' '))
        {
            message.pop_back();
        }
        return message;
    }

    std::wstring GetProcessPath(DWORD processId)
    {
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
        if (process == nullptr)
        {
            return {};
        }

        std::vector<wchar_t> buffer(32768);
        DWORD length = static_cast<DWORD>(buffer.size());
        const BOOL success = QueryFullProcessImageNameW(process, 0, buffer.data(), &length);
        CloseHandle(process);
        return success ? std::wstring(buffer.data(), length) : std::wstring();
    }

    BOOL CALLBACK EnumerateApplicationWindows(HWND window, LPARAM parameter)
    {
        auto* applications = reinterpret_cast<std::vector<Application>*>(parameter);
        if (!IsWindowVisible(window) || GetWindow(window, GW_OWNER) != nullptr)
        {
            return TRUE;
        }

        const LONG_PTR extendedStyle = GetWindowLongPtrW(window, GWL_EXSTYLE);
        if ((extendedStyle & WS_EX_TOOLWINDOW) != 0)
        {
            return TRUE;
        }

        DWORD processId = 0;
        GetWindowThreadProcessId(window, &processId);
        if (processId == 0)
        {
            return TRUE;
        }

        const std::wstring path = GetProcessPath(processId);
        if (path.empty())
        {
            return TRUE;
        }

        const int titleLength = GetWindowTextLengthW(window);
        std::vector<wchar_t> titleBuffer(static_cast<size_t>(titleLength) + 1);
        GetWindowTextW(window, titleBuffer.data(), static_cast<int>(titleBuffer.size()));
        const std::wstring title(titleBuffer.data());
        if (title.empty())
        {
            return TRUE;
        }

        const std::wstring key = NormalizePath(path);
        auto application = std::find_if(
            applications->begin(), applications->end(),
            [&key](const Application& candidate) { return candidate.key == key; });
        if (application == applications->end())
        {
            Application entry;
            entry.path = path;
            entry.key = key;
            entry.name = GetFileName(path);
            entry.titles = title;
            entry.windows.push_back(window);
            entry.processIds.insert(processId);
            applications->push_back(std::move(entry));
        }
        else
        {
            application->windows.push_back(window);
            application->processIds.insert(processId);
            if (application->titles.find(title) == std::wstring::npos)
            {
                application->titles += L" | " + title;
            }
        }
        return TRUE;
    }

    BYTE PercentageToAlpha(int percentage)
    {
        return static_cast<BYTE>((percentage * 255 + 50) / 100);
    }

    bool ApplyOpacity(HWND window, BYTE alpha)
    {
        LONG_PTR style = GetWindowLongPtrW(window, GWL_EXSTYLE);
        if ((style & WS_EX_LAYERED) == 0)
        {
            SetLastError(ERROR_SUCCESS);
            const LONG_PTR result = SetWindowLongPtrW(window, GWL_EXSTYLE, style | WS_EX_LAYERED);
            if (result == 0 && GetLastError() != ERROR_SUCCESS)
            {
                return false;
            }
            if (!SetWindowPos(window, nullptr, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED))
            {
                return false;
            }
        }

        return SetLayeredWindowAttributes(window, 0, alpha, LWA_ALPHA) != FALSE;
    }

    std::wstring GetSelectedApplicationKey()
    {
        const int selected = ListView_GetNextItem(g_applicationList, -1, LVNI_SELECTED);
        if (selected < 0 || static_cast<size_t>(selected) >= g_applications.size())
        {
            return {};
        }
        return g_applications[static_cast<size_t>(selected)].key;
    }

    void SetStatus(const std::wstring& message)
    {
        SetWindowTextW(g_statusLabel, message.c_str());
    }

    void UpdateSelectedApplication()
    {
        const int selected = ListView_GetNextItem(g_applicationList, -1, LVNI_SELECTED);
        const bool hasSelection =
            selected >= 0 && static_cast<size_t>(selected) < g_applications.size();
        EnableWindow(g_opacitySlider, hasSelection);

        if (!hasSelection)
        {
            SetWindowTextW(g_opacityLabel, L"--%");
            return;
        }

        const Application& application = g_applications[static_cast<size_t>(selected)];
        const auto saved = g_savedOpacity.find(application.key);
        const int percentage = saved == g_savedOpacity.end() ? 100 : saved->second.percentage;
        SendMessageW(g_opacitySlider, TBM_SETPOS, TRUE, percentage);
        const std::wstring label = std::to_wstring(percentage) + L"%";
        SetWindowTextW(g_opacityLabel, label.c_str());
    }

    void RefreshApplications()
    {
        const std::wstring selectedKey = GetSelectedApplicationKey();
        std::vector<Application> applications;
        EnumWindows(EnumerateApplicationWindows, reinterpret_cast<LPARAM>(&applications));

        for (auto setting = g_savedOpacity.begin(); setting != g_savedOpacity.end();)
        {
            if (!setting->second.always)
            {
                const auto application = std::find_if(
                    applications.begin(), applications.end(),
                    [&setting](const Application& candidate)
                    {
                        return candidate.key == setting->first;
                    });
                const bool sameProcessStillVisible = application != applications.end() &&
                    std::any_of(application->processIds.begin(), application->processIds.end(),
                        [&setting](DWORD processId)
                        {
                            return setting->second.processIds.find(processId) !=
                                setting->second.processIds.end();
                        });
                if (!sameProcessStillVisible)
                {
                    setting = g_savedOpacity.erase(setting);
                    continue;
                }
            }
            ++setting;
        }

        bool hadApplyError = false;
        for (const Application& application : applications)
        {
            const auto saved = g_savedOpacity.find(application.key);
            if (saved == g_savedOpacity.end())
            {
                continue;
            }

            const BYTE alpha = PercentageToAlpha(saved->second.percentage);
            for (HWND window : application.windows)
            {
                DWORD processId = 0;
                GetWindowThreadProcessId(window, &processId);
                if (!saved->second.always &&
                    saved->second.processIds.find(processId) == saved->second.processIds.end())
                {
                    continue;
                }

                const auto applied = g_appliedOpacity.find(window);
                if (applied != g_appliedOpacity.end() &&
                    applied->second.key == application.key && applied->second.alpha == alpha)
                {
                    continue;
                }

                if (ApplyOpacity(window, alpha))
                {
                    g_appliedOpacity[window] = { application.key, alpha };
                }
                else
                {
                    hadApplyError = true;
                }
            }
        }

        for (auto item = g_appliedOpacity.begin(); item != g_appliedOpacity.end();)
        {
            if (!IsWindow(item->first))
            {
                item = g_appliedOpacity.erase(item);
            }
            else
            {
                ++item;
            }
        }

        std::sort(applications.begin(), applications.end(),
            [](const Application& left, const Application& right)
            {
                return left.name < right.name;
            });
        g_applications = std::move(applications);

        g_updatingList = true;
        ListView_DeleteAllItems(g_applicationList);
        int selectedIndex = -1;
        for (size_t index = 0; index < g_applications.size(); ++index)
        {
            const Application& application = g_applications[index];
            const auto saved = g_savedOpacity.find(application.key);
            const std::wstring percentage = std::to_wstring(
                saved == g_savedOpacity.end() ? 100 : saved->second.percentage) + L"%";

            LVITEMW item{};
            item.mask = LVIF_TEXT;
            item.iItem = static_cast<int>(index);
            item.pszText = const_cast<LPWSTR>(application.name.c_str());
            ListView_InsertItem(g_applicationList, &item);
            ListView_SetItemText(g_applicationList, static_cast<int>(index), 1,
                const_cast<LPWSTR>(application.titles.c_str()));
            ListView_SetItemText(g_applicationList, static_cast<int>(index), 2,
                const_cast<LPWSTR>(percentage.c_str()));
            const wchar_t* restoreMode = saved != g_savedOpacity.end() && saved->second.always
                ? L"Always" : L"Once";
            ListView_SetItemText(g_applicationList, static_cast<int>(index), 3,
                const_cast<LPWSTR>(restoreMode));

            if (application.key == selectedKey)
            {
                selectedIndex = static_cast<int>(index);
            }
        }

        if (selectedIndex < 0 && !g_applications.empty())
        {
            selectedIndex = 0;
        }
        if (selectedIndex >= 0)
        {
            ListView_SetItemState(g_applicationList, selectedIndex,
                LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        }
        g_updatingList = false;

        UpdateSelectedApplication();
        if (hadApplyError)
        {
            SetStatus(L"Could not change the opacity of one or more windows.");
        }
        else
        {
            SetStatus(g_applications.empty()
                ? L"No visible applications found."
                : L"Application list is up to date.");
        }
    }

    void UpdateApplicationOpacity(bool persist)
    {
        const int selected = ListView_GetNextItem(g_applicationList, -1, LVNI_SELECTED);
        if (selected < 0 || static_cast<size_t>(selected) >= g_applications.size())
        {
            return;
        }

        const int percentage = static_cast<int>(SendMessageW(g_opacitySlider, TBM_GETPOS, 0, 0));
        Application& application = g_applications[static_cast<size_t>(selected)];
        auto& setting = g_savedOpacity[application.key];
        const bool always = setting.always;
        setting.path = application.path;
        setting.percentage = percentage;
        if (always)
        {
            setting.processIds.clear();
        }
        else if (setting.processIds.empty())
        {
            setting.processIds = application.processIds;
        }
        const BYTE alpha = PercentageToAlpha(percentage);

        bool hadApplyError = false;
        for (HWND window : application.windows)
        {
            DWORD processId = 0;
            GetWindowThreadProcessId(window, &processId);
            if (!always && setting.processIds.find(processId) == setting.processIds.end())
            {
                continue;
            }

            if (ApplyOpacity(window, alpha))
            {
                g_appliedOpacity[window] = { application.key, alpha };
            }
            else
            {
                g_appliedOpacity.erase(window);
                hadApplyError = true;
            }
        }

        const std::wstring label = std::to_wstring(percentage) + L"%";
        SetWindowTextW(g_opacityLabel, label.c_str());
        ListView_SetItemText(g_applicationList, selected, 2,
            const_cast<LPWSTR>(label.c_str()));

        const DWORD saveError = persist && always ? SaveSettings() : ERROR_SUCCESS;
        if (saveError != ERROR_SUCCESS)
        {
            LogMessage(L"Could not persist opacity for " + application.path + L": " +
                GetWindowsErrorMessage(saveError) + L" (" +
                std::to_wstring(saveError) + L").");
            SetStatus(L"Could not save settings: " + GetWindowsErrorMessage(saveError));
        }
        else if (hadApplyError)
        {
            LogMessage(L"Failed to apply opacity to one or more windows for " + application.path + L".");
            SetStatus(L"Could not change the opacity of one or more windows.");
        }
        else
        {
            LogMessage(L"Opacity set to " + std::to_wstring(percentage) + L"% for " +
                application.path + (always ? L" (Always)." : L" (Once)."));
            SetStatus(always
                ? L"Setting saved and will also apply to future windows from this application."
                : L"Opacity applies only to the current application session and is not saved.");
        }
    }

    void BeginRestoreModeEdit(int row)
    {
        if (row < 0 || static_cast<size_t>(row) >= g_applications.size())
        {
            return;
        }

        RECT cell{};
        if (!ListView_GetSubItemRect(g_applicationList, row, 3, LVIR_BOUNDS, &cell))
        {
            return;
        }

        const int width = cell.right - cell.left;
        const int height = cell.bottom - cell.top;
        MapWindowPoints(g_applicationList, g_mainWindow,
            reinterpret_cast<POINT*>(&cell), 2);
        const RECT comboRect{ cell.left, cell.top, cell.left + width,
            cell.top + (std::max)(height, 180) };
        MoveWindow(g_restoreMode, comboRect.left, comboRect.top,
            comboRect.right - comboRect.left, comboRect.bottom - comboRect.top, TRUE);

        const Application& application = g_applications[static_cast<size_t>(row)];
        const auto existingSetting = g_savedOpacity.find(application.key);
        const bool always = existingSetting != g_savedOpacity.end() && existingSetting->second.always;
        SendMessageW(g_restoreMode, CB_SETCURSEL, always ? 0 : 1, 0);
        g_editingRestoreRow = row;
        ShowWindow(g_restoreMode, SW_SHOW);
        SetFocus(g_restoreMode);
        SendMessageW(g_restoreMode, CB_SHOWDROPDOWN, TRUE, 0);
    }

    void UpdateRestoreMode()
    {
        const int row = g_editingRestoreRow;
        g_editingRestoreRow = -1;
        ShowWindow(g_restoreMode, SW_HIDE);
        SetFocus(g_applicationList);

        if (row < 0 || static_cast<size_t>(row) >= g_applications.size())
        {
            return;
        }

        const Application& application = g_applications[static_cast<size_t>(row)];
        const auto existingSetting = g_savedOpacity.find(application.key);
        const bool always = SendMessageW(g_restoreMode, CB_GETCURSEL, 0, 0) == 0;
        const int percentage = existingSetting == g_savedOpacity.end()
            ? 100 : existingSetting->second.percentage;
        const bool hadPreviousSetting = existingSetting != g_savedOpacity.end();
        const SavedOpacity previousSetting = hadPreviousSetting
            ? existingSetting->second : SavedOpacity{};
        auto& setting = g_savedOpacity[application.key];
        setting.path = application.path;
        setting.percentage = percentage;
        setting.always = always;
        setting.processIds = always ? std::set<DWORD>{} : application.processIds;
        LogMessage(L"Restore mode requested: " + std::wstring(always ? L"Always" : L"Once") +
            L" for " + application.path + L" at " + std::to_wstring(percentage) + L"%.");

        const DWORD saveError = SaveSettings();
        if (saveError != ERROR_SUCCESS)
        {
            if (hadPreviousSetting)
            {
                g_savedOpacity[application.key] = previousSetting;
            }
            else
            {
                g_savedOpacity.erase(application.key);
            }
            const wchar_t* previousMode = hadPreviousSetting && previousSetting.always
                ? L"Always" : L"Once";
            ListView_SetItemText(g_applicationList, row, 3, const_cast<LPWSTR>(previousMode));
            SendMessageW(g_restoreMode, CB_SETCURSEL,
                hadPreviousSetting && previousSetting.always ? 0 : 1, 0);
            LogMessage(L"Restore mode change rolled back for " + application.path + L".");
            SetStatus(L"Could not save restore mode: " + GetWindowsErrorMessage(saveError));
        }
        else
        {
            const wchar_t* modeLabel = always ? L"Always" : L"Once";
            ListView_SetItemText(g_applicationList, row, 3, const_cast<LPWSTR>(modeLabel));
            SetStatus(always
                ? L"Opacity saved for future restarts. Keep Glazr running while the application restarts."
                : L"Opacity applies only until the application closes; it will not be restored after restart.");
        }
    }

    void LayoutControls(HWND window)
    {
        RECT client{};
        GetClientRect(window, &client);
        const int width = client.right - client.left;
        const int height = client.bottom - client.top;
        const int margin = 12;
        const int controlsTop = height - 82;

        MoveWindow(g_applicationList, margin, margin, width - margin * 2,
            controlsTop - margin * 2, TRUE);
        MoveWindow(GetDlgItem(window, IdOpacityPrompt), margin, controlsTop + 7, 270, 24, TRUE);
        MoveWindow(g_opacitySlider, 290, controlsTop, width - 430, 35, TRUE);
        MoveWindow(g_opacityLabel, width - 125, controlsTop + 5, 48, 24, TRUE);
        // Reserve space on the right for the help button so it doesn't get covered by the status label
        const int helpWidth = 28;
        MoveWindow(g_statusLabel, margin, height - 34, width - margin * 2 - (helpWidth + 8), 24, TRUE);
        MoveWindow(g_helpButton, width - margin - helpWidth, height - 34, helpWidth, 24, TRUE);
        // Ensure help button is on top of status label
        SetWindowPos(g_helpButton, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    }

    LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
        case WM_CREATE:
        {
            g_mainWindow = window;
            g_applicationList = CreateWindowExW(
                WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                0, 0, 0, 0, window,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IdApplicationList)),
                GetModuleHandleW(nullptr), nullptr);
            ListView_SetExtendedListViewStyle(g_applicationList,
                LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
            if (!SetWindowSubclass(ListView_GetHeader(g_applicationList),
                    HeaderWindowProcedure, 1, 0))
            {
                return -1;
            }

            const wchar_t* columnNames[] = { L"Application", L"Window", L"Opacity", L"Restore" };
            const int columnWidths[] = { 190, 370, 110, 100 };
            for (int column = 0; column < 4; ++column)
            {
                LVCOLUMNW definition{};
                definition.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
                definition.pszText = const_cast<LPWSTR>(columnNames[column]);
                definition.cx = columnWidths[column];
                definition.iSubItem = column;
                ListView_InsertColumn(g_applicationList, column, &definition);
            }

            CreateWindowExW(0, L"STATIC", L"Selected application opacity:",
                WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IdOpacityPrompt)),
                GetModuleHandleW(nullptr), nullptr);
            g_opacitySlider = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
                WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS | TBS_HORZ,
                0, 0, 0, 0, window,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IdOpacitySlider)),
                GetModuleHandleW(nullptr), nullptr);
            SendMessageW(g_opacitySlider, TBM_SETRANGE, TRUE, MAKELONG(10, 100));
            SendMessageW(g_opacitySlider, TBM_SETTICFREQ, 10, 0);
            g_opacityLabel = CreateWindowExW(0, L"STATIC", L"100%",
                WS_CHILD | WS_VISIBLE | SS_RIGHT, 0, 0, 0, 0, window,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IdOpacityLabel)),
                GetModuleHandleW(nullptr), nullptr);
            g_restoreMode = CreateWindowExW(0, WC_COMBOBOXW, L"",
                WS_CHILD | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                0, 0, 0, 0, window,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IdRestoreMode)),
                GetModuleHandleW(nullptr), nullptr);
            SendMessageW(g_restoreMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Always"));
            SendMessageW(g_restoreMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Once"));
            g_statusLabel = CreateWindowExW(0, L"STATIC", L"",
                WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 0, 0, window,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IdStatusLabel)),
                GetModuleHandleW(nullptr), nullptr);
            g_helpButton = CreateWindowExW(0, L"BUTTON", L"?",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0, window,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IdHelpButton)),
                GetModuleHandleW(nullptr), nullptr);

            if (g_applicationList == nullptr || g_opacitySlider == nullptr ||
                g_opacityLabel == nullptr || g_restoreMode == nullptr || g_statusLabel == nullptr ||
                g_helpButton == nullptr)
            {
                return -1;
            }

            ApplyWindowTheme(window);
            LayoutControls(window);
            RefreshApplications();
            g_windowEventHook = SetWinEventHook(
                EVENT_OBJECT_CREATE, EVENT_OBJECT_SHOW, nullptr, WindowEventCallback,
                0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
            if (g_windowEventHook == nullptr)
            {
                const DWORD error = GetLastError();
                LogMessage(L"Could not install window event hook; using periodic refresh only. " +
                    GetWindowsErrorMessage(error) + L" (" + std::to_wstring(error) + L").");
            }
            else
            {
                LogMessage(L"Installed window create/show event hook.");
            }
            SetTimer(window, RefreshTimerId, RefreshIntervalMs, nullptr);
            return 0;
        }
        case WM_ERASEBKGND:
        {
            RECT client{};
            GetClientRect(window, &client);
            FillRect(reinterpret_cast<HDC>(wParam), &client, g_themeBrush);
            return 1;
        }
        case WM_CTLCOLORSTATIC:
        {
            HDC deviceContext = reinterpret_cast<HDC>(wParam);
            SetTextColor(deviceContext, g_themeText);
            SetBkMode(deviceContext, TRANSPARENT);
            return reinterpret_cast<LRESULT>(g_themeBrush);
        }
        // WM_DRAWITEM: removed owner-draw Refresh button handling (Refresh control removed)
        case WM_SETTINGCHANGE:
        case WM_THEMECHANGED:
        case WM_SYSCOLORCHANGE:
            ApplyWindowTheme(window);
            return 0;
        case WM_SIZE:
            if (g_restoreMode != nullptr && g_editingRestoreRow >= 0)
            {
                g_editingRestoreRow = -1;
                ShowWindow(g_restoreMode, SW_HIDE);
            }
            LayoutControls(window);
            return 0;
        case WM_TIMER:
            if (wParam == RefreshTimerId)
            {
                RefreshApplications();
            }
            else if (wParam == EventRefreshTimerId)
            {
                KillTimer(window, EventRefreshTimerId);
                RefreshApplications();
            }
            return 0;
        case WindowEventMessage:
            SetTimer(window, EventRefreshTimerId, EventRefreshDelayMs, nullptr);
            return 0;
        case WM_HSCROLL:
            if (reinterpret_cast<HWND>(lParam) == g_opacitySlider)
            {
                UpdateApplicationOpacity(LOWORD(wParam) != TB_THUMBTRACK);
            }
            return 0;
        case WM_GETMINMAXINFO:
        {
            auto* limits = reinterpret_cast<MINMAXINFO*>(lParam);
            limits->ptMinTrackSize.x = 700;
            limits->ptMinTrackSize.y = 450;
            return 0;
        }
        case WM_NOTIFY:
        {
            const auto* notification = reinterpret_cast<NMHDR*>(lParam);
            if (notification->hwndFrom == g_applicationList &&
                notification->code == LVN_ITEMCHANGED && !g_updatingList)
            {
                UpdateSelectedApplication();
            }
            else if (notification->hwndFrom == g_applicationList &&
                notification->code == NM_CLICK)
            {
                const auto* hit = reinterpret_cast<NMITEMACTIVATE*>(lParam);
                if (hit->iSubItem == 3)
                {
                    BeginRestoreModeEdit(hit->iItem);
                }
            }
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == IdHelpButton)
            {
                MessageBoxW(window,
                    L"Glazr - Windows opacity changer.\nby Patu^Xenium.\n\nGithub sources: https://github.com/patuwwy/opacity",
                    L"Glazr", MB_OK | MB_ICONINFORMATION);
            }
            else if (LOWORD(wParam) == IdRestoreMode &&
                HIWORD(wParam) == CBN_SELENDOK)
            {
                UpdateRestoreMode();
            }
            else if (LOWORD(wParam) == IdRestoreMode &&
                (HIWORD(wParam) == CBN_CLOSEUP || HIWORD(wParam) == CBN_SELENDCANCEL) &&
                g_editingRestoreRow >= 0)
            {
                g_editingRestoreRow = -1;
                ShowWindow(g_restoreMode, SW_HIDE);
                SetFocus(g_applicationList);
            }
            return 0;
        case WM_DESTROY:
            KillTimer(window, RefreshTimerId);
            KillTimer(window, EventRefreshTimerId);
            if (g_windowEventHook != nullptr)
            {
                UnhookWinEvent(g_windowEventHook);
                g_windowEventHook = nullptr;
            }
            if (g_themeBrush != nullptr)
            {
                DeleteObject(g_themeBrush);
                g_themeBrush = nullptr;
            }
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
    INITCOMMONCONTROLSEX commonControls{ sizeof(commonControls), ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES };
    InitCommonControlsEx(&commonControls);

    if (!GetSettingsPath() || !LoadSettings())
    {
        LogMessage(L"Startup failed while preparing or reading application settings.");
        MessageBoxW(nullptr,
            L"Could not read or prepare the settings file in LocalAppData.",
            L"Glazr", MB_OK | MB_ICONERROR);
        return 1;
    }

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = WindowProcedure;
    windowClass.hInstance = instance;
    windowClass.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP_ICON));
    windowClass.hIconSm = reinterpret_cast<HICON>(
        LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR));
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = L"GlazrMainWindow";
    if (RegisterClassExW(&windowClass) == 0)
    {
        MessageBoxW(nullptr, L"Could not register the window class.", L"Glazr",
            MB_OK | MB_ICONERROR);
        return 1;
    }

    HWND window = CreateWindowExW(0, windowClass.lpszClassName,
        L"Glazr",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 900, 570,
        nullptr, nullptr, instance, nullptr);
    if (window == nullptr)
    {
        MessageBoxW(nullptr, L"Could not create the application window.", L"Glazr",
            MB_OK | MB_ICONERROR);
        return 1;
    }

    LogMessage(L"Application started. Process ID: " +
        std::to_wstring(GetCurrentProcessId()) + L".");
    ShowWindow(window, showCommand);
    UpdateWindow(window);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}
