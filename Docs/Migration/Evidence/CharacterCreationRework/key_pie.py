import ctypes as c
from ctypes import wintypes as w
import time
u=c.windll.user32
handles=[]
@c.WINFUNCTYPE(w.BOOL,w.HWND,w.LPARAM)
def visit(hwnd,arg):
    buf=c.create_unicode_buffer(512)
    u.GetWindowTextW(hwnd,buf,512)
    if buf.value.startswith('NoShellForWinter Preview ['): handles.append(hwnd)
    return True
u.EnumWindows(visit,0)
assert len(handles)==1
u.SetForegroundWindow(handles[0])
u.PostMessageW(handles[0],0x100,0xBE,1 | (u.MapVirtualKeyW(0xBE,0)<<16))
time.sleep(0.08)
u.PostMessageW(handles[0],0x101,0xBE,1 | (u.MapVirtualKeyW(0xBE,0)<<16) | 0xC0000000)
