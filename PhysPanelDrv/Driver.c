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

#include "driver.h" // 專案的主標頭檔
#include <wdm.h>    // WDM (Windows Driver Model) 的核心標頭檔

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
// 全域常數與全域變數 (Global Constants & Variables)
//======================================================================

// DirectX 內部顯示面板尺寸的 WNF ID。
// 這是一個由系統定義的內部常數，用於覆寫顯示器的實體尺寸。
// 使用 #define 來定義 WNF 狀態名稱的 ID。
#define WNF_DX_INTERNAL_PANEL_DIMENSIONS_ID { 0xA3BC4875, 0x41C61629 }

// 驅動程式核心功能記憶體池標籤
// 在核心偵錯器 (WinDbg) 中用於追蹤記憶體分配
#define PPD_POOL_TAG 'vDPP'

// 全域變數，用於保存背景工作執行緒的物件指標 (PETHREAD)。
// 這是為了在 DriverUnload 中安全地等待執行緒結束，防止在卸載時
// 發生競爭條件 (Race Condition) 而導致系統崩潰。
PETHREAD g_pWorkerThread = NULL;

// 驅動程式進入點和卸載函式原型宣告
DRIVER_INITIALIZE DriverEntry;
DRIVER_UNLOAD DriverUnload;

// 宣告背景工作執行緒的函式
KSTART_ROUTINE WnfUpdateWorkerThread;
VOID WnfUpdateWorkerThread(_In_ PVOID StartContext);

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
    HANDLE hThread = NULL;                // 執行緒句柄
    PULONGLONG pDimensionsContext = NULL; // 用於傳遞給執行緒的上下文

    // PAGED_CODE() 是一個在偵錯組建中進行執行時檢查的巨集。
    // 它會驗證這段程式碼是否在允許分頁錯誤的低 IRQL (PASSIVE_LEVEL) 下執行。
    PAGED_CODE();

    // 將驅動程式物件和註冊表路徑參數標記為未使用，以避免編譯器產生警告。
    UNREFERENCED_PARAMETER(RegistryPath);

    // KdPrintEx 用於將偵錯訊息輸出到核心偵錯器 (如 WinDbg)。
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "PhysPanelDrv: DriverEntry - Starting...\n"));

    // 設定想要的尺寸 (單位：公釐) 
    // 這裡直接硬式編碼 (hardcode) 所需的寬度和高度。
    const ULONG widthMm = 155;
    const ULONG heightMm = 87;

    // 將兩個 32 位元的 ULONG (寬度和高度) 封裝成一個 64 位元的 ULONGLONG。
    // 這是 WNF API 預期的資料格式。
    // 記憶體佈局: |<--- Height (32 bits) --->|<--- Width (32 bits) --->|
    dimensions = ((ULONGLONG)heightMm << 32) | widthMm;

    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "PhysPanelDrv: Preparing dimensions Width=%lu, Height=%lu\n", widthMm, heightMm));

    // ** 異步處理 **
    // 為了不阻塞系統啟動，必須在背景執行緒中執行重複的 WNF 更新
    // 1. 為執行緒上下文分配記憶體
    pDimensionsContext = (PULONGLONG)ExAllocatePool2(
        POOL_FLAG_PAGED,        // 可分頁記憶體
        sizeof(ULONGLONG),      // 大小
        PPD_POOL_TAG            // 記憶體標籤
    );

    if (pDimensionsContext == NULL)
    {
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "PhysPanelDrv: Failed to allocate memory for worker thread context.\n"));
        // 即使失敗，也返回 SUCCESS，以避免啟動問題
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit; // 跳轉到退出點
    }

    // 2. 將 dimensions 資料存入上下文
    *pDimensionsContext = dimensions;

    // 3. 建立系統工作執行緒
    status = PsCreateSystemThread(
        &hThread,                 // 接收執行緒句柄
        (ACCESS_MASK)0,           // 預設存取權限
        NULL,                     // 無物件屬性
        NULL,                     // 不在使用者進程中
        NULL,                     // 無 Client ID
        WnfUpdateWorkerThread,    // 執行緒函式
        pDimensionsContext        // 傳遞 dimensions 作為上下文
    );

    if (!NT_SUCCESS(status))
    {
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "PhysPanelDrv: Failed to create worker thread. Status: 0x%X\n", status));
        // 執行緒建立失敗，釋放剛剛分配的上下文記憶體
        ExFreePoolWithTag(pDimensionsContext, PPD_POOL_TAG);
        goto Exit;
    }

    // 4. 取得執行緒的物件指標 (PETHREAD) 並增加其引用計數。
    // 這樣 DriverUnload 函式才能在未來安全地存取此物件，
    // 並等待它執行完畢，即使句柄 (hThread) 稍後被關閉。
    status = ObReferenceObjectByHandle(
        hThread,                     // 從 PsCreateSystemThread 拿到的句柄
        THREAD_ALL_ACCESS,           // 請求的存取權限
        *PsThreadType,               // 物件類型 (執行緒)
        KernelMode,                  // 模式
        (PVOID*)&g_pWorkerThread,    // 輸出物件指標到全域變數
        NULL
    );

    if (!NT_SUCCESS(status))
    {
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "PhysPanelDrv: Failed to get thread object reference. Status: 0x%X\n", status));
        // 錯誤：無法獲取執行緒的引用。
        // 這會導致卸載時不安全，但仍繼續載入以避免啟動失敗。
        g_pWorkerThread = NULL; // 確保全域指標為 NULL，以便 DriverUnload 知道
        // 句柄 hThread 將在下面被關閉。
    }
    else
    {
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "PhysPanelDrv: Worker thread created and referenced.\n"));
    }

    // 5. 關閉執行緒句柄。
    // 由於已經透過 ObReferenceObjectByHandle 獲得了對執行緒物件的引用 (g_pWorkerThread)，
    // 不再需要這個句柄 (hThread)。
    // 關閉句柄會減少一個句柄計數，但物件引用仍會保持執行緒物件的存活，
    // 直到 DriverUnload 呼叫 ObDereferenceObject。
    ZwClose(hThread);

