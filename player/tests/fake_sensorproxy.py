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

SERVICE = 'net.hadess.SensorProxy'
PATH = '/net/hadess/SensorProxy'
INTERFACE = 'net.hadess.SensorProxy'
PROPERTIES = 'org.freedesktop.DBus.Properties'
CONTROL = 'org.casdap.FakeSensorProxy'
CONTROL_PATH = '/org/casdap/FakeSensorProxy'


class InvalidArgs(dbus.DBusException):
    _dbus_error_name = 'org.freedesktop.DBus.Error.InvalidArgs'


class State:
    def __init__(self):
        self.claims = 0
        self.releases = 0
        self.claim_delay_ms = 0
        self.props = {
            'HasAccelerometer': dbus.Boolean(True),
            'AccelerometerOrientation': dbus.String('undefined'),
        }
        self.proxies = []

    def set(self, name, value):
        self.props[name] = value
        for proxy in self.proxies:
            proxy.PropertiesChanged(INTERFACE, dbus.Dictionary({name: value}, signature='sv'),
                                    dbus.Array([], signature='s'))


class Proxy(dbus.service.Object):
    def __init__(self, bus, state):
        super().__init__(bus, PATH)
        self.state = state
        state.proxies.append(self)

    @dbus.service.method(INTERFACE, in_signature='', out_signature='', async_callbacks=('reply', 'error'))
    def ClaimAccelerometer(self, reply, error):
        self.state.claims += 1
        if self.state.claim_delay_ms > 0:
            GLib.timeout_add(self.state.claim_delay_ms, lambda: reply() or False)
        else:
            reply()

    @dbus.service.method(INTERFACE, in_signature='', out_signature='')
    def ReleaseAccelerometer(self):
        self.state.releases += 1

    @dbus.service.method(PROPERTIES, in_signature='ss', out_signature='v')
    def Get(self, interface, name):
        if interface != INTERFACE or name not in self.state.props:
            raise InvalidArgs(f'{interface}.{name}')
        return self.state.props[name]

    @dbus.service.method(PROPERTIES, in_signature='s', out_signature='a{sv}')
    def GetAll(self, interface):
        if interface != INTERFACE:
            raise InvalidArgs(interface)
        return dbus.Dictionary(self.state.props, signature='sv')

    @dbus.service.signal(PROPERTIES, signature='sa{sv}as')
    def PropertiesChanged(self, interface, changed, invalidated):
        pass


class Control(dbus.service.Object):
    def __init__(self, bus, state, loop):
        super().__init__(bus, CONTROL_PATH)
        self.bus = bus
        self.state = state
        self.loop = loop
        self.second = None

    @dbus.service.method(CONTROL, in_signature='b', out_signature='')
    def SetHas(self, has):
        self.state.set('HasAccelerometer', dbus.Boolean(has))

    @dbus.service.method(CONTROL, in_signature='s', out_signature='')
    def SetOrientation(self, value):
        self.state.set('AccelerometerOrientation', dbus.String(value))

    @dbus.service.method(CONTROL, in_signature='u', out_signature='')
    def SetClaimDelay(self, ms):
        self.state.claim_delay_ms = int(ms)

    @dbus.service.method(CONTROL, in_signature='', out_signature='u')
    def Claims(self):
        return dbus.UInt32(self.state.claims)

    @dbus.service.method(CONTROL, in_signature='', out_signature='u')
    def Releases(self):
        return dbus.UInt32(self.state.releases)

    @dbus.service.method(CONTROL, in_signature='', out_signature='')
    def ClearCounts(self):
        self.state.claims = 0
        self.state.releases = 0

    @dbus.service.method(CONTROL, in_signature='', out_signature='')
    def Appear(self):
        self.bus.request_name(SERVICE, dbus.bus.NAME_FLAG_DO_NOT_QUEUE | dbus.bus.NAME_FLAG_ALLOW_REPLACEMENT)

    @dbus.service.method(CONTROL, in_signature='', out_signature='')
    def Handover(self):
        if self.second is None:
            self.second = dbus.SessionBus(private=True)
            Proxy(self.second, self.state)
        self.second.request_name(SERVICE, dbus.bus.NAME_FLAG_DO_NOT_QUEUE | dbus.bus.NAME_FLAG_REPLACE_EXISTING)

    @dbus.service.method(CONTROL, in_signature='', out_signature='')
    def Vanish(self):
        self.bus.release_name(SERVICE)
        if self.second is not None:
            self.second.release_name(SERVICE)

    @dbus.service.method(CONTROL, in_signature='', out_signature='')
    def Quit(self):
        GLib.idle_add(self.loop.quit)


def main():
    DBusGMainLoop(set_as_default=True)
    loop = GLib.MainLoop()
    bus = dbus.SessionBus()
    state = State()
    Proxy(bus, state)
    control = Control(bus, state, loop)
    bus.request_name(CONTROL, dbus.bus.NAME_FLAG_DO_NOT_QUEUE)
    for sig in (signal.SIGTERM, signal.SIGINT):
        unix_signal_add(GLib.PRIORITY_DEFAULT, sig, lambda: loop.quit() or False)
    loop.run()
    del control


if __name__ == '__main__':
    main()
