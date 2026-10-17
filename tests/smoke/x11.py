"""Small Xlib-only test driver: targeted events, no desktop input grabs."""
import ctypes as c
import ctypes.util as util
import time
import shutil
import subprocess

class Key(c.Structure):
    _fields_ = [('type',c.c_int),('serial',c.c_ulong),('send_event',c.c_int),('display',c.c_void_p),('window',c.c_ulong),('root',c.c_ulong),('subwindow',c.c_ulong),('time',c.c_ulong),('x',c.c_int),('y',c.c_int),('x_root',c.c_int),('y_root',c.c_int),('state',c.c_uint),('code',c.c_uint),('same_screen',c.c_int)]
class Data(c.Union):
    _fields_ = [('l',c.c_long*5)]
class Client(c.Structure):
    _fields_ = [('type',c.c_int),('serial',c.c_ulong),('send_event',c.c_int),('display',c.c_void_p),('window',c.c_ulong),('message_type',c.c_ulong),('format',c.c_int),('data',Data)]
class Event(c.Union):
    _fields_ = [('key',Key),('client',Client),('pad',c.c_long*24)]
class Image(c.Structure):
    _fields_ = [('width',c.c_int),('height',c.c_int),('xoffset',c.c_int),('format',c.c_int),('data',c.c_void_p),('byte_order',c.c_int),('bitmap_unit',c.c_int),('bitmap_bit_order',c.c_int),('bitmap_pad',c.c_int),('depth',c.c_int),('bytes_per_line',c.c_int),('bits_per_pixel',c.c_int),('red_mask',c.c_ulong),('green_mask',c.c_ulong),('blue_mask',c.c_ulong)]