Exit:
    // 註冊一個卸載函式，以便 I/O 管理器知道如何在需要時安全地卸載此驅動程式
    DriverObject->DriverUnload = DriverUnload;

    // 立即回傳成功，表示驅動程式已"成功"初始化並載入
    // 這可確保 DriverEntry 不會阻塞系統啟動
    // 注意：即使執行緒建立或引用失敗，仍返回 SUCCESS
    // 以避免在啟動時引起問題。
    return STATUS_SUCCESS;
}

//======================================================================
// WnfUpdateWorkerThread - 背景工作執行緒
//
// 這個函式在 DriverEntry 建立的獨立執行緒上執行。
// 它的任務是循環呼叫 ZwUpdateWnfStateData，然後自我終止。
//======================================================================

VOID
WnfUpdateWorkerThread(
    _In_ PVOID StartContext
)
{
    PAGED_CODE();

    // 區域變數
    NTSTATUS status;
    WNF_STATE_NAME wnfStateName = WNF_DX_INTERNAL_PANEL_DIMENSIONS_ID;
    LARGE_INTEGER delay;

    // 1. 從上下文中檢索 dimensions 資料
    // 必須在釋放 StartContext 之前複製它
    PULONGLONG pDimensions = (PULONGLONG)StartContext;
    ULONGLONG dimensions = *pDimensions;

    // 2. 定義延遲時間 (1 秒)
    // 單位是 100 奈秒 (nanoseconds)
    // 1 秒 = 10,000,000 個 100ns
    // 負值表示相對時間
    delay.QuadPart = -10000000LL; // 1 秒

    // 嘗試更新的次數。
    // 這是一個防禦性策略，確保設定能在系統啟動過程中
    // "勝出"，覆蓋掉其他可能也在設定此值的元件。
    const int retryCount = 25;

    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "PhysPanelDrv: Worker thread started. Will attempt %d updates.\n", retryCount));

    // 3. 執行 25 次循環
    for (int i = 0; i < retryCount; i++)
    {
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "PhysPanelDrv: Update attempt %d/25...\n", i + 1));

        // 呼叫核心 WNF API (ZwUpdateWnfStateData) 來更新狀態資料
        // 這會將設定的尺寸寫入到系統中
        status = ZwUpdateWnfStateData(
            &wnfStateName,        // 要更新的 WNF 狀態 ID
            &dimensions,          // 指向包含新資料的緩衝區
            sizeof(dimensions),   // 資料的長度 (8 bytes)
            NULL,                 // TypeId: 使用 NULL 表示預設型別
            NULL,                 // ExplicitScope: 使用 NULL 表示預設作用域
            0,                    // MatchingChangeStamp: 條件式更新用的戳記，此處不需要
            FALSE                 // CheckStamp: FALSE 表示不檢查目前的 ChangeStamp，直接強制覆寫
        );

        if (!NT_SUCCESS(status)) {
            KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "PhysPanelDrv: ZwUpdateWnfStateData failed (Attempt %d). Status: 0x%X\n", i + 1, status));
        }
        else {
            KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "PhysPanelDrv: Successfully updated panel dimensions (Attempt %d).\n", i + 1));
        }

        // 延遲 1 秒
        // 檢查迴圈條件，確保這是最後一次迭代
        if (i < retryCount - 1)
        {
            KeDelayExecutionThread(KernelMode, FALSE, &delay);
        }
    }

    // 4. 清理
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "PhysPanelDrv: Worker thread finished %d updates. Cleaning up and terminating.\n", retryCount));

    // 釋放 DriverEntry 分配的上下文記憶體
    if (StartContext != NULL)
    {
        ExFreePoolWithTag(StartContext, PPD_POOL_TAG);
    }

    // 5. 終止執行緒
    // 呼叫此函式後，DriverUnload 中的 KeWaitForSingleObject 將會被喚醒。
    PsTerminateSystemThread(STATUS_SUCCESS);
}

