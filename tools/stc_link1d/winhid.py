"""Windows system HID transport; no driver installation or third-party module."""
import ctypes as c
from ctypes import wintypes as w
import hashlib
import os
from pathlib import Path

if os.name != "nt":
    raise ImportError("Live STC-Link1D access requires Windows")

kernel = c.WinDLL("kernel32.dll", use_last_error=True, winmode=0x800)
hid = c.WinDLL("hid.dll", use_last_error=True, winmode=0x800)
setup = c.WinDLL("setupapi.dll", use_last_error=True, winmode=0x800)
INVALID = c.c_void_p(-1).value


class Guid(c.Structure):
    _fields_ = [("a", w.DWORD), ("b", w.WORD), ("d", w.WORD), ("e", c.c_ubyte * 8)]


class Interface(c.Structure):
    _fields_ = [("size", w.DWORD), ("guid", Guid), ("flags", w.DWORD), ("reserved", c.c_size_t)]


class Attributes(c.Structure):
    _fields_ = [("size", w.DWORD), ("vid", w.WORD), ("pid", w.WORD), ("version", w.WORD)]


class Overlapped(c.Structure):
    _fields_ = [("internal", c.c_size_t), ("internal_high", c.c_size_t),
                ("offset", w.DWORD), ("offset_high", w.DWORD), ("event", w.HANDLE)]


class ProcessEntry(c.Structure):
    _fields_ = [("size", w.DWORD), ("usage", w.DWORD), ("pid", w.DWORD),
                ("heap", c.c_size_t), ("module", w.DWORD), ("threads", w.DWORD),
                ("parent", w.DWORD), ("priority", w.LONG), ("flags", w.DWORD),
                ("exe", w.WCHAR * 260)]


def api(library, name, result, *args):
    fn = getattr(library, name)
    fn.restype, fn.argtypes = result, args
    return fn


close = api(kernel, "CloseHandle", w.BOOL, w.HANDLE)
create = api(kernel, "CreateFileW", w.HANDLE, w.LPCWSTR, w.DWORD, w.DWORD, c.c_void_p, w.DWORD, w.DWORD, w.HANDLE)
event_create = api(kernel, "CreateEventW", w.HANDLE, c.c_void_p, w.BOOL, w.BOOL, w.LPCWSTR)
wait = api(kernel, "WaitForSingleObject", w.DWORD, w.HANDLE, w.DWORD)
cancel = api(kernel, "CancelIoEx", w.BOOL, w.HANDLE, c.POINTER(Overlapped))
result = api(kernel, "GetOverlappedResult", w.BOOL, w.HANDLE, c.POINTER(Overlapped), c.POINTER(w.DWORD), w.BOOL)
read = api(kernel, "ReadFile", w.BOOL, w.HANDLE, c.c_void_p, w.DWORD, c.POINTER(w.DWORD), c.POINTER(Overlapped))
write = api(kernel, "WriteFile", w.BOOL, w.HANDLE, c.c_void_p, w.DWORD, c.POINTER(w.DWORD), c.POINTER(Overlapped))
get_guid = api(hid, "HidD_GetHidGuid", None, c.POINTER(Guid))
get_attrs = api(hid, "HidD_GetAttributes", w.BOOL, w.HANDLE, c.POINTER(Attributes))
get_preparsed = api(hid, "HidD_GetPreparsedData", w.BOOL, w.HANDLE, c.POINTER(c.c_void_p))
free_preparsed = api(hid, "HidD_FreePreparsedData", w.BOOL, c.c_void_p)
get_caps = api(hid, "HidP_GetCaps", w.LONG, c.c_void_p, c.c_void_p)
get_product = api(hid, "HidD_GetProductString", w.BOOL, w.HANDLE, c.c_void_p, w.DWORD)
get_serial = api(hid, "HidD_GetSerialNumberString", w.BOOL, w.HANDLE, c.c_void_p, w.DWORD)
get_set = api(setup, "SetupDiGetClassDevsW", w.HANDLE, c.POINTER(Guid), w.LPCWSTR, w.HWND, w.DWORD)
enum_interface = api(setup, "SetupDiEnumDeviceInterfaces", w.BOOL, w.HANDLE, c.c_void_p, c.POINTER(Guid), w.DWORD, c.POINTER(Interface))
get_detail = api(setup, "SetupDiGetDeviceInterfaceDetailW", w.BOOL, w.HANDLE, c.POINTER(Interface), c.c_void_p, w.DWORD, c.POINTER(w.DWORD), c.c_void_p)
destroy_set = api(setup, "SetupDiDestroyDeviceInfoList", w.BOOL, w.HANDLE)


def checked(handle):
    if handle in (None, INVALID):
        raise c.WinError(c.get_last_error())
    return handle


def attributes(handle):
    attr = Attributes()
    attr.size = c.sizeof(attr)
    if not get_attrs(handle, c.byref(attr)):
        raise c.WinError(c.get_last_error())
    preparsed = c.c_void_p()
    if not get_preparsed(handle, c.byref(preparsed)):
        raise c.WinError(c.get_last_error())
    try:
        caps = c.create_string_buffer(64)
        if get_caps(preparsed, caps) != 0x110000:
            raise OSError("HidP_GetCaps failed")
        sizes = (int.from_bytes(caps.raw[4:6], "little"), int.from_bytes(caps.raw[6:8], "little"))
    finally:
        free_preparsed(preparsed)
    if (attr.vid, attr.pid, *sizes) != (0x34BF, 0x1005, 65, 65):
        raise OSError("Device must be 34BF:1005 with 65-byte input/output reports")
    return attr.version


