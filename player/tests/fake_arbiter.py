#!/usr/bin/python3
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

SERVICE = 'net.seilent.MicroOs.Intercept'
PATH = '/net/seilent/MicroOs/Intercept'
INTERFACE = 'net.seilent.MicroOs.Intercept1'
PROPERTIES = 'org.freedesktop.DBus.Properties'
CONTROL = 'net.seilent.MicroDap.FakeArbiter'


class InvalidArgs(dbus.DBusException):
    _dbus_error_name = 'org.freedesktop.DBus.Error.InvalidArgs'


class Arbiter(dbus.service.Object):
    def __init__(self, bus, loop):
        super().__init__(bus, PATH)
        self.bus = bus
        self.loop = loop
        self.stack = []
        self.calls = []
        self.watches = {}

    def holder(self):
        return self.stack[-1] if self.stack else ''

    def changed(self, before):
        now = self.holder()
        if now != before:
            self.PropertiesChanged(INTERFACE, dbus.Dictionary({'Holder': dbus.String(now)}, signature='sv'),
                                   dbus.Array([], signature='s'))

    def drop(self, name):
        self.stack.remove(name)
        self.watches.pop(name).cancel()

    def vanished(self, name, owner):
        if owner or name not in self.stack:
            return
        before = self.holder()
        self.drop(name)
        self.changed(before)

    @dbus.service.method(INTERFACE, in_signature='', out_signature='', sender_keyword='sender')
    def Acquire(self, sender=None):
        self.calls.append(f'Acquire {sender}')
        before = self.holder()
        if sender not in self.stack:
            self.stack.append(sender)
            self.watches[sender] = self.bus.watch_name_owner(sender, lambda o, n=sender: self.vanished(n, o))
        self.changed(before)

    @dbus.service.method(INTERFACE, in_signature='', out_signature='', sender_keyword='sender')
    def Release(self, sender=None):
        self.calls.append(f'Release {sender}')
        before = self.holder()
        if sender in self.stack:
            self.drop(sender)
        self.changed(before)

    @dbus.service.method(PROPERTIES, in_signature='ss', out_signature='v')
    def Get(self, interface, name):
        if interface != INTERFACE or name != 'Holder':
            raise InvalidArgs(f'{interface}.{name}')
        return dbus.String(self.holder())

    @dbus.service.method(PROPERTIES, in_signature='s', out_signature='a{sv}')
    def GetAll(self, interface):
        if interface != INTERFACE:
            raise InvalidArgs(interface)
        return dbus.Dictionary({'Holder': dbus.String(self.holder())}, signature='sv')

    @dbus.service.signal(PROPERTIES, signature='sa{sv}as')
    def PropertiesChanged(self, interface, changed, invalidated):
        pass

    @dbus.service.method(CONTROL, in_signature='', out_signature='as')
    def Calls(self):
        return dbus.Array([dbus.String(c) for c in self.calls], signature='s')

    @dbus.service.method(CONTROL, in_signature='', out_signature='')
    def ClearCalls(self):
        self.calls = []

    @dbus.service.method(CONTROL, in_signature='', out_signature='')
    def Quit(self):
        GLib.idle_add(self.loop.quit)


def main():
    DBusGMainLoop(set_as_default=True)
    loop = GLib.MainLoop()
    bus = dbus.SessionBus()
    arbiter = Arbiter(bus, loop)
    name = dbus.service.BusName(SERVICE, bus)
    for sig in (signal.SIGTERM, signal.SIGINT):
        unix_signal_add(GLib.PRIORITY_DEFAULT, sig, lambda: loop.quit() or False)
    loop.run()
    del name, arbiter


if __name__ == '__main__':
    main()