class X11:
    def __init__(self):
        self.lib=c.CDLL(util.find_library('X11'))
        signatures={
            'XOpenDisplay':(c.c_void_p,[c.c_char_p]),'XDefaultRootWindow':(c.c_ulong,[c.c_void_p]),
            'XQueryTree':(c.c_int,[c.c_void_p,c.c_ulong,c.POINTER(c.c_ulong),c.POINTER(c.c_ulong),c.POINTER(c.POINTER(c.c_ulong)),c.POINTER(c.c_uint)]),
            'XFetchName':(c.c_int,[c.c_void_p,c.c_ulong,c.POINTER(c.c_void_p)]),'XFree':(c.c_int,[c.c_void_p]),
            'XInternAtom':(c.c_ulong,[c.c_void_p,c.c_char_p,c.c_int]),'XSendEvent':(c.c_int,[c.c_void_p,c.c_ulong,c.c_int,c.c_long,c.POINTER(Event)]),
            'XKeysymToKeycode':(c.c_uint,[c.c_void_p,c.c_ulong]),'XFlush':(c.c_int,[c.c_void_p]),'XSync':(c.c_int,[c.c_void_p,c.c_int]),
            'XMoveResizeWindow':(c.c_int,[c.c_void_p,c.c_ulong,c.c_int,c.c_int,c.c_uint,c.c_uint]),'XRaiseWindow':(c.c_int,[c.c_void_p,c.c_ulong]),
            'XGetGeometry':(c.c_int,[c.c_void_p,c.c_ulong,c.POINTER(c.c_ulong),c.POINTER(c.c_int),c.POINTER(c.c_int),c.POINTER(c.c_uint),c.POINTER(c.c_uint),c.POINTER(c.c_uint),c.POINTER(c.c_uint)]),
            'XGetImage':(c.POINTER(Image),[c.c_void_p,c.c_ulong,c.c_int,c.c_int,c.c_uint,c.c_uint,c.c_ulong,c.c_int]),'XDestroyImage':(c.c_int,[c.POINTER(Image)]),'XCloseDisplay':(c.c_int,[c.c_void_p])}
        for name,(result,args) in signatures.items():
            fn=getattr(self.lib,name);fn.restype=result;fn.argtypes=args
        self.display=self.lib.XOpenDisplay(None)
        if not self.display:raise RuntimeError('XOpenDisplay failed')
        self.root=self.lib.XDefaultRootWindow(self.display)
        self.last_time=1
        self.error=0
        self.error_callback=c.CFUNCTYPE(c.c_int,c.c_void_p,c.c_void_p)(self._error)
        self.lib.XSetErrorHandler.argtypes=[c.c_void_p];self.lib.XSetErrorHandler.restype=c.c_void_p
        self.previous_error_handler=self.lib.XSetErrorHandler(self.error_callback)
    def _error(self,display,event):
        self.error+=1
        return 0
    def visible(self,window):
        # i3 may unmap a floating client when a user changes workspace. Move
        # only this test client; never request keyboard focus.
        if shutil.which('i3-msg'):
            subprocess.run(['i3-msg',f'[id={window}] move window to workspace current'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,timeout=5)
        self.lib.XRaiseWindow(self.display,window);self.lib.XSync(self.display,False)
    def windows(self,title):
        found=[]
        def visit(window,level):
            name=c.c_void_p()
            if self.lib.XFetchName(self.display,window,c.byref(name)) and name.value:
                try:
                    if c.string_at(name).decode(errors='replace')==title:found.append(window)
                finally:self.lib.XFree(name)
            if level>3:return
            root,parent=c.c_ulong(),c.c_ulong();children=c.POINTER(c.c_ulong)();count=c.c_uint()
            if self.lib.XQueryTree(self.display,window,c.byref(root),c.byref(parent),c.byref(children),c.byref(count)):
                try:
                    for i in range(count.value):visit(children[i],level+1)
                finally:
                    if children:self.lib.XFree(children)
        visit(self.root,0)
        return found
    def geometry(self,window):
        root=c.c_ulong();x,y=c.c_int(),c.c_int();w,h,border,depth=[c.c_uint() for _ in range(4)]
        assert self.lib.XGetGeometry(self.display,window,c.byref(root),c.byref(x),c.byref(y),c.byref(w),c.byref(h),c.byref(border),c.byref(depth))
        return w.value,h.value
    def resize(self,window,w,h):
        self.lib.XMoveResizeWindow(self.display,window,40,40,w,h);self.lib.XRaiseWindow(self.display,window);self.lib.XFlush(self.display)
        deadline=time.monotonic()+4
        while self.geometry(window)!=(w,h):
            if time.monotonic()>deadline:raise RuntimeError('WM refused resize; float the fixture or use a non-tiling test session')
            time.sleep(.05)
        time.sleep(.15)
    def image(self,window):
        self.visible(window);time.sleep(.12)
        w,h=self.geometry(window);image=self.lib.XGetImage(self.display,window,0,0,w,h,c.c_ulong(-1).value,2)
        if not image:raise RuntimeError('XGetImage failed; keep fixture visible')
        try:
            info=image.contents
            if info.bits_per_pixel!=32 or info.byte_order!=0 or (info.red_mask,info.green_mask,info.blue_mask)!=(0xff0000,0xff00,0xff):raise RuntimeError('test capture requires 32-bit little-endian RGB X11 visual')
            raw=c.string_at(info.data,info.bytes_per_line*h)
            rgb=bytearray(w*h*3)
            for row in range(h):
                pixels=raw[row*info.bytes_per_line:row*info.bytes_per_line+w*4]
                at=row*w*3;rgb[at:at+w*3:3]=pixels[2::4];rgb[at+1:at+w*3:3]=pixels[1::4];rgb[at+2:at+w*3:3]=pixels[0::4]
            return (w,h,bytes(rgb))
        finally:self.lib.XDestroyImage(image)
    def send(self,window,kind,code=0,x=0,y=0,state=0):
        event=Event();key=event.key;key.type=kind;key.display=self.display;key.window=window;key.root=self.root
        key.time=self.last_time;self.last_time+=1;key.x=x;key.y=y;key.x_root=x+40;key.y_root=y+40;key.state=state;key.same_screen=1
        key.code=self.lib.XKeysymToKeycode(self.display,code) if kind in (2,3) else code
        assert self.lib.XSendEvent(self.display,window,False,{2:1,3:2,4:4,5:8,6:64}[kind],c.byref(event));self.lib.XFlush(self.display)
    def key(self,window,code):
        self.send(window,2,code);time.sleep(.05);self.send(window,3,code);time.sleep(.12)
    def click(self,window,x,y):
        self.send(window,6,x=x,y=y);self.send(window,4,1,x,y);time.sleep(.06);self.send(window,5,1,x,y);time.sleep(.18)
    def close(self,window):
        event=Event();event.client.type=33;event.client.display=self.display;event.client.window=window
        event.client.message_type=self.lib.XInternAtom(self.display,b'WM_PROTOCOLS',False);event.client.format=32
        event.client.data.l[0]=self.lib.XInternAtom(self.display,b'WM_DELETE_WINDOW',False)
        self.lib.XSendEvent(self.display,window,False,0,c.byref(event));self.lib.XFlush(self.display)
    def shutdown(self):
        self.lib.XCloseDisplay(self.display)
        self.lib.XSetErrorHandler(self.previous_error_handler)