def devices():
    guid = Guid()
    get_guid(c.byref(guid))
    info = checked(get_set(c.byref(guid), None, None, 0x12))
    found = []
    try:
        index = 0
        while True:
            entry = Interface()
            entry.size = c.sizeof(entry)
            if not enum_interface(info, None, c.byref(guid), index, c.byref(entry)):
                if c.get_last_error() == 259:
                    break
                raise c.WinError(c.get_last_error())
            index += 1
            size = w.DWORD()
            get_detail(info, c.byref(entry), None, 0, c.byref(size), None)
            if size.value < 8:
                raise OSError("Invalid HID interface detail size")
            detail = c.create_string_buffer(size.value)
            c.cast(detail, c.POINTER(w.DWORD))[0] = 8 if c.sizeof(c.c_void_p) == 8 else 6
            if not get_detail(info, c.byref(entry), detail, size, c.byref(size), None):
                raise c.WinError(c.get_last_error())
            path = c.wstring_at(c.addressof(detail) + 4)
            if "vid_34bf&pid_1005&mi_00#" not in path.lower():
                continue
            handle = checked(create(path, 0, 3, None, 3, 0, None))
            try:
                version = attributes(handle)
                product, serial = c.create_unicode_buffer(256), c.create_unicode_buffer(256)
                get_product(handle, product, c.sizeof(product))
                get_serial(handle, serial, c.sizeof(serial))
                found.append({"path": path, "product": product.value, "serial": serial.value,
                              "version": version, "inputBytes": 65, "outputBytes": 65})
            finally:
                close(handle)
    finally:
        destroy_set(info)
    return found


def ensure_official_closed():
    snapshot = api(kernel, "CreateToolhelp32Snapshot", w.HANDLE, w.DWORD, w.DWORD)
    first = api(kernel, "Process32FirstW", w.BOOL, w.HANDLE, c.POINTER(ProcessEntry))
    next_process = api(kernel, "Process32NextW", w.BOOL, w.HANDLE, c.POINTER(ProcessEntry))
    handle = checked(snapshot(2, 0))
    try:
        item = ProcessEntry()
        item.size = c.sizeof(item)
        valid = first(handle, c.byref(item))
        if not valid:
            raise c.WinError(c.get_last_error())
        while valid:
            if item.exe.lower().startswith(("aicube-isp", "aiapp-isp", "stc-isp")):
                raise OSError(f"Close {item.exe} before opening the downloader")
            valid = next_process(handle, c.byref(item))
        if c.get_last_error() != 18:
            raise c.WinError(c.get_last_error())
    finally:
        close(handle)


class Transport:
    def __init__(self, path, trace):
        self.handle = self.lease = None
        self.trace, self.path = trace, path
        ensure_official_closed()
        if path.lower() not in {d["path"].lower() for d in devices()}:
            raise OSError("Explicit device path does not identify a connected Link1D")
        # Same key/directory as the existing StudioX bridge downloader.
        directory = Path(os.environ["LOCALAPPDATA"]) / "MCUStudioX" / "DeviceLocks"
        directory.mkdir(parents=True, exist_ok=True)
        key = hashlib.sha256(path.upper().encode()).hexdigest().upper()
        self.lease = checked(create(str(directory / (key + ".lock")), 0xC0000000, 0, None, 4, 0x80, None))
        try:
            self._open()
        except BaseException:
            self.close()
            raise

    def _open(self):
        self.handle = checked(create(self.path, 0xC0000000, 3, None, 3, 0x40000000, None))
        attributes(self.handle)

    def reopen(self):
        close(self.handle)
        self.handle = None
        self._open()

    def transfer(self, outgoing=None, timeout=3000):
        if outgoing is not None and len(outgoing) != 65:
            raise ValueError("Expected 65-byte report")
        buffer = c.create_string_buffer(65) if outgoing is None else c.create_string_buffer(outgoing, 65)
        ov, count = Overlapped(), w.DWORD()
        ov.event = checked(event_create(None, True, False, None))
        pending = False
        try:
            fn = read if outgoing is None else write
            if outgoing is not None:
                self.trace({"kind": "tx_submit", "hex": outgoing.hex()})
            ok = fn(self.handle, buffer, 65, c.byref(count), c.byref(ov))
            if not ok:
                error = c.get_last_error()
                if error != 997:
                    raise c.WinError(error)
                pending = True
                waiting = wait(ov.event, timeout)
                if waiting == 258:
                    raise TimeoutError(f"HID transfer timed out after {timeout} ms")
                if waiting != 0:
                    raise c.WinError(c.get_last_error())
                if not result(self.handle, c.byref(ov), c.byref(count), False):
                    raise c.WinError(c.get_last_error())
                pending = False
            data = buffer.raw[:count.value]
            self.trace({"kind": "rx" if outgoing is None else "tx_complete", "count": count.value,
                        "hex": data.hex()})
            if count.value != 65:
                raise OSError(f"Short HID transfer: {count.value}/65")
            return data
        finally:
            if pending:
                # Do not release OVERLAPPED/buffer memory until cancellation completes.
                cancel(self.handle, c.byref(ov))
                result(self.handle, c.byref(ov), c.byref(count), True)
            close(ov.event)

    def write(self, data):
        self.transfer(data)

    def read(self, timeout):
        return self.transfer(timeout=timeout)

    def close(self):
        for name in ("handle", "lease"):
            handle = getattr(self, name, None)
            if handle is not None:
                close(handle)
                setattr(self, name, None)

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()