//======================================================================
// DriverUnload - 當驅動程式被卸載時呼叫
//
// 這個函式負責執行所有必要的清理工作。
// 它的關鍵任務是安全地等待背景工作執行緒 (g_pWorkerThread)
// 完全終止，然後才能返回，以防止系統崩潰。
//======================================================================

VOID
DriverUnload(
    _In_ PDRIVER_OBJECT DriverObject
)
{
    NTSTATUS status;
    PAGED_CODE();
    UNREFERENCED_PARAMETER(DriverObject);

    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "PhysPanelDrv: DriverUnload initiated...\n"));

    // 檢查：確認 DriverEntry 是否成功建立並引用了執行緒。
    if (g_pWorkerThread != NULL)
    {
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "PhysPanelDrv: Waiting for worker thread to terminate...\n"));

        // 等待執行緒物件發出訊號 (signaled)，這表示執行緒已經終止。
        // 這是卸載過程中最關鍵的步驟，用以防止競爭條件。
        // 在此無限期等待，直到執行緒安全結束。
        status = KeWaitForSingleObject(
            (PVOID)g_pWorkerThread,    // 要等待的物件 (執行緒)
            Executive,                 // 等待原因 (驅動程式的標準等待)
            KernelMode,                // 模式
            FALSE,                     // 不可警示 (Not alertable)
            NULL                       // 無超時 (Wait forever)
        );

        if (NT_SUCCESS(status))
        {
            KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "PhysPanelDrv: Worker thread has terminated.\n"));
        }
        else
        {
            // 這種情況很少見，但仍需記錄
            KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "PhysPanelDrv: KeWaitForSingleObject failed. Status: 0x%X\n", status));
        }

        // 執行緒已終止，現在可以安全地釋放在 DriverEntry 中取得的物件引用。
        // 這會將引用計數減 1，允許記憶體管理器最終銷毀該執行緒物件。
        ObDereferenceObject(g_pWorkerThread);
        g_pWorkerThread = NULL; // 清除全域指標
    }
    else
    {
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "PhysPanelDrv: No active worker thread to wait for (g_pWorkerThread is NULL).\n"));
    }

    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "PhysPanelDrv: Driver unloaded.\n"));
}