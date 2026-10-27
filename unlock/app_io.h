/* AD106 adaptation 2026-10-27; derived from pinned TOTORONG AD103 UEFI port. */
#pragma once
#include "port.h"
EFI_STATUS GuIoOpen(EFI_HANDLE Image,EFI_SYSTEM_TABLE *Table);
EFI_STATUS GuIoClose(VOID);
EFI_STATUS GuLoadAssets(VOID);
EFI_STATUS GuSavePromDump(const VOID *Data,UINTN Size);
VOID GuReleaseAssets(VOID);
BOOLEAN GuLogReady(VOID);
EFI_STATUS GuCheckpoint(const char *Phase, EFI_STATUS OperationStatus);
EFI_STATUS GuLockAttempt(VOID);
extern BOOLEAN GuScreenDetached;
VOID GuConsole(const char *Text);
