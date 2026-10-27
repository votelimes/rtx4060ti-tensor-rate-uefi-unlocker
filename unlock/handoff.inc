/* AD106 adaptation 2026-10-27; derived from pinned TOTORONG AD103 UEFI port. */
/* Derived from the v0.3 handoff path exercised on the upstream author's single AD103. */
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
static EFI_GUID FsGuid=EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID,LoadedGuid=EFI_LOADED_IMAGE_PROTOCOL_GUID;
static const CHAR16 WindowsPath[]=L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi";
static EFI_HANDLE WindowsImage,WindowsVolume;
static EFI_DEVICE_PATH_PROTOCOL *WindowsDevicePath;
static EFI_STATUS MakeWindowsPath(VOID) {
  EFI_DEVICE_PATH_PROTOCOL *base=NULL;
  EFI_STATUS s=Gu.bs->HandleProtocol(WindowsVolume,&PathGuid,(VOID**)&base);
  if(EFI_ERROR(s)||!base)return EFI_NOT_FOUND;
  UINTN off=0;BOOLEAN end=FALSE;
  for(UINTN i=0;i<128 && off<=4092;i++) {
    EFI_DEVICE_PATH_PROTOCOL *n=(VOID*)((UINT8*)base+off);
    UINTN len=n->Length[0]+((UINTN)n->Length[1]<<8);
    if(len<4||len>4096-off)return EFI_COMPROMISED_DATA;
    if(n->Type==END_DEVICE_PATH_TYPE) {
      if(n->SubType!=END_ENTIRE_DEVICE_PATH_SUBTYPE||len!=4)return EFI_UNSUPPORTED;
      end=TRUE;break;
    }
    off+=len;
  }
  if(!end)return EFI_COMPROMISED_DATA;
  UINTN node=4+sizeof(WindowsPath),total=off+node+4;
  UINT8 *b=GuAlloc(total);if(!b)return EFI_OUT_OF_RESOURCES;
  memcpy(b,base,off);b[off]=MEDIA_DEVICE_PATH;b[off+1]=MEDIA_FILEPATH_DP;
  b[off+2]=(UINT8)node;b[off+3]=(UINT8)(node>>8);memcpy(b+off+4,WindowsPath,sizeof(WindowsPath));
  b[off+node]=END_DEVICE_PATH_TYPE;b[off+node+1]=END_ENTIRE_DEVICE_PATH_SUBTYPE;b[off+node+2]=4;
  WindowsDevicePath=(VOID*)b;return EFI_SUCCESS;
}
static EFI_STATUS PreloadWindows(VOID) {
  EFI_HANDLE *h=NULL;UINTN n=0,found=0;
  EFI_STATUS s=Gu.bs->LocateHandleBuffer(ByProtocol,&FsGuid,NULL,&n,&h);
  if(EFI_ERROR(s))return s;
  if(!h||n>256){GuFree(h);return EFI_COMPROMISED_DATA;}
  for(UINTN i=0;i<n;i++) {
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs=NULL;EFI_FILE_PROTOCOL *root=NULL,*file=NULL;
    EFI_STATUS x=Gu.bs->HandleProtocol(h[i],&FsGuid,(VOID**)&fs);
    if(!EFI_ERROR(x)&&fs)x=fs->OpenVolume(fs,&root);
    if(EFI_ERROR(x)||!root){s=EFI_DEVICE_ERROR;break;}
    x=root->Open(root,&file,(CHAR16*)WindowsPath,EFI_FILE_MODE_READ,0);
    if(!EFI_ERROR(x)&&file) {
      UINT8 mz[2];UINTN bytes=2;EFI_STATUS y=file->Read(file,&bytes,mz);
      EFI_STATUS closed=file->Close(file);
      if(EFI_ERROR(y)||EFI_ERROR(closed)||bytes!=2||mz[0]!='M'||mz[1]!='Z')s=EFI_COMPROMISED_DATA;
      else {found++;WindowsVolume=h[i];}
    } else if(x!=EFI_NOT_FOUND)s=EFI_DEVICE_ERROR;
    x=root->Close(root);if(EFI_ERROR(x))s=x;
    if(EFI_ERROR(s))break;
  }
  GuFree(h);GuLog("Windows candidates=%zu status=%#llx\n",(size_t)found,(unsigned long long)s);
  if(EFI_ERROR(s))return s;
  if(found!=1)return found?EFI_NO_MAPPING:EFI_NOT_FOUND;
  s=MakeWindowsPath();if(EFI_ERROR(s))return s;
  s=Gu.bs->LoadImage(FALSE,Gu.image,WindowsDevicePath,NULL,0,&WindowsImage);
  GuLog("Windows LoadImage=%#llx\n",(unsigned long long)s);
  if(EFI_ERROR(s))return s;
  EFI_LOADED_IMAGE_PROTOCOL *li=NULL;
  s=Gu.bs->HandleProtocol(WindowsImage,&LoadedGuid,(VOID**)&li);
  if(EFI_ERROR(s)||!li||li->DeviceHandle!=WindowsVolume||li->ImageCodeType!=EfiLoaderCode)return EFI_COMPROMISED_DATA;
  return GuLogReady()?EFI_SUCCESS:EFI_DEVICE_ERROR;
}
static EFI_STATUS VerifySelectors(const char *phase) {
  UINT32 ss0=GuRead32(0x82381c),ss1=GuRead32(0x823820),effective=GuRead32(0x823818);
  GuLog("%s SS0=0x%08x SS1=0x%08x\n",phase,ss0,ss1);
  GuLog("%s unlock_matched=%u; handoff checks GPU accessibility separately\n",phase,ss0==0x88888888 && ss1==8);
  BOOLEAN valid=ss0!=0xffffffff && ss1!=0xffffffff && effective!=0xffffffff &&
    (ss0&0xffff0000U)!=0xbadf0000U && (ss1&0xffff0000U)!=0xbadf0000U &&
    (effective&0xffff0000U)!=0xbadf0000U;
  Gu.unlock_matched=Gu.unlock_matched && valid && ss0==0x88888888 && ss1==8 && !(effective&0x07ffffffU);
  GuLog("%s effective=%#x UNLOCK_MATCHED=%u\n",phase,effective,Gu.unlock_matched);
  return !Gu.fault && GuLogReady() && valid?EFI_SUCCESS:EFI_DEVICE_ERROR;
}
/* Persist returned operation errors even if the normal report has failed.
   Never advance to another firmware call unless both logs remain usable. */
