#!/usr/bin/env python3
import argparse,subprocess,json,os,hashlib
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--rom',required=True);p.add_argument('--prom',required=True);p.add_argument('--group',choices=['all','core','app'],default='all');a=p.parse_args();rom=str(Path(a.rom).resolve());prom=str(Path(a.prom).resolve())
full=Path(rom).read_bytes();live=Path(prom).read_bytes()
assert len(full)==2048000 and len(live)==1048576
assert hashlib.sha256(full).hexdigest()=='78223d9a983590e0c17e02fe46d8157938e9bb36e8a5641d1c0a55550358eada'
assert hashlib.sha256(live).hexdigest()=='eab7c64d7cda12606eb54aeba11b075fd7a58c4a94398400d23948d3555bccc2'
assert live[:0x9a200]==full[0x9400:0xa3600]
print('AD106 RUN000 captured PROM and original ROM view match byte-for-byte; actual PROM used as MMIO fixture.',flush=True)
r=Path(__file__).resolve().parent;os.chdir(r);Path('test-build').mkdir(exist_ok=True)
os.environ['ASAN_OPTIONS']='detect_leaks=1:halt_on_error=1'
os.environ['UBSAN_OPTIONS']='halt_on_error=1:print_stacktrace=1'
core=['port','compat','vendor/unlock_610','vendor/prep_610','vendor/layout_610','vendor/vbios_610','vendor/gfw_eval']
flags=['gcc','-std=c11','-fshort-wchar','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-Wall','-Wextra','-Wno-unused-parameter','-Wno-misleading-indentation','-Wno-format','-D__KERNEL__','-DGU_UEFI_PORT','-DGU_TARGET_PCI_ID=0x280310deU','-Iunlock/compat','-Ideps/edk2/MdePkg/Include','-Ideps/edk2/MdePkg/Include/X64']
cmd=flags+['unlock/tests/test_ad102_transaction.c']+['unlock/'+x+'.c' for x in core]+['-o','test-build/core-tests']
if a.group in ['all','core']:
 subprocess.run(cmd,check=True)
 with open('test-build/core-results.txt','w') as f:
  s=subprocess.run(['test-build/core-tests',prom,'USB/AD106-EXPERIMENT/ASSETS'],stdout=f,stderr=subprocess.STDOUT)
 print(Path('test-build/core-results.txt').read_text()[-8000:]);s.check_returncode()
app=flags+['-Wno-unused-function','unlock/tests/test_ad103_app.c']+['unlock/'+x+'.c' for x in ['app','app_io','ownership','format','sha256','checks']+core]+['-o','test-build/app-tests']
if a.group in ['all','app']:
 subprocess.run(app,check=True)
 with open('test-build/app-results.txt','w') as f:
  s=subprocess.run(['test-build/app-tests',prom,'USB/AD106-EXPERIMENT/ASSETS','fixtures/AD106-pci-config.bin'],stdout=f,stderr=subprocess.STDOUT)
 print(Path('test-build/app-results.txt').read_text()[-8000:]);s.check_returncode()
