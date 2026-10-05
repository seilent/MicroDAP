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

SERVICE = 'org.shadowblip.InputPlumber'
ROOT = '/org/shadowblip/InputPlumber'
OBJECT_MANAGER = 'org.freedesktop.DBus.ObjectManager'
PROPERTIES = 'org.freedesktop.DBus.Properties'
COMPOSITE = 'org.shadowblip.Input.CompositeDevice'
DBUS_DEVICE = 'org.shadowblip.Input.DBusDevice'
CONTROL = 'org.casdap.FakeInputPlumber'
CONTROL_PATH = '/org/casdap/FakeInputPlumber'
TARGET_PREFIX = ROOT + '/devices/target/'


class InvalidArgs(dbus.DBusException):
    _dbus_error_name = 'org.freedesktop.DBus.Error.InvalidArgs'


class UnknownObject(dbus.DBusException):
    _dbus_error_name = 'org.freedesktop.DBus.Error.UnknownObject'


class CompositeDevice(dbus.service.Object):
    def __init__(self, bus, path):
        super().__init__(bus, path)
        self.path = path
        self.props = {
            'Name': dbus.String('Fake Composite'),
            'InterceptMode': dbus.UInt32(0),
            'TargetDevices': dbus.Array([dbus.String('xb360'), dbus.String('keyboard')], signature='s'),
        }

    def interfaces(self):
        return dbus.Dictionary({COMPOSITE: dbus.Dictionary(self.props, signature='sv')}, signature='sa{sv}')

    @dbus.service.method(PROPERTIES, in_signature='ss', out_signature='v')
    def Get(self, interface, name):
        if interface != COMPOSITE or name not in self.props:
            raise InvalidArgs(f'{interface}.{name}')
        return self.props[name]

    @dbus.service.method(PROPERTIES, in_signature='s', out_signature='a{sv}')
    def GetAll(self, interface):
        if interface != COMPOSITE:
            raise InvalidArgs(interface)
        return dbus.Dictionary(self.props, signature='sv')

    @dbus.service.method(PROPERTIES, in_signature='ssv', out_signature='')
    def Set(self, interface, name, value):
        if interface != COMPOSITE or name != 'InterceptMode':
            raise InvalidArgs(f'{interface}.{name}')
        if not isinstance(value, dbus.UInt32):
            raise InvalidArgs(f'{name}: {type(value).__name__}')
        self.props[name] = dbus.UInt32(value)
        self.PropertiesChanged(COMPOSITE, dbus.Dictionary({name: self.props[name]}, signature='sv'),
                               dbus.Array([], signature='s'))

    @dbus.service.signal(PROPERTIES, signature='sa{sv}as')
    def PropertiesChanged(self, interface, changed, invalidated):
        pass


class DBusTarget(dbus.service.Object):
    def __init__(self, bus, path):
        super().__init__(bus, path)
        self.path = path
        self.props = {'Name': dbus.String('Fake DBus Target')}

    def interfaces(self):
        return dbus.Dictionary({DBUS_DEVICE: dbus.Dictionary(self.props, signature='sv')}, signature='sa{sv}')

    @dbus.service.method(PROPERTIES, in_signature='ss', out_signature='v')
    def Get(self, interface, name):
        if interface != DBUS_DEVICE or name not in self.props:
            raise InvalidArgs(f'{interface}.{name}')
        return self.props[name]

    @dbus.service.method(PROPERTIES, in_signature='s', out_signature='a{sv}')
    def GetAll(self, interface):
        if interface != DBUS_DEVICE:
            raise InvalidArgs(interface)
        return dbus.Dictionary(self.props, signature='sv')

    @dbus.service.signal(DBUS_DEVICE, signature='sd')
    def InputEvent(self, name, value):
        pass


