# Glazr

**Glazr** is a lightweight native Windows utility written in C++ (Win32 API) that lets you dynamically adjust the opacity (alpha transparency) of top-level application windows.

It allows you to modify transparency in real-time — either temporarily for the current session or persistently so that settings are remembered and re-applied whenever the application runs.

---

## Key Features

- **Real-Time Opacity Control:** Smoothly tweak window transparency from 10% to 100% using a trackbar slider.
- **Two Persistence Modes (Restore column):**
  - `Once` – Applies opacity only to the running application session; setting is forgotten once closed.
  - `Always` – Stores the target opacity in configuration and automatically reapplies it to all future windows spawned from that executable.
- **Automatic Window Detection:**
  - Hooked into Windows creation and show events via `SetWinEventHook` for near-instant detection of newly opened windows.
  - Background periodic polling timer (every 1.5 seconds) ensuring state accuracy even across untracked events.
- **System Dark Mode Support:**
  - Reads `AppsUseLightTheme` registry key to automatically mirror Windows light/dark theme preference.
  - Custom-styled ListView header, list background, DWM dark title bar, and themed controls.
- **Persistent Storage & Diagnostic Logging:**
  - Configuration saved in INI format at `%LOCALAPPDATA%\Glazr\settings.ini`.
  - Detailed diagnostic output saved at `%LOCALAPPDATA%\Glazr\glazr.log`.
- **Zero External Dependencies:** Built purely on Win32 API, DWM, UxTheme, and standard C++ libraries without heavy third-party UI runtimes.

---

## Requirements

- **Operating System:** Windows 10 / Windows 11 (recommended) or Windows 7 SP1+ (with DWM composition enabled).
- **Toolchain / Build Environment:**
  - Visual Studio 2019 (MSVC toolset `v142`), Visual Studio 2022 (`v143`), or newer.
  - Workload: **Desktop development with C++**.
  - Windows 10 / 11 SDK (e.g. 10.0.x).

> **Note for newer Visual Studio versions (VS 2022 / vNext):**  
> The project file is configured with the widely compatible `v142` toolset. When opening the solution in a newer Visual Studio version without the v142 toolset installed, you can simply right-click the solution or project and select **Retarget Projects** (or choose `v143` / newer in **Project Properties -> General -> Platform Toolset**).

---

## Building from Source

### Method 1: Visual Studio IDE (GUI)

1. Clone the repository:
   ```cmd
   git clone https://github.com/patuwwy/opacity.git
   cd opacity
   ```
2. Open the solution file [`Glazr.sln`](Glazr.sln) in Visual Studio.
3. Choose the target configuration in the toolbar:
   - **Configuration:** `Release` (or `Debug`)
   - **Platform:** `x64` (or `Win32`)
4. Build the solution:
   - Go to **Build** -> **Build Solution** (`Ctrl + Shift + B`).
5. The compiled binary will be located at:
   - `x64\Release\Glazr.exe` (for 64-bit builds)
   - `Release\Glazr.exe` (for 32-bit builds)

---

### Method 2: Command Line (MSBuild / Developer Command Prompt)

1. Open **Developer Command Prompt for VS 2022** or **Developer PowerShell for VS 2022**.
2. Navigate to the project root directory:
   ```cmd
   cd C:\path\to\Glazr
   ```
3. Run MSBuild to build the Release x64 binary:
   ```cmd
   msbuild Glazr.sln /p:Configuration=Release /p:Platform=x64
   ```
   _(For 32-bit architecture, use `/p:Platform=Win32` instead)_

---

## Usage

1. Launch `Glazr.exe`.
2. All running applications with visible top-level windows will be listed in the table.
3. **Change Opacity:** Select an application row from the list and drag the opacity slider at the bottom.
4. **Change Restore Mode:** Click on the cell in the **Restore** column next to the target application:
   - Select `Always` to keep the opacity permanently saved across application restarts.
   - Select `Once` if you want the opacity to revert back once the application exits.

> **Note:** Applications running with elevated Administrator privileges (such as Task Manager or elevated consoles) require Glazr to also be run **as Administrator** to allow adjusting their layered window attributes.

---

## Author & Links

- **Author:** Patu^Xenium
- **Repository:** [https://github.com/patuwwy/glazr](https://github.com/patuwwy/glazr)
