"""Link to a ForgeBox running our firmware: real USB (1209:3001) or the host emulator.

Both expose the same transact(cmd, data) -> (cmd, status, payload) over Keystone's EAPDU framing
(see agave remote-wallet/src/keystone.rs). The emulator is the firmware's own protocol + wallet
code compiled for Linux (fw/tests/emulator), speaking [len][packet] over a pipe.
"""
import os
import select
import struct
import subprocess

HDR = 9
MAX_REQ_DATA = 64 - HDR
REQUEST_ID = 0x1234

CMD_ECHO = 0x0001
CMD_INFO = 0x0005
CMD_FB_SOL_ADDRESS = 0x0100
CMD_FB_SIGN_SOL_MESSAGE = 0x0101

STATUS_NAMES = {0x0000: "success", 0x0001: "failure", 0x0004: "rejected", 0x0005: "parsing error",
                0x0006: "not implemented"}


class LinkTimeout(Exception):
    pass


class UsbLink:
    def __init__(self):
        import usb.core
        import usb.util
        self._usb = usb
        dev = usb.core.find(idVendor=0x1209, idProduct=0x3001)
        if dev is None:
            raise SystemExit("ERROR: no device 1209:3001 found (plugged in? running our firmware?)")
        try:
            if dev.is_kernel_driver_active(0):
                dev.detach_kernel_driver(0)
        except (NotImplementedError, usb.core.USBError):
            pass
        dev.set_configuration()
        intf = dev.get_active_configuration()[(0, 0)]
        is_out = lambda e: usb.util.endpoint_direction(e.bEndpointAddress) == usb.util.ENDPOINT_OUT
        self.ep_out = usb.util.find_descriptor(intf, custom_match=is_out)
        self.ep_in = usb.util.find_descriptor(intf, custom_match=lambda e: not is_out(e))
        self.name = f"USB {usb.util.get_string(dev, dev.iManufacturer)} / {usb.util.get_string(dev, dev.iProduct)}"

    def write(self, packet):
        self.ep_out.write(packet, timeout=1000)

    def read(self, timeout_ms):
        try:
            return bytes(self.ep_in.read(64, timeout=timeout_ms))
        except self._usb.core.USBTimeoutError:
            raise LinkTimeout()


class EmulatorLink:
    def __init__(self, binary=None):
        binary = binary or os.environ.get("FB_EMU", "/tmp/fb_emu")
        if not os.path.exists(binary):
            raise SystemExit(f"ERROR: emulator {binary} missing; run tests/emulator/build.sh")
        self.proc = subprocess.Popen([binary], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
        self.name = f"emulator {binary}"

    def write(self, packet):
        self.proc.stdin.write(bytes([len(packet)]) + packet)
        self.proc.stdin.flush()

    def _read_exact(self, n, timeout_ms):
        out = b""
        fd = self.proc.stdout.fileno()
        while len(out) < n:
            ready, _, _ = select.select([fd], [], [], timeout_ms / 1000)
            if not ready:
                raise LinkTimeout()
            chunk = os.read(fd, n - len(out))
            if not chunk:
                raise SystemExit("ERROR: emulator exited")
            out += chunk
        return out

    def read(self, timeout_ms):
        n = self._read_exact(1, timeout_ms)[0]
        return self._read_exact(n, timeout_ms)


def open_link(emulator=False):
    return EmulatorLink() if emulator else UsbLink()


def drain(link):
    while True:
        try:
            link.read(50)
        except LinkTimeout:
            return


def send(link, cmd, data=b""):
    total = max(1, -(-len(data) // MAX_REQ_DATA))
    for i in range(total):
        chunk = data[i * MAX_REQ_DATA:(i + 1) * MAX_REQ_DATA]
        link.write(struct.pack(">BHHHH", 0, cmd, total, i, REQUEST_ID) + chunk)


def receive(link, timeout_ms=3000):
    parts, total, status, cmd = {}, None, None, None
    while total is None or len(parts) < total:
        pkt = link.read(timeout_ms)
        if len(pkt) < HDR + 2:
            raise RuntimeError(f"short packet: {pkt.hex()}")
        _, cmd, total, index, _ = struct.unpack(">BHHHH", pkt[:HDR])
        status = struct.unpack(">H", pkt[-2:])[0]
        parts[index] = pkt[HDR:-2]
    return cmd, status, b"".join(parts[i] for i in range(total))


def transact(link, cmd, data=b"", timeout_ms=3000):
    drain(link)
    send(link, cmd, data)
    return receive(link, timeout_ms)
