import ctypes as c
from ctypes import wintypes as w
import time
import sys
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
hwnd=handles[0]
point=w.POINT(400,400)
u.ClientToScreen(hwnd,c.byref(point))
u.SetCursorPos(point.x,point.y)
time.sleep(.08)
mode=sys.argv[1] if len(sys.argv)>1 else 'zoom'
if mode=='zoom':
    u.PostMessageW(hwnd,0x20A,120<<16,(point.y<<16)|point.x)
else:
    down,up,flag=(0x201,0x202,1) if mode=='orbit' else (0x207,0x208,16)
    u.PostMessageW(hwnd,down,flag,(400<<16)|400)
    time.sleep(.1)
    u.SetCursorPos(point.x+50,point.y+10)
    u.PostMessageW(hwnd,0x200,flag,(410<<16)|450)
    time.sleep(.1)
    u.PostMessageW(hwnd,up,0,(410<<16)|450)
