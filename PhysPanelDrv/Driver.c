// PhysPanelDrv - Physical Panel Dimensions Override Driver
// Copyright (C) 2025 8bit2qubit

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#include "driver.h"  // 專案的主標頭檔
#include <wdm.h>     // WDM (Windows Driver Model) 的核心標頭檔

//======================================================================
// 未公開的 WNF 核心 API 宣告 (Undocumented WNF Kernel API Declarations)
//
// 這段程式碼宣告了與 Windows Notification Facility (WNF) 互動所需的
// 未公開（undocumented）核心 API 型別與函式原型。
// WNF 是 Windows 內部一種輕量級的發布/訂閱 (pub/sub) 機制。
//======================================================================

// WNF 型別 ID (未公開)
typedef const struct _WNF_TYPE_ID* PCWNF_TYPE_ID;

// WNF 變更戳記 (未公開)
typedef ULONG WNF_CHANGE_STAMP, * PWNF_CHANGE_STAMP;

// 核心模式 API 函式原型宣告 (未公開)
EXTERN_C_START

NTSTATUS
NTAPI // NTAPI 是 Windows 核心 API 的標準呼叫約定
ZwUpdateWnfStateData(
    _In_ PCWNF_STATE_NAME StateName,
    _In_reads_bytes_opt_(Length) const VOID* Buffer,
    _In_opt_ ULONG Length,
    _In_opt_ PCWNF_TYPE_ID TypeId,
    _In_opt_ const PVOID ExplicitScope,
    _In_ WNF_CHANGE_STAMP MatchingChangeStamp,
    _In_ LOGICAL CheckStamp
);

EXTERN_C_END

//======================================================================
// 全域常數與函式原型 (Global Constants & Prototypes)
//======================================================================

// DirectX 內部顯示面板尺寸的 WNF ID。
// 這是一個由系統定義的內部常數，用於覆寫顯示器的實體尺寸。
// 使用 #define 來定義 WNF 狀態名稱的 ID。
#define WNF_DX_INTERNAL_PANEL_DIMENSIONS_ID { 0xA3BC4875, 0x41C61629 }

// 驅動程式進入點和卸載函式原型宣告
DRIVER_INITIALIZE DriverEntry;
DRIVER_UNLOAD DriverUnload;

//======================================================================
// DriverEntry - 驅動程式的主進入點
//
// 當 I/O 管理器載入此驅動程式時，會呼叫此函式。
// 這是驅動程式執行其初始化工作的起點。
//======================================================================

NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
)
{
    // 區域變數宣告
    NTSTATUS status;
    ULONGLONG dimensions;
    WNF_STATE_NAME wnfStateName = WNF_DX_INTERNAL_PANEL_DIMENSIONS_ID;

    // PAGED_CODE() 是一個在偵錯組建中進行執行時檢查的巨集。
    // 它會驗證這段程式碼是否在允許分頁錯誤的低 IRQL (PASSIVE_LEVEL) 下執行。
    PAGED_CODE();

    // 將驅動程式物件和註冊表路徑參數標記為未使用，以避免編譯器產生警告。
    // 在這個簡單的驅動程式中，不需要使用這些參數。
    UNREFERENCED_PARAMETER(RegistryPath);

    // KdPrintEx 用於將偵錯訊息輸出到核心偵錯器 (如 WinDbg)。
    // 這是核心模式開發中最重要的偵錯工具。
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "PhysPanelDrv: DriverEntry - Starting...\n"));

    // 設定想要的尺寸 (單位：公釐) 
    // 這裡直接硬式編碼 (hardcode) 所需的寬度和高度。
    const ULONG widthMm = 155;
    const ULONG heightMm = 87;

    // 將兩個 32 位元的 ULONG (寬度和高度) 封裝成一個 64 位元的 ULONGLONG。
    // 這是 WNF API 預期的資料格式。
    // 記憶體佈局: |<--- Height (32 bits) --->|<--- Width (32 bits) --->|
    dimensions = ((ULONGLONG)heightMm << 32) | widthMm;

    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "PhysPanelDrv: Setting dimensions to Width=%lu, Height=%lu\n", widthMm, heightMm));

    // 呼叫核心 WNF API (ZwUpdateWnfStateData) 來更新狀態資料。
    // 這會將設定的尺寸寫入到系統中。
    status = ZwUpdateWnfStateData(
        &wnfStateName,                      // 要更新的 WNF 狀態 ID
        &dimensions,                        // 指向包含新資料的緩衝區
        sizeof(dimensions),                 // 資料的長度 (8 bytes)
        NULL,                               // TypeId: 使用 NULL 表示預設型別
        NULL,                               // ExplicitScope: 使用 NULL 表示預設作用域
        0,                                  // MatchingChangeStamp: 條件式更新用的戳記，此處不需要
        FALSE                               // CheckStamp: FALSE 表示不檢查目前的 ChangeStamp，直接強制覆寫
    );

    // 檢查 API 呼叫是否成功
    if (!NT_SUCCESS(status)) {
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "PhysPanelDrv: ZwUpdateWnfStateData failed with status 0x%X\n", status));
        // 重要：即使 API 呼叫失敗，仍然回傳 STATUS_SUCCESS。
        // 這是一個關鍵的設計決策，因為這個驅動程式並非系統執行的必要元件。
        // 如果在這裡回傳失敗，可能會導致系統啟動過程中出現問題 (例如，如果此驅動程式被設定為開機啟動)。
        // 讓驅動程式「安靜地失敗」但成功載入，是更安全的選擇。
    }
    else
    {
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "PhysPanelDrv: Successfully updated panel dimensions.\n"));
    }

    // 工作在 DriverEntry 中就已完成。
    // 註冊一個卸載函式，以便 I/O 管理器知道如何在需要時安全地卸載此驅動程式。
    DriverObject->DriverUnload = DriverUnload;

    // 回傳成功，表示驅動程式已成功初始化並載入到記憶體中。
    return STATUS_SUCCESS;
}

//======================================================================
// DriverUnload - 當驅動程式被卸載時呼叫
//
// 這個函式負責執行所有必要的清理工作，例如釋放資源。
// 在這個簡單的驅動程式中，它只會記錄一條卸載訊息。
//======================================================================

VOID
DriverUnload(
    _In_ PDRIVER_OBJECT DriverObject
)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(DriverObject);
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "PhysPanelDrv: Driver unloaded.\n"));
}