static EFI_STATUS HandoffCheckpoint(const char *Phase,EFI_STATUS OperationStatus) {
  GuLog("handoff phase=%s status=%#llx owned=%u detached=%u\n",Phase,
    (unsigned long long)OperationStatus,Gu.owned,GuScreenDetached);
  EFI_STATUS saved=GuPhase(Phase,OperationStatus);
  if(EFI_ERROR(OperationStatus))return OperationStatus;
  if(EFI_ERROR(saved))return saved;
  return GuLogReady()?EFI_SUCCESS:EFI_DEVICE_ERROR;
}
#define HANDOFF_MAX_NODES 16
typedef struct {EFI_HANDLE handle;UINTN depth;} HANDOFF_NODE;
static EFI_STATUS NodeCheckpoint(const char *Name,UINTN Index,EFI_STATUS Status) {
  char phase[64];UINTN n=0;
  while(Name[n] && n<sizeof(phase)-4){phase[n]=Name[n];n++;}
  if(Name[n] || Index>=100)return EFI_INVALID_PARAMETER;
  phase[n++]='_';phase[n++]=(char)('0'+Index/10);phase[n++]=(char)('0'+Index%10);phase[n]=0;
  return HandoffCheckpoint(phase,Status);
}
/* Only follow BY_CHILD_CONTROLLER relationships rooted at this GPU.
   Snapshot at TPL_NOTIFY, then restore TPL before logging or connecting. */