class Root(dbus.service.Object):
    def __init__(self, bus, fake):
        super().__init__(bus, ROOT)
        self.fake = fake

    @dbus.service.method(OBJECT_MANAGER, in_signature='', out_signature='a{oa{sa{sv}}}')
    def GetManagedObjects(self):
        objects = dbus.Dictionary(signature='oa{sa{sv}}')
        for path, obj in self.fake.objects.items():
            objects[dbus.ObjectPath(path)] = obj.interfaces()
        return objects

    @dbus.service.signal(OBJECT_MANAGER, signature='oa{sa{sv}}')
    def InterfacesAdded(self, path, interfaces):
        pass

    @dbus.service.signal(OBJECT_MANAGER, signature='oas')
    def InterfacesRemoved(self, path, interfaces):
        pass


class Control(dbus.service.Object):
    def __init__(self, bus, fake, loop):
        super().__init__(bus, CONTROL_PATH)
        self.fake = fake
        self.loop = loop

    @dbus.service.method(CONTROL, in_signature='s', out_signature='u')
    def GetMode(self, path):
        obj = self.fake.objects.get(str(path))
        if not isinstance(obj, CompositeDevice):
            raise UnknownObject(str(path))
        return obj.props['InterceptMode']

    @dbus.service.method(CONTROL, in_signature='', out_signature='as')
    def Devices(self):
        return dbus.Array([dbus.String(p) for p, o in self.fake.objects.items() if isinstance(o, CompositeDevice)],
                          signature='s')

    @dbus.service.method(CONTROL, in_signature='', out_signature='s')
    def AddDevice(self):
        return self.fake.add_device()

    @dbus.service.method(CONTROL, in_signature='s', out_signature='s')
    def AddTarget(self, name):
        return self.fake.add_target(str(name))

    @dbus.service.method(CONTROL, in_signature='s', out_signature='')
    def RemoveTarget(self, name):
        self.fake.remove_target(str(name))

    @dbus.service.method(CONTROL, in_signature='ssd', out_signature='')
    def Emit(self, target, name, value):
        obj = self.fake.objects.get(TARGET_PREFIX + str(target))
        if not isinstance(obj, DBusTarget):
            raise UnknownObject(str(target))
        obj.InputEvent(dbus.String(name), dbus.Double(value))

    @dbus.service.method(CONTROL, in_signature='', out_signature='')
    def Quit(self):
        GLib.idle_add(self.loop.quit)


class Fake:
    def __init__(self, bus, loop):
        self.bus = bus
        self.objects = {}
        self.next_device = 0
        self.root = Root(bus, self)
        self.control = Control(bus, self, loop)
        self.add_device(announce=False)
        self.add_device(announce=False)
        self.add_target('dbus0', announce=False)
        self.add_target('dbus1', announce=False)
        self.name = dbus.service.BusName(SERVICE, bus)

    def add_device(self, announce=True):
        path = f'{ROOT}/CompositeDevice{self.next_device}'
        self.next_device += 1
        device = CompositeDevice(self.bus, path)
        self.objects[path] = device
        if announce:
            self.root.InterfacesAdded(dbus.ObjectPath(path), device.interfaces())
        return path

    def add_target(self, name, announce=True):
        path = TARGET_PREFIX + name
        if path in self.objects:
            return path
        target = DBusTarget(self.bus, path)
        self.objects[path] = target
        if announce:
            self.root.InterfacesAdded(dbus.ObjectPath(path), target.interfaces())
        return path

    def remove_target(self, name):
        path = TARGET_PREFIX + name
        target = self.objects.pop(path, None)
        if target is None:
            raise UnknownObject(path)
        target.remove_from_connection()
        self.root.InterfacesRemoved(dbus.ObjectPath(path), dbus.Array([dbus.String(DBUS_DEVICE)], signature='s'))


def main():
    DBusGMainLoop(set_as_default=True)
    loop = GLib.MainLoop()
    bus = dbus.SessionBus()
    fake = Fake(bus, loop)
    for sig in (signal.SIGTERM, signal.SIGINT):
        unix_signal_add(GLib.PRIORITY_DEFAULT, sig, lambda: loop.quit() or False)
    loop.run()
    del fake


if __name__ == '__main__':
    main()
