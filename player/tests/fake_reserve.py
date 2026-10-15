#!/usr/bin/python3
import signal
import time

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

SERVICE = 'org.freedesktop.ReserveDevice1.Audio1'
PATH = '/org/freedesktop/ReserveDevice1/Audio1'
INTERFACE = 'org.freedesktop.ReserveDevice1'
CONTROL = 'net.seilent.MicroDap.FakeReserve'
CONTROL_PATH = '/control'
BUS_SERVICE = 'org.freedesktop.DBus'
BUS_PATH = '/org/freedesktop/DBus'
CALL_TIMEOUT_S = 60.0


class Fake:
    def __init__(self, bus):
        self.bus = bus
        self.me = bus.get_unique_name()
        self.reset()
        bus.add_signal_receiver(self.owner_changed, signal_name='NameOwnerChanged', dbus_interface=BUS_SERVICE,
                                bus_name=BUS_SERVICE, path=BUS_PATH, arg0=SERVICE)

    def reset(self):
        self.answer = True
        self.stuck = False
        self.calls = 0
        self.last_reply_ms = -1
        self.last_error = ''
        self.last_answer = False
        self.pending = set()

    def owner(self):
        try:
            return str(self.bus.get_name_owner(SERVICE))
        except dbus.DBusException:
            return ''

    def take(self, replace=False):
        flags = dbus.bus.NAME_FLAG_DO_NOT_QUEUE | dbus.bus.NAME_FLAG_ALLOW_REPLACEMENT
        if replace:
            flags |= dbus.bus.NAME_FLAG_REPLACE_EXISTING
        self.bus.request_name(SERVICE, flags)

    def owner_changed(self, name, old, new):
        if new == self.me:
            return
        if new == '':
            if old == self.me:
                return
            if self.pending:
                self.stuck = True
                return
            self.take()
            return
        self.request(str(new), -20)

    def request(self, owner, priority):
        self.calls += 1
        self.last_reply_ms = -1
        self.last_error = ''
        sent = time.monotonic()
        token = object()
        self.pending.add(token)

        def finish():
            self.pending.discard(token)
            self.last_reply_ms = int((time.monotonic() - sent) * 1000)

        def on_reply(answer):
            finish()
            self.last_answer = bool(answer)
            if self.last_answer:
                self.stuck = False
                self.take(replace=True)

        def on_error(error):
            finish()
            self.last_answer = False
            self.last_error = error.get_dbus_name() or 'error'
            if self.stuck and not self.pending:
                self.stuck = False
                self.take()

        self.bus.call_async(owner, PATH, INTERFACE, 'RequestRelease', 'i', (dbus.Int32(priority),),
                            on_reply, on_error, timeout=CALL_TIMEOUT_S)


class Device(dbus.service.Object):
    def __init__(self, bus, fake):
        super().__init__(bus, PATH)
        self.fake = fake

    @dbus.service.method(INTERFACE, in_signature='i', out_signature='b')
    def RequestRelease(self, priority):
        return dbus.Boolean(self.fake.answer)


class Control(dbus.service.Object):
    def __init__(self, bus, fake, loop):
        super().__init__(bus, CONTROL_PATH)
        self.fake = fake
        self.loop = loop

    @dbus.service.method(CONTROL, in_signature='b', out_signature='')
    def SetAnswer(self, answer):
        self.fake.answer = bool(answer)

    @dbus.service.method(CONTROL, in_signature='', out_signature='')
    def Replug(self):
        owner = self.fake.owner()
        if owner and owner != self.fake.me:
            self.fake.request(owner, -20)

    @dbus.service.method(CONTROL, in_signature='i', out_signature='')
    def Request(self, priority):
        owner = self.fake.owner()
        if owner and owner != self.fake.me:
            self.fake.request(owner, int(priority))

    @dbus.service.method(CONTROL, in_signature='', out_signature='')
    def Steal(self):
        self.fake.take(replace=True)

    @dbus.service.method(CONTROL, in_signature='', out_signature='sbuisbu')
    def State(self):
        owner = self.fake.owner()
        if owner == self.fake.me:
            owner = 'fake'
        return (dbus.String(owner), dbus.Boolean(self.fake.stuck), dbus.UInt32(self.fake.calls),
                dbus.Int32(self.fake.last_reply_ms), dbus.String(self.fake.last_error),
                dbus.Boolean(self.fake.last_answer), dbus.UInt32(len(self.fake.pending)))

    @dbus.service.method(CONTROL, in_signature='', out_signature='')
    def Reset(self):
        self.fake.reset()
        if not self.fake.owner():
            self.fake.take()

    @dbus.service.method(CONTROL, in_signature='', out_signature='')
    def Quit(self):
        GLib.idle_add(self.loop.quit)


def main():
    DBusGMainLoop(set_as_default=True)
    loop = GLib.MainLoop()
    bus = dbus.SessionBus()
    fake = Fake(bus)
    device = Device(bus, fake)
    control = Control(bus, fake, loop)
    fake.take()
    bus.request_name(CONTROL, dbus.bus.NAME_FLAG_DO_NOT_QUEUE)
    for sig in (signal.SIGTERM, signal.SIGINT):
        unix_signal_add(GLib.PRIORITY_DEFAULT, sig, lambda: loop.quit() or False)
    loop.run()
    del control
    del device


if __name__ == '__main__':
    main()