static EFI_STATUS GatherChildren(HANDOFF_NODE *Nodes,UINTN Index,UINTN *Total) {
  EFI_OPEN_PROTOCOL_INFORMATION_ENTRY records[64];UINTN used=0;
  EFI_GUID **protocols=NULL;UINTN protocol_count=0;
  EFI_TPL old=Gu.bs->RaiseTPL(TPL_NOTIFY);
  EFI_STATUS s=Gu.bs->ProtocolsPerHandle(Nodes[Index].handle,&protocols,&protocol_count);
  if(!EFI_ERROR(s) && (!protocols || !protocol_count || protocol_count>32))s=EFI_COMPROMISED_DATA;
  for(UINTN p=0;!EFI_ERROR(s) && p<protocol_count;p++) {
    EFI_OPEN_PROTOCOL_INFORMATION_ENTRY *entries=NULL;UINTN count=0;
    if(!protocols[p]){s=EFI_COMPROMISED_DATA;break;}
    s=Gu.bs->OpenProtocolInformation(Nodes[Index].handle,protocols[p],&entries,&count);
    if(!EFI_ERROR(s) && (count>64-used || (count && !entries)))s=EFI_COMPROMISED_DATA;
    if(!EFI_ERROR(s))for(UINTN e=0;e<count;e++)records[used++]=entries[e];
    if(entries && EFI_ERROR(Gu.bs->FreePool(entries)))s=EFI_DEVICE_ERROR;
  }
  if(protocols && EFI_ERROR(Gu.bs->FreePool(protocols)))s=EFI_DEVICE_ERROR;
  Gu.bs->RestoreTPL(old);
  GuLog("child snapshot: node=%zu handle=%#llx depth=%zu records=%zu status=%#llx\n",
    (size_t)Index,(unsigned long long)(UINTN)Nodes[Index].handle,(size_t)Nodes[Index].depth,(size_t)used,(unsigned long long)s);
  if(EFI_ERROR(s))return s;
  for(UINTN e=0;e<used;e++) {
    EFI_OPEN_PROTOCOL_INFORMATION_ENTRY *r=&records[e];
    GuLog("child owner: node=%zu agent=%#llx controller=%#llx attributes=%#x count=%u\n",
      (size_t)Index,(unsigned long long)(UINTN)r->AgentHandle,(unsigned long long)(UINTN)r->ControllerHandle,r->Attributes,r->OpenCount);
    if(!(r->Attributes&EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER))continue;
    EFI_HANDLE child=r->ControllerHandle;
    if(!child || child==Gu.image || child==Gu.handle || child==Nodes[Index].handle)return EFI_COMPROMISED_DATA;
    BOOLEAN seen=FALSE;for(UINTN n=0;n<*Total;n++)if(Nodes[n].handle==child)seen=TRUE;
    if(seen){GuLogText("child already queued; no repeated connection\n");continue;}
    if(*Total>=HANDOFF_MAX_NODES || Nodes[Index].depth>=4)return EFI_OUT_OF_RESOURCES;
    Nodes[*Total].handle=child;Nodes[*Total].depth=Nodes[Index].depth+1;
    GuLog("child queued: index=%zu parent=%zu handle=%#llx depth=%zu\n",(size_t)*Total,(size_t)Index,
      (unsigned long long)(UINTN)child,(size_t)Nodes[*Total].depth);(*Total)++;
  }
  return GuLogReady()?EFI_SUCCESS:EFI_DEVICE_ERROR;
}
static EFI_STATUS ConnectChildren(VOID) {
  HANDOFF_NODE nodes[HANDOFF_MAX_NODES]={{Gu.handle,0}};UINTN total=1;
  for(UINTN i=0;i<total;i++) {
    EFI_STATUS s;
    if(i) {
      GuLog("child connect intent: index=%zu handle=%#llx depth=%zu driver_list=NULL recursive=0\n",
        (size_t)i,(unsigned long long)(UINTN)nodes[i].handle,(size_t)nodes[i].depth);
      s=NodeCheckpoint("BEFORE_CHILD_CONNECT",i,EFI_SUCCESS);if(EFI_ERROR(s))return s;
      s=Gu.bs->ConnectController(nodes[i].handle,NULL,NULL,FALSE);
      GuLog("child connect returned: index=%zu status=%#llx no_applicable_driver=%u\n",
        (size_t)i,(unsigned long long)s,(unsigned)(s==EFI_NOT_FOUND));
      /* A leaf may have no driver to attach. Full GOP/PCI validation still follows. */
      s=NodeCheckpoint("AFTER_CHILD_CONNECT",i,s==EFI_NOT_FOUND?EFI_SUCCESS:s);if(EFI_ERROR(s))return s;
    }
    s=NodeCheckpoint("BEFORE_CHILD_ENUM",i,EFI_SUCCESS);if(EFI_ERROR(s))return s;
    s=GatherChildren(nodes,i,&total);
    s=NodeCheckpoint("AFTER_CHILD_ENUM",i,s);if(EFI_ERROR(s))return s;
  }
  GuLog("child tree complete: nodes=%zu max_nodes=16 max_depth=4; no recursive calls\n",(size_t)total);
  return GuLogReady()?EFI_SUCCESS:EFI_DEVICE_ERROR;
}
static EFI_STATUS RestoreDisplayForWindows(EFI_HANDLE Driver) {
  if(!WindowsImage || !Driver || !GuScreenDetached || !GuSafeToReturn() || !Gu.quiesced ||
     Gu.flrs!=1 || Gu.allocations || Gu.dma_count || !Gu.owned || Gu.fault)return EFI_NOT_READY;
  EFI_STATUS s=VerifySelectors("POST-FLR");if(EFI_ERROR(s))return s;
  GuLogText("stage=before_GOP_reconnect; transaction quiesced, DMA released, PCI restored\n");
  s=GuPhase("BEFORE_GOP_RECONNECT",EFI_SUCCESS);if(EFI_ERROR(s))return s;
  if(!GuLogReady())return EFI_DEVICE_ERROR;
  s=HandoffCheckpoint("BEFORE_PCI_CLOSE",EFI_SUCCESS);if(EFI_ERROR(s))return s;
  s=Gu.bs->CloseProtocol(Gu.handle,&PciGuid,Gu.image,NULL);
  if(!EFI_ERROR(s))Gu.owned=FALSE;
  s=HandoffCheckpoint("AFTER_PCI_CLOSE",s);if(EFI_ERROR(s))return s;
  EFI_HANDLE drivers[2]={Driver,NULL};
  GuLog("GOP reconnect intent: controller=%#llx driver=%#llx remaining_path=NULL recursive=0; synchronous firmware call, no application timeout\n",
    (unsigned long long)(UINTN)Gu.handle,(unsigned long long)(UINTN)Driver);
  s=HandoffCheckpoint("BEFORE_CONNECT_CONTROLLER",EFI_SUCCESS);if(EFI_ERROR(s))return s;
  s=Gu.bs->ConnectController(Gu.handle,drivers,NULL,FALSE);
  s=HandoffCheckpoint("AFTER_CONNECT_CONTROLLER",s);if(EFI_ERROR(s))return s;
  s=ConnectChildren();if(EFI_ERROR(s))return s;
  s=HandoffCheckpoint("BEFORE_PCI_REFRESH",EFI_SUCCESS);if(EFI_ERROR(s))return s;
  s=GuRefreshTargetPci();
  s=HandoffCheckpoint("AFTER_PCI_REFRESH",s);if(EFI_ERROR(s))return s;
  ExpectDetached=FALSE;
  s=HandoffCheckpoint("BEFORE_DISPLAY_CHECK",EFI_SUCCESS);if(EFI_ERROR(s))return s;
  s=DisplayCheck()?EFI_SUCCESS:EFI_DEVICE_ERROR;
  s=HandoffCheckpoint("AFTER_DISPLAY_CHECK",s);if(EFI_ERROR(s))return s;
  s=HandoffCheckpoint("BEFORE_PCI_VERIFY",EFI_SUCCESS);if(EFI_ERROR(s))return s;
  UINT16 command=0;UINT32 value=0;
  s=GuConfigRead(EfiPciIoWidthUint16,4,&command);
  if(EFI_ERROR(s)||(command&~4U)!=((UINT16)InitialConfig[1]&~4U)||((command&4U)&&!(InitialConfig[1]&4U)))return EFI_DEVICE_ERROR;
  for(UINT32 offset=0x10;offset<=0x24;offset+=4) {
    s=GuConfigRead(EfiPciIoWidthUint32,offset,&value);
    if(EFI_ERROR(s)||value!=InitialConfig[offset/4])return EFI_DEVICE_ERROR;
  }
  s=HandoffCheckpoint("AFTER_PCI_VERIFY",EFI_SUCCESS);if(EFI_ERROR(s))return s;
  s=HandoffCheckpoint("BEFORE_POST_GOP_SELECTORS",EFI_SUCCESS);if(EFI_ERROR(s))return s;
  s=VerifySelectors("POST-GOP");
  s=HandoffCheckpoint("AFTER_POST_GOP_SELECTORS",s);if(EFI_ERROR(s))return s;
  GuLogText("stage=before_StartImage; POST-GOP selectors and PCI BAR/command verified\n");
  s=GuPhase("BEFORE_WINDOWS_HANDOFF",EFI_SUCCESS);if(EFI_ERROR(s))return s;
  if(!GuLogReady())return EFI_DEVICE_ERROR;
  GuScreenDetached=FALSE;
  return EFI_SUCCESS;
}
