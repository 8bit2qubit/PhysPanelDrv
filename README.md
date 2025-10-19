# PhysPanelDrv - Physical Panel Dimensions Override Driver

> 🌐 **English**

<p align="center">
  <a href="#"><img src="https://img.shields.io/badge/tech-C%20%26%20WDK-blueviolet.svg?style=flat-square" alt="Tech"></a>
  <a href="https://github.com/8bit2qubit/PhysPanelDrv/blob/main/LICENSE"><img src="https://img.shields.io/github/license/8bit2qubit/PhysPanelDrv" alt="License"></a>
</p>

A minimalist and lightweight Windows kernel driver designed to **override the physical display panel dimensions** reported to the operating system. This allows simulating different screen sizes (e.g., a handheld device) to influence UI behavior in certain applications and system components, such as the Windows Shell or DirectX games.

## ⚠️ WARNING: Please Read Before Proceeding

This is a **kernel-mode driver** that interacts with undocumented Windows internals. By using this software, you acknowledge and agree to the following:

*   **System Instability** – Kernel-mode code runs with the highest privilege. Any bugs or incompatibilities can cause system instability, crashes (Blue Screen of Death - BSOD), data corruption, or require a complete OS reinstallation.
*   **Use Entirely at Your Own Risk** – You are solely responsible for any consequences. The developer provides no warranty, support, or liability for any damages whatsoever.
*   **Security Implications** – Installing this driver requires disabling security features like Secure Boot and enabling Test Signing Mode, which can lower your system's security posture.
*   **Undocumented APIs** – This driver relies on internal Windows functions (`ZwUpdateWnfStateData`) that are not officially supported and may change or be removed in any Windows update, causing the driver to fail or crash the system.
*   **Backup Is Essential** – Always create a full system backup and a System Restore Point before installing any kernel-mode driver.

**If you are not comfortable with these risks, do not use this software.**

---

## ⚙️ System Requirements

*   **Operating System**: Windows 11 Build 26100 / 26200 / 26220 (x64)
*   **Permissions**: Administrator privileges are required for installation and removal.
*   **System State**: **Secure Boot** must be **disabled** and **Test Signing Mode** must be **enabled**.
*   **Required Tools**: The installation and removal process relies on `devcon.exe`, which is part of the Windows Driver Kit (WDK).

---

## 💻 Technical Details

*   **Language**: C
*   **Toolchain**: Windows Driver Kit (WDK)
*   **Core API**: The driver utilizes the undocumented kernel API `ZwUpdateWnfStateData` to publish a new value for a well-known Windows Notification Facility (WNF) state.
*   **WNF State Name**: `WNF_DX_INTERNAL_PANEL_DIMENSIONS`. This state is used internally by DirectX and other components to determine the physical size of the primary display panel.

---

## 🙏 Acknowledgements

This project is a practical implementation based on the research and discovery of the `WNF_DX_INTERNAL_PANEL_DIMENSIONS` state name by:

*   **[physpanel](https://github.com/riverar/physpanel)** by **@riverar**

A huge thank you for their invaluable contributions to the Windows internals community.

---

## 🛠️ Building from Source

1.  **Clone the Repository**
    ```bash
    git clone https://github.com/8bit2qubit/PhysPanelDrv.git
    cd PhysPanelDrv
    ```
2.  **Prerequisites**
    *   Install **Visual Studio**.
    *   Install the latest **Windows Driver Kit (WDK)**.
3.  **Configure Dimensions & Build**
    *   Open `PhysPanelDrv/Driver.c` and modify the `widthMm` and `heightMm` variables as needed.
    *   Open `PhysPanelDrv.sln` in Visual Studio, select the `Release` and `x64` configuration, and build the solution.

---

## 📄 License

This project is licensed under the [GNU General Public License v3.0 (GPL-3.0)](https://github.com/8bit2qubit/PhysPanelDrv/blob/main/LICENSE).

This means you are free to use, modify, and distribute this software, but any derivative works based on this project must also be distributed under the **same GPL-3.0 license and provide the complete source code**. For more details, please see the [official GPL-3.0 terms](https://www.gnu.org/licenses/gpl-3.0.html).