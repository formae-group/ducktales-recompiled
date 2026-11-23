
import re, sys, zlib
rom=open('roms/DuckTales (USA).nes','rb').read()
prg=rom[16:16+rom[4]*0x4000]
last=rom[4]-1
def rd(bank,addr):
    b= last if addr>=0xC000 else bank
    return prg[b*0x4000+(addr&0x3FFF)]
src=open('src/decomp/flow.c','rb').read().decode()
syms={}
for m in re.finditer(r'#define\s+(DT_\w+)\s+(0x[0-9A-Fa-f]+)', open('src/game/ducktales_symbols.h','rb').read().decode()):
    syms[m.group(1)]=int(m.group(2),16)
out=['','# Game flow (src/decomp/flow.c, docs/decompilation.md "Task code"): the','# entries and resume points of the main task\'s decompiled code, then the','# routines it hands off to, where check mode stops the original.','']
for m in re.finditer(r'(ROUTINE|TASK)\((\w+), "(\w+)", "(\w+)"', src):
    kind,sym,id,name=m.groups(); a=syms[sym]
    b=bytes(rd(7,a+i) for i in range(8))
    out+=[f'# {name}','[[mod_function_hook]]',f'id = "ducktales.decomp.{id}"',f'addr = 0x{a:04X}','length = 8',f'crc32 = 0x{zlib.crc32(b):08X}','']
for m in re.finditer(r'STOP\((\w+), "([\w.]+)"\)', src):
    sym,id=m.groups(); a=syms[sym]
    bank=int(id.split('.')[1]) if '.' in id else 7
    b=bytes(rd(bank,a+i) for i in range(8))
    out+=[f'[[mod_function_hook]]',f'id = "ducktales.decomp.stop.{id}"',f'addr = 0x{a:04X}','length = 8',f'crc32 = 0x{zlib.crc32(b):08X}','']
txt='\r\n'.join(out)
t=open('config/game.toml','rb').read().decode()
start=t.find('\r\n# Game flow (src/decomp/flow.c')
if start>=0:
    end=t.find('# END game flow', start)
    t=t[:start]+t[end+len('# END game flow\r\n'):]
t=t.rstrip('\r\n')+'\r\n'+txt+'# END game flow\r\n'
open('config/game.toml','wb').write(t.encode())
print(txt.count('[[mod_function_hook]]'))
