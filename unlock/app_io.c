/* AD106 adaptation 2026-10-27; derived from pinned TOTORONG AD103 UEFI port. */
#include "app_io.h"
#include "compat/compat.h"
#include "format.h"
#include "sha256.h"
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include <Guid/FileInfo.h>
#include <Guid/FileSystemInfo.h>
static EFI_GUID LoadedGuid=EFI_LOADED_IMAGE_PROTOCOL_GUID,PathGuid=EFI_DEVICE_PATH_PROTOCOL_GUID,
  FsGuid=EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID,InfoGuid=EFI_FILE_INFO_ID,
  VolumeInfoGuid=EFI_FILE_SYSTEM_INFO_ID;
static EFI_SYSTEM_TABLE *Table;
static EFI_FILE_PROTOCOL *Root,*Parent,*Run,*Report,*Journal;
static BOOLEAN LogFault;
static UINT64 LogSequence,ReportBytes,JournalSequence;
static EFI_STATUS FirstLogError;
static char FirstFailurePhase[128];
static EFI_STATUS FirstFailureStatus;
#define REPORT_LIMIT (8U*1024U*1024U)
#define RECORD_BYTES 4096U
#define RECORD_HASH_OFFSET 4000U
/* Fixed storage: no allocations or GPU accesses in the emergency path. */
static char Record[RECORD_BYTES],VerifyRecord[RECORD_BYTES];
BOOLEAN GuScreenDetached;
typedef struct {const char *name;CHAR16 *file;UINTN size;const char *hash;} ASSET_SPEC;
static const ASSET_SPEC Specs[]={
 {"gpu-unlock/610.43.03/booter_load_ad102.bin",L"ASSETS\\booter_load_ad102.bin",57672,"6ee05b20e1dc244c08c052f36ef28469b8d09f4955d1fe33e48a179e11a7fd49"},
 {"gpu-unlock/610.43.03/v67-ad102-610.43.03-four-plm.bin",L"ASSETS\\v67-ad102-610.43.03-four-plm.bin",64000,"44c36b2d6b3424e53e18f3e55db85c833655de071ce66ce42749588a4a7dc8cb"},
 {"gpu-unlock/610.43.03/gsp_ga10x.bin",L"ASSETS\\gsp_ga10x.bin",84277400,"572373620a37418f24dc16b5031c39338778c3257e48e8408de9a57291b24f3a"},
 {"gpu-unlock/610.43.03/gsp_boot_ad102.bin",L"ASSETS\\gsp_boot_ad102.bin",36864,"45d6335a8185d6ff13e8d1484cea73f68d3d13aa1c19dc080ed1c3482dd5bac0"},
 {"gpu-unlock/610.43.03/gsp_boot_desc_ad102.bin",L"ASSETS\\gsp_boot_desc_ad102.bin",84,"83f8f3f1f5f9496e0ea8fc7103c2beb64d0b6de2f5ffe725f13514de5a95fa42"}
};
static VOID LogStatus(EFI_STATUS Status){
  if(!EFI_ERROR(FirstLogError))FirstLogError=EFI_ERROR(Status)?Status:EFI_DEVICE_ERROR;
  LogFault=TRUE;if(Gu.armed)Gu.fault=TRUE;
}
static VOID LogError(VOID){LogStatus(EFI_DEVICE_ERROR);}
static size_t Format(char *Text,size_t Size,const char *Fmt,...) {
  va_list args;va_start(args,Fmt);size_t n=GuFormatV(Text,Size,Fmt,args);va_end(args);return n;
}
static EFI_STATUS WriteChecked(EFI_FILE_PROTOCOL *File,UINT64 Offset,const VOID *Data,UINTN Bytes) {
  if(!File || !File->SetPosition || !File->Read || !File->Write || !File->Flush || Bytes>sizeof(VerifyRecord))return EFI_UNSUPPORTED;
  EFI_STATUS s=File->SetPosition(File,Offset);if(EFI_ERROR(s))return s;
  UINTN n=Bytes;s=File->Write(File,&n,(VOID *)Data);
  if(EFI_ERROR(s))return s;if(n!=Bytes)return EFI_BAD_BUFFER_SIZE;
  s=File->Flush(File);if(EFI_ERROR(s))return s;
  s=File->SetPosition(File,Offset);if(EFI_ERROR(s))return s;
  n=Bytes;s=File->Read(File,&n,VerifyRecord);
  if(EFI_ERROR(s))return s;
  return n==Bytes && !memcmp(Data,VerifyRecord,Bytes)?EFI_SUCCESS:EFI_CRC_ERROR;
}
EFI_STATUS GuCheckpoint(const char *Phase,EFI_STATUS OperationStatus) {
  if(!Journal)return EFI_NOT_READY;
  if(EFI_ERROR(OperationStatus) && !FirstFailurePhase[0]) {
    Format(FirstFailurePhase,sizeof(FirstFailurePhase),"%s",Phase);FirstFailureStatus=OperationStatus;
  }
  memset(Record,' ',sizeof(Record));
  UINT64 next=JournalSequence+1;
  size_t n=Format(Record,RECORD_HASH_OFFSET,
    "AD106 SAFETY v0.2.0\nsequence=%llu\nphase=%s\noperation_status=%#llx\n"
    "log_sequence=%llu report_bytes=%llu first_log_error=%#llx\n"
    "first_failed_phase=%s first_failed_status=%#llx\n"
    "requested_stall_us=%llu (not wall-clock time)\n"
    "armed=%u started=%u quiesced=%u retained=%u fault=%u owned=%u detached=%u unlock_matched=%u\n"
    "dma_maps=%u allocations_present=%u mmio_writes=%u FLR=%u\n"
    "last_mmio_offset=%#x last_mmio_value=%#x\n"
    "first_io_operation=%s first_io_offset=%#x first_io_value=%#x first_io_status=%#llx\n"
    "BME_pending=%u command=%#x cached_attributes=%#llx hardware_status=%#llx attribute_status=%#llx\n"
    "Incomplete/failed attempts require log review and physical power off. No automatic retry.\n",
    (unsigned long long)next,Phase,(unsigned long long)OperationStatus,
    (unsigned long long)LogSequence,(unsigned long long)ReportBytes,(unsigned long long)FirstLogError,
    FirstFailurePhase[0]?FirstFailurePhase:"none",(unsigned long long)FirstFailureStatus,
    (unsigned long long)Gu.ticks,Gu.armed,Gu.started,Gu.quiesced,Gu.retained,Gu.fault,Gu.owned,GuScreenDetached,Gu.unlock_matched,
    Gu.dma_count,Gu.allocations!=NULL,Gu.mmio_writes,Gu.flrs,Gu.last_mmio_offset,Gu.last_mmio_value,
    Gu.first_io_operation?Gu.first_io_operation:"none",Gu.first_io_offset,Gu.first_io_value,(unsigned long long)Gu.first_io_status,
    Gu.master_attributes_pending,Gu.master_last_command,(unsigned long long)Gu.master_last_attributes,
    (unsigned long long)Gu.master_hardware_status,(unsigned long long)Gu.master_attribute_status);
  if(n>=RECORD_HASH_OFFSET){LogError();return EFI_BAD_BUFFER_SIZE;}
  Record[n]=' ';Record[RECORD_HASH_OFFSET-1]='\n';
  UINT8 digest[32];char hex[65];static const char digits[]="0123456789abcdef";
  GuSha256(Record,RECORD_HASH_OFFSET,digest);
  for(UINTN i=0;i<32;i++){hex[2*i]=digits[digest[i]>>4];hex[2*i+1]=digits[digest[i]&15];}hex[64]=0;
  n=Format(Record+RECORD_HASH_OFFSET,sizeof(Record)-RECORD_HASH_OFFSET,"SHA256=%s\nEND\n",hex);
  if(n>=sizeof(Record)-RECORD_HASH_OFFSET){LogError();return EFI_BAD_BUFFER_SIZE;}
  Record[RECORD_HASH_OFFSET+n]=' ';Record[sizeof(Record)-1]='\n';
  /* Alternate slots; do not overwrite the preceding confirmed checkpoint. */
  EFI_STATUS s=WriteChecked(Journal,((next-1)&1U)*RECORD_BYTES,Record,sizeof(Record));
  if(EFI_ERROR(s))LogStatus(s);else JournalSequence=next;
  return s;
}
VOID GuConsole(const char *Text) {
  if(GuScreenDetached)return;
  if(!Table || !Table->ConOut || !Table->ConOut->OutputString){LogError();return;}
  while(*Text) {
    CHAR16 line[128];UINTN n=0;
    while(*Text && n<126){if(*Text=='\n')line[n++]='\r';line[n++]=(UINT8)*Text++;}
    line[n]=0;if(EFI_ERROR(Table->ConOut->OutputString(Table->ConOut,line))){LogError();return;}
  }
}
VOID GuLogText(const char *Text) {
  if(Report && !LogFault) {
    char record[1536];
    size_t n=Format(record,sizeof(record),"[seq=%llu stall_us=%llu] %s",(unsigned long long)++LogSequence,(unsigned long long)Gu.ticks,Text);
    if(n>=sizeof(record) || n>REPORT_LIMIT-ReportBytes)LogStatus(EFI_VOLUME_FULL);
    else {
      UINTN written=n;EFI_STATUS s=Report->Write(Report,&written,record);
      if(EFI_ERROR(s))LogStatus(s);
      else if(written!=n)LogStatus(EFI_BAD_BUFFER_SIZE);
      else {ReportBytes+=n;s=Report->Flush(Report);if(EFI_ERROR(s))LogStatus(s);}
    }
  } else LogError();
  /* Disk first: a broken console must not prevent the diagnostic disk write. */
  GuConsole(Text);
}
VOID GuLog(const char *Format,...) {
  char text[1024];va_list args;va_start(args,Format);
  size_t n=GuFormatV(text,sizeof(text),Format,args);va_end(args);
  if(n>=sizeof(text)){LogError();GuConsole("Log format or length error.\n");return;}
  GuLogText(text);
}
BOOLEAN GuLogReady(VOID){return Report && !LogFault;}
static BOOLEAN UsbPath(EFI_HANDLE Handle) {
  EFI_DEVICE_PATH_PROTOCOL *path=NULL;
  if(EFI_ERROR(Table->BootServices->HandleProtocol(Handle,&PathGuid,(VOID **)&path)) || !path)return FALSE;
  UINTN total=0;BOOLEAN usb=FALSE;
  for(UINTN nodes=0;nodes<64 && total+4<=4096;nodes++) {
    UINTN n=path->Length[0]+((UINTN)path->Length[1]<<8);
    if(n<4 || n>4096-total)return FALSE;
    if(path->Type==MESSAGING_DEVICE_PATH &&
      ((path->SubType==MSG_USB_DP && n==sizeof(USB_DEVICE_PATH)) ||
       (path->SubType==MSG_SD_DP && n==sizeof(SD_DEVICE_PATH))))usb=TRUE;
    if(path->Type==END_DEVICE_PATH_TYPE)return usb && n==4 && path->SubType==END_ENTIRE_DEVICE_PATH_SUBTYPE;
    total+=n;path=(VOID *)((UINT8 *)path+n);
  }
  return FALSE;
}
static EFI_STATUS Metadata(EFI_FILE_PROTOCOL *File,BOOLEAN Directory,UINTN Expected) {
  union {EFI_FILE_INFO info;UINT8 bytes[512];} data;UINTN n=sizeof(data);
  EFI_STATUS status=File->GetInfo(File,&InfoGuid,&n,&data);
  if(EFI_ERROR(status))return status;
  if(n<SIZE_OF_EFI_FILE_INFO || n>sizeof(data) || data.info.Size>n || data.info.Size<SIZE_OF_EFI_FILE_INFO ||
    !!(data.info.Attribute&EFI_FILE_DIRECTORY)!=!!Directory ||
    (!Directory && data.info.FileSize!=Expected))return EFI_COMPROMISED_DATA;
  return EFI_SUCCESS;
}
EFI_STATUS GuIoOpen(EFI_HANDLE Image,EFI_SYSTEM_TABLE *St) {
  Table=St;Root=Parent=Run=Report=Journal=NULL;LogFault=FALSE;
  FirstLogError=EFI_SUCCESS;LogSequence=ReportBytes=JournalSequence=0;
  FirstFailurePhase[0]=0;FirstFailureStatus=EFI_SUCCESS;
  memset(GuAssets,0,sizeof(GuAssets));
  EFI_LOADED_IMAGE_PROTOCOL *loaded=NULL;EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs=NULL;
  EFI_STATUS status=Table->BootServices->HandleProtocol(Image,&LoadedGuid,(VOID **)&loaded);
  if(EFI_ERROR(status) || !loaded || !UsbPath(loaded->DeviceHandle))return EFI_UNSUPPORTED;
  status=Table->BootServices->HandleProtocol(loaded->DeviceHandle,&FsGuid,(VOID **)&fs);
  if(EFI_ERROR(status) || !fs)return EFI_UNSUPPORTED;
  status=fs->OpenVolume(fs,&Root);if(EFI_ERROR(status) || !Root)return EFI_DEVICE_ERROR;
  status=Root->Open(Root,&Parent,L"\\AD106-EXPERIMENT",EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE,0);
  if(EFI_ERROR(status) || !Parent)return EFI_NOT_FOUND;
  status=Metadata(Parent,TRUE,0);if(EFI_ERROR(status))return status;
  for(UINTN slot=0;slot<100;slot++) {
    CHAR16 name[]=L"RUN00";name[3]=(CHAR16)('0'+slot/10);name[4]=(CHAR16)('0'+slot%10);
    EFI_FILE_PROTOCOL *existing=NULL;
    status=Parent->Open(Parent,&existing,name,EFI_FILE_MODE_READ,0);
    if(!EFI_ERROR(status)) {
      if(!existing || EFI_ERROR(existing->Close(existing)))return EFI_DEVICE_ERROR;continue;
    }
    if(status!=EFI_NOT_FOUND)return status;
    status=Parent->Open(Parent,&Run,name,EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,EFI_FILE_DIRECTORY);
    if(EFI_ERROR(status) || !Run)return EFI_DEVICE_ERROR;
    status=Metadata(Run,TRUE,0);if(EFI_ERROR(status))return status;
    status=Run->Open(Run,&existing,L"REPORT.TXT",EFI_FILE_MODE_READ,0);
    if(!EFI_ERROR(status)){if(existing)existing->Close(existing);return EFI_ALREADY_STARTED;}
    if(status!=EFI_NOT_FOUND)return status;
    status=Run->Open(Run,&Report,L"REPORT.TXT",EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,0);
    if(EFI_ERROR(status) || !Report)return EFI_DEVICE_ERROR;
    GuLog("Palit RTX4060Ti AD106 16GB experimental port v0.2.5\nRUN%02u\nOne U: WPR_CFG -> FBPA -> WPR -> FEAT; one SEC2 launch for all four; require marker+HALT+WPR_CFG/WPR/FEAT readback; FBPA mismatch nonfatal. Log SS0/SS1 outcomes -> verified FLR/GOP recovery -> Windows even on selector mismatch. Hardware behavior on this board is untested. Profile pinned to AD106 RUN000. No VBIOS flash or boot-variable writes.\n",(unsigned)slot);
    GuLogText("PIO readback is not an execution gate. This path does not independently verify TCM contents; FWSEC scratch/WPR results are required.\n");
    if(!GuLogReady())return EFI_DEVICE_ERROR;
    union {EFI_FILE_SYSTEM_INFO info;UINT8 bytes[512];} volume;UINTN size=sizeof(volume);
    status=Root->GetInfo(Root,&VolumeInfoGuid,&size,&volume);
    if(EFI_ERROR(status))return status;
    if(size<SIZE_OF_EFI_FILE_SYSTEM_INFO || size>sizeof(volume) || volume.info.Size>size || volume.info.Size<SIZE_OF_EFI_FILE_SYSTEM_INFO)return EFI_COMPROMISED_DATA;
    GuLog("USB volume: read_only=%u free_bytes=%llu required=16777216\n",volume.info.ReadOnly,(unsigned long long)volume.info.FreeSpace);
    if(volume.info.ReadOnly)return EFI_WRITE_PROTECTED;
    if(volume.info.FreeSpace<16U*1024U*1024U)return EFI_VOLUME_FULL;
    status=Run->Open(Run,&Journal,L"SAFETY.LOG",EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,0);
    if(EFI_ERROR(status)||!Journal)return EFI_ERROR(status)?status:EFI_DEVICE_ERROR;
    /* Allocate and exercise both fixed-size slots before touching the GPU. */
    status=GuCheckpoint("PREFLIGHT_STORAGE_SLOT_0",EFI_SUCCESS);if(EFI_ERROR(status))return status;
    status=GuCheckpoint("PREFLIGHT_STORAGE_SLOT_1",EFI_SUCCESS);if(EFI_ERROR(status))return status;
    status=Parent->Flush(Parent);if(EFI_ERROR(status))return status;
    status=Run->Flush(Run);if(EFI_ERROR(status))return status;
    status=Parent->Open(Parent,&existing,L"ATTEMPT.LCK",EFI_FILE_MODE_READ,0);
    if(!EFI_ERROR(status)) {
      if(existing)existing->Close(existing);
      GuLogText("BLOCKED: ATTEMPT.LCK exists. Review prior logs and power off before deliberately enabling another attempt.\n");
      return EFI_ALREADY_STARTED;
    }
    if(status!=EFI_NOT_FOUND)return status;
    return GuLogReady()?EFI_SUCCESS:EFI_DEVICE_ERROR;
  }
  return EFI_VOLUME_FULL;
}
EFI_STATUS GuLockAttempt(VOID) {
  if(!GuLogReady() || !Journal || !Parent || Gu.armed)return EFI_NOT_READY;
  EFI_FILE_PROTOCOL *file=NULL;
  EFI_STATUS s=Parent->Open(Parent,&file,L"ATTEMPT.LCK",EFI_FILE_MODE_READ,0);
  if(!EFI_ERROR(s)){if(file)file->Close(file);return EFI_ALREADY_STARTED;}
  if(s!=EFI_NOT_FOUND)return s;
  s=Parent->Open(Parent,&file,L"ATTEMPT.LCK",EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,0);
  if(EFI_ERROR(s)||!file)return EFI_ERROR(s)?s:EFI_DEVICE_ERROR;
  static const char text[]="AD106 v0.2.6: automatic countdown complete. One attempt reserved.\nDo not remove this lock until logs are reviewed and the machine has been powered off.\n";
  s=WriteChecked(file,0,text,sizeof(text)-1);
  EFI_STATUS closed=file->Close(file);if(!EFI_ERROR(s))s=closed;
  if(!EFI_ERROR(s))s=Parent->Flush(Parent);
  if(EFI_ERROR(s)){LogStatus(s);return s;}
  GuLogText("Persistent one-attempt lock committed and read back: ATTEMPT.LCK\n");
  return GuLogReady()?GuCheckpoint("ATTEMPT_LOCKED",EFI_SUCCESS):EFI_DEVICE_ERROR;
}
EFI_STATUS GuSavePromDump(const VOID *Data,UINTN Size) {
  if(!Run || !Data || Size!=1048576)return EFI_INVALID_PARAMETER;
  EFI_FILE_PROTOCOL *file=NULL;
  EFI_STATUS status=Run->Open(Run,&file,L"PROM.BIN",EFI_FILE_MODE_READ,0);
  if(!EFI_ERROR(status)) {
    if(file)file->Close(file);
    return EFI_ALREADY_STARTED;
  }
  if(status!=EFI_NOT_FOUND)return status;
  status=Run->Open(Run,&file,L"PROM.BIN",EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,0);
  if(EFI_ERROR(status) || !file)return EFI_ERROR(status)?status:EFI_DEVICE_ERROR;
  UINTN written=Size;
  status=file->Write(file,&written,(VOID *)Data);
  if(!EFI_ERROR(status) && written!=Size)status=EFI_BAD_BUFFER_SIZE;
  if(!EFI_ERROR(status))status=file->Flush(file);
  EFI_STATUS closed=file->Close(file);
  return EFI_ERROR(status)?status:closed;
}
EFI_STATUS GuLoadAssets(VOID) {
  for(UINTN i=0;i<5;i++) {
    EFI_FILE_PROTOCOL *file=NULL;const ASSET_SPEC *spec=&Specs[i];
    EFI_STATUS status=Parent->Open(Parent,&file,spec->file,EFI_FILE_MODE_READ,0);
    if(EFI_ERROR(status) || !file)return EFI_NOT_FOUND;
    status=Metadata(file,FALSE,spec->size);
    UINT8 *data=NULL;
    if(!EFI_ERROR(status)) {
      data=GuAlloc(spec->size);if(!data)status=EFI_OUT_OF_RESOURCES;
    }
    for(UINTN done=0;!EFI_ERROR(status) && done<spec->size;) {
      UINTN n=spec->size-done;if(n>1048576)n=1048576;UINTN want=n;
      status=file->Read(file,&n,data+done);
      if(!EFI_ERROR(status) && n!=want)status=EFI_END_OF_FILE;done+=n;
    }
    EFI_STATUS closed=file->Close(file);if(!EFI_ERROR(status))status=closed;
    if(!EFI_ERROR(status) && !GuHashMatches(data,spec->size,spec->hash))status=EFI_CRC_ERROR;
    if(EFI_ERROR(status)){GuFree(data);return status;}
    GuAssets[i].name=spec->name;GuAssets[i].firmware.data=data;GuAssets[i].firmware.size=spec->size;
    GuLog("asset verified: %s bytes=%zu sha256=%s\n",spec->name,(size_t)spec->size,spec->hash);
    if(!GuLogReady())return EFI_DEVICE_ERROR;
  }
  return EFI_SUCCESS;
}
VOID GuReleaseAssets(VOID) {
  if(!GuSafeToReturn())return;
  for(UINTN i=0;i<5;i++){GuFree((VOID *)GuAssets[i].firmware.data);memset(&GuAssets[i],0,sizeof(GuAssets[i]));}
}
EFI_STATUS GuIoClose(VOID) {
  EFI_FILE_PROTOCOL **files[]={&Report,&Journal,&Run,&Parent,&Root};EFI_STATUS result=EFI_SUCCESS;
  if(!GuSafeToReturn())return EFI_ACCESS_DENIED;
  for(UINTN i=0;i<5;i++)if(*files[i]) {
    /* EFI_FILE_PROTOCOL.Close invalidates the handle even on error. */
    EFI_FILE_PROTOCOL *file=*files[i];*files[i]=NULL;
    EFI_STATUS status=file->Close(file);
    if(EFI_ERROR(status)){LogStatus(status);return status;}
  }
  return result;
}
