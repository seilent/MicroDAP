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

SERVICE = 'net.seilent.MicroOs.Power'
PATH = '/net/seilent/MicroOs/Power'
POWER = 'net.seilent.MicroOs.Power1'
CONTROL = 'net.seilent.MicroDap.FakePowerd'
CONTROL_PATH = '/net/seilent/MicroDap/FakePowerd'
MODES = ('dap', 'dap-off')


class Power(dbus.service.Object):
    def __init__(self, bus):
        super().__init__(bus, PATH)
        self.bus = bus
        self.holds = {}
        self.calls = []
        bus.add_signal_receiver(self.on_owner, 'NameOwnerChanged', 'org.freedesktop.DBus',
                                'org.freedesktop.DBus', '/org/freedesktop/DBus')

    def on_owner(self, name, old, new):
        if name.startswith(':') and not new:
            self.holds.pop(str(name), None)

    @dbus.service.method(POWER, in_signature='s', out_signature='', sender_keyword='sender')
    def Hold(self, mode, sender=None):
        self.calls.append('Hold %s' % mode)
        if mode not in MODES:
            raise dbus.exceptions.DBusException('not a hold mode', name='org.freedesktop.DBus.Error.InvalidArgs')
        self.holds[str(sender)] = str(mode)

    @dbus.service.method(POWER, in_signature='s', out_signature='', sender_keyword='sender')
    def Release(self, mode, sender=None):
        self.calls.append('Release %s' % mode)
        if mode not in MODES:
            raise dbus.exceptions.DBusException('not a hold mode', name='org.freedesktop.DBus.Error.InvalidArgs')
        self.holds.pop(str(sender), None)


class Control(dbus.service.Object):
    def __init__(self, bus, power, loop):
        super().__init__(bus, CONTROL_PATH)
        self.power = power
        self.loop = loop

    @dbus.service.method(CONTROL, in_signature='', out_signature='as')
    def Holds(self):
        return dbus.Array(sorted(set(self.power.holds.values())), signature='s')

    @dbus.service.method(CONTROL, in_signature='', out_signature='as')
    def Calls(self):
        return dbus.Array(self.power.calls, signature='s')

    @dbus.service.method(CONTROL, in_signature='', out_signature='')
    def Clear(self):
        self.power.calls = []

    @dbus.service.method(CONTROL, in_signature='', out_signature='')
    def Quit(self):
        GLib.idle_add(self.loop.quit)


def main():
    DBusGMainLoop(set_as_default=True)
    loop = GLib.MainLoop()
    bus = dbus.SessionBus()
    power = Power(bus)
    control = Control(bus, power, loop)
    name = dbus.service.BusName(SERVICE, bus)
    for sig in (signal.SIGTERM, signal.SIGINT):
        unix_signal_add(GLib.PRIORITY_DEFAULT, sig, lambda: loop.quit() or False)
    loop.run()
    del control
    del name


if __name__ == '__main__':
    main()
