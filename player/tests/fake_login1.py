#!/usr/bin/python3
import os
import signal

import dbus
import dbus.service
import gi
from dbus.mainloop.glib import DBusGMainLoop
from gi.repository import GLib

try:
    gi.require_version('GLibUnix', '2.0')
    from gi.repository import GLibUnix
    unix_signal_add = GLibUnix.signal_add
except (ImportError, ValueError, AttributeError):
    unix_signal_add = GLib.unix_signal_add

SERVICE = 'org.freedesktop.login1'
PATH = '/org/freedesktop/login1'
MANAGER = 'org.freedesktop.login1.Manager'
CONTROL = 'net.seilent.MicroDap.FakeLogin1'
CONTROL_PATH = '/net/seilent/MicroDap/FakeLogin1'


class Manager(dbus.service.Object):
    def __init__(self, bus):
        super().__init__(bus, PATH)
        self.inhibits = []
        self.open_fds = {}
        self.count = 0

    @dbus.service.method(MANAGER, in_signature='ssss', out_signature='h')
    def Inhibit(self, what, who, why, mode):
        reader, writer = os.pipe()
        self.count += 1
        self.inhibits.append((str(what), str(who), str(why), str(mode)))
        self.open_fds[reader] = GLib.io_add_watch(reader, GLib.PRIORITY_DEFAULT, GLib.IO_HUP | GLib.IO_ERR,
                                                  self.on_hup)
        fd = dbus.types.UnixFd(writer)
        os.close(writer)
        return fd

    def on_hup(self, source, condition):
        self.open_fds.pop(source, None)
        os.close(source)
        return False

    @dbus.service.signal(MANAGER, signature='b')
    def PrepareForSleep(self, start):
        pass


class Control(dbus.service.Object):
    def __init__(self, bus, manager, loop):
        super().__init__(bus, CONTROL_PATH)
        self.manager = manager
        self.loop = loop

    @dbus.service.method(CONTROL, in_signature='', out_signature='u')
    def InhibitCount(self):
        return self.manager.count

    @dbus.service.method(CONTROL, in_signature='', out_signature='u')
    def OpenFds(self):
        return len(self.manager.open_fds)

    @dbus.service.method(CONTROL, in_signature='', out_signature='as')
    def LastInhibit(self):
        if not self.manager.inhibits:
            return dbus.Array([], signature='s')
        return dbus.Array([dbus.String(s) for s in self.manager.inhibits[-1]], signature='s')

    @dbus.service.method(CONTROL, in_signature='b', out_signature='')
    def EmitPrepareForSleep(self, start):
        self.manager.PrepareForSleep(dbus.Boolean(start))

    @dbus.service.method(CONTROL, in_signature='', out_signature='')
    def Quit(self):
        GLib.idle_add(self.loop.quit)


def main():
    DBusGMainLoop(set_as_default=True)
    loop = GLib.MainLoop()
    bus = dbus.SessionBus()
    manager = Manager(bus)
    control = Control(bus, manager, loop)
    name = dbus.service.BusName(SERVICE, bus)
    for sig in (signal.SIGTERM, signal.SIGINT):
        unix_signal_add(GLib.PRIORITY_DEFAULT, sig, lambda: loop.quit() or False)
    loop.run()
    del control
    del name


if __name__ == '__main__':
    main()
