import ctypes as c
from ctypes import wintypes as w
import sys
u=c.windll.user32
u.SetProcessDPIAware()
handles=[]
@c.WINFUNCTYPE(w.BOOL,w.HWND,w.LPARAM)
def visit(hwnd, arg):
    buf=c.create_unicode_buffer(512)
    u.GetWindowTextW(hwnd,buf,512)
    if buf.value.startswith('NoShellForWinter Preview ['):
        handles.append(hwnd)
    return True
u.EnumWindows(visit,0)
assert len(handles)==1,handles
rect=w.RECT()
u.GetWindowRect(handles[0],c.byref(rect))
# Floating PIE adds 3 px side borders and 38 px vertical title/border chrome.
width,height=map(int,sys.argv[1:3])
assert u.SetWindowPos(handles[0],0,0,0,width+6,height+38,0x414)
u.GetWindowRect(handles[0],c.byref(rect))
print({'window':(rect.left,rect.top,rect.right,rect.bottom),'requested_viewport':(width,height)})
