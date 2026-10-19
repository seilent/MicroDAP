#!/usr/bin/python3
import os
import signal
import socket
import subprocess
import sys
import threading
import time

import dbus
import dbus.mainloop
import dbus.mainloop.glib
import dbus.service
from gi.repository import GLib

ADDRESS = os.environ['DBUS_SESSION_BUS_ADDRESS']
MICRODAP_BT = os.environ['MICRODAP_BT']
LOG = os.environ['MICRODAP_BT_LOG']
KEEP_FILE = os.environ['MICRODAP_KEEP_FILE']
BOOT_ID_FILE = os.environ['MICRODAP_BOOT_ID_FILE']
BOOT_ID = '0f6f1a2e-5d1c-4a1b-9c3e-7b2d8e4f6a10'

BLUEZ = 'org.bluez'
OBJECT_MANAGER = 'org.freedesktop.DBus.ObjectManager'
PROPERTIES = 'org.freedesktop.DBus.Properties'
ADAPTER = 'org.bluez.Adapter1'
DEVICE = 'org.bluez.Device1'
AGENT = 'org.bluez.Agent1'
AGENT_MANAGER = 'org.bluez.AgentManager1'
AGENT_PATH = '/net/seilent/MicroDap/agent'
SERVICE = 'net.seilent.MicroDap.Bluetooth'
SERVICE_PATH = '/net/seilent/MicroDap/Bluetooth'
ADAPTER_PATH = '/org/bluez/hci0'
REJECTED = 'org.bluez.Error.Rejected'
INVALID_ARGS = 'org.freedesktop.DBus.Error.InvalidArgs'
BASE_UUID = '-0000-1000-8000-00805f9b34fb'
PHONE = ADAPTER_PATH + '/dev_00_11_22_33_44_55'


def uuid(short):
    return '0000' + short + BASE_UUID


class InvalidArgs(dbus.DBusException):
    _dbus_error_name = 'org.freedesktop.DBus.Error.InvalidArgs'


class DoesNotExist(dbus.DBusException):
    _dbus_error_name = 'org.bluez.Error.DoesNotExist'


class Root(dbus.service.Object):
    def __init__(self, bus, fake):
        super().__init__(bus, '/')
        self.fake = fake

    @dbus.service.method(OBJECT_MANAGER, in_signature='', out_signature='a{oa{sa{sv}}}')
    def GetManagedObjects(self):
        objects = dbus.Dictionary(signature='oa{sa{sv}}')
        objects[dbus.ObjectPath(ADAPTER_PATH)] = dbus.Dictionary(
            {ADAPTER: dbus.Dictionary(self.fake.adapter.props, signature='sv')}, signature='sa{sv}')
        for path, device in self.fake.devices.items():
            objects[dbus.ObjectPath(path)] = dbus.Dictionary(
                {DEVICE: dbus.Dictionary(device.props, signature='sv')}, signature='sa{sv}')
        return objects


class Manager(dbus.service.Object):
    def __init__(self, bus, fake):
        super().__init__(bus, '/org/bluez')
        self.fake = fake

    @dbus.service.method(AGENT_MANAGER, in_signature='os', out_signature='', sender_keyword='sender')
    def RegisterAgent(self, path, capability, sender=None):
        self.fake.agent = (str(path), str(capability), str(sender))

    @dbus.service.method(AGENT_MANAGER, in_signature='o', out_signature='')
    def UnregisterAgent(self, path):
        self.fake.agent = None

    @dbus.service.method(AGENT_MANAGER, in_signature='o', out_signature='')
    def RequestDefaultAgent(self, path):
        self.fake.default_agent = str(path)
        self.fake.adapter.props['Pairable'] = dbus.Boolean(True)


class Adapter(dbus.service.Object):
    def __init__(self, bus, fake):
        super().__init__(bus, ADAPTER_PATH)
        self.fake = fake
        self.props = {
            'Address': dbus.String('AA:BB:CC:DD:EE:FF'),
            'Alias': dbus.String('adapter'),
            'Powered': dbus.Boolean(True),
            'Pairable': dbus.Boolean(False),
            'PairableTimeout': dbus.UInt32(30),
            'Discoverable': dbus.Boolean(True),
            'DiscoverableTimeout': dbus.UInt32(180),
        }

    @dbus.service.method(PROPERTIES, in_signature='ss', out_signature='v')
    def Get(self, interface, name):
        if interface != ADAPTER or name not in self.props:
            raise InvalidArgs(f'{interface}.{name}')
        return self.props[name]

    @dbus.service.method(PROPERTIES, in_signature='s', out_signature='a{sv}')
    def GetAll(self, interface):
        if interface != ADAPTER:
            raise InvalidArgs(interface)
        return dbus.Dictionary(self.props, signature='sv')

    @dbus.service.method(PROPERTIES, in_signature='ssv', out_signature='')
    def Set(self, interface, name, value):
        if interface != ADAPTER or name not in self.props:
            raise InvalidArgs(f'{interface}.{name}')
        if type(value) is not type(self.props[name]):
            raise InvalidArgs(f'{name}: {type(value).__name__}')
        self.props[name] = value
        self.PropertiesChanged(ADAPTER, dbus.Dictionary({name: value}, signature='sv'), dbus.Array([], signature='s'))

    @dbus.service.signal(PROPERTIES, signature='sa{sv}as')
    def PropertiesChanged(self, interface, changed, invalidated):
        pass

    @dbus.service.method(ADAPTER, in_signature='a{sv}', out_signature='')
    def SetDiscoveryFilter(self, filter):
        self.fake.discovery.append(('filter', {str(k): str(v) for k, v in filter.items()}))

    @dbus.service.method(ADAPTER, in_signature='', out_signature='')
    def StartDiscovery(self):
        self.fake.discovery.append('start')

    @dbus.service.method(ADAPTER, in_signature='', out_signature='')
    def StopDiscovery(self):
        self.fake.discovery.append('stop')

    @dbus.service.method(ADAPTER, in_signature='o', out_signature='')
    def RemoveDevice(self, path):
        device = self.fake.devices.pop(str(path), None)
        if device is None:
            raise DoesNotExist(str(path))
        device.remove_from_connection()
        self.fake.removed.append(str(path))


class Device(dbus.service.Object):
    def __init__(self, bus, fake, path, uuids):
        super().__init__(bus, path)
        self.fake = fake
        self.path = path
        self.props = {
            'Address': dbus.String(path.rsplit('/', 1)[1][4:].replace('_', ':')),
            'Paired': dbus.Boolean(False),
            'Trusted': dbus.Boolean(False),
            'Connected': dbus.Boolean(False),
            'ServicesResolved': dbus.Boolean(False),
            'UUIDs': dbus.Array(uuids, signature='s'),
        }

    @dbus.service.method(PROPERTIES, in_signature='ss', out_signature='v')
    def Get(self, interface, name):
        if interface != DEVICE or name not in self.props:
            raise InvalidArgs(f'{interface}.{name}')
        return self.props[name]

    @dbus.service.method(PROPERTIES, in_signature='s', out_signature='a{sv}')
    def GetAll(self, interface):
        if interface != DEVICE:
            raise InvalidArgs(interface)
        return dbus.Dictionary(self.props, signature='sv')

    @dbus.service.method(PROPERTIES, in_signature='ssv', out_signature='')
    def Set(self, interface, name, value):
        if interface != DEVICE or name not in self.props:
            raise InvalidArgs(f'{interface}.{name}')
        self.props[name] = value

    @dbus.service.signal(PROPERTIES, signature='sa{sv}as')
    def PropertiesChanged(self, interface, changed, invalidated):
        pass

    @dbus.service.method(DEVICE, in_signature='', out_signature='')
    def Disconnect(self):
        self.fake.disconnected.append(self.path)
        self.flip('Connected', dbus.Boolean(False))

    def flip(self, name, value):
        self.props[name] = value
        self.PropertiesChanged(DEVICE, dbus.Dictionary({name: value}, signature='sv'), dbus.Array([], signature='s'))


class Fake:
    def __init__(self, bus):
        self.bus = bus
        self.devices = {}
        self.removed = []
        self.disconnected = []
        self.discovery = []
        self.agent = None
        self.default_agent = None
        self.signals = []
        self.root = Root(bus, self)
        self.manager = Manager(bus, self)
        self.adapter = Adapter(bus, self)
        bus.add_signal_receiver(self.on_signal, dbus_interface=PROPERTIES, signal_name='PropertiesChanged',
                                arg0=SERVICE)
        self.name = dbus.service.BusName(BLUEZ, bus)

    def on_signal(self, interface, changed, invalidated):
        self.signals.append({str(k): v for k, v in changed.items()})

    def add_device(self, path, uuids):
        device = Device(self.bus, self, path, [dbus.String(u) for u in uuids])
        self.devices[path] = device
        return device

    def flip(self, path, name, value):
        self.devices[path].flip(name, value)


def in_loop(fn, *args):
    done = threading.Event()
    box = {}

    def run():
        try:
            box['result'] = fn(*args)
        except Exception as e:
            box['error'] = e
        done.set()
        return False

    GLib.idle_add(run)
    if not done.wait(5):
        raise TimeoutError(fn.__name__)
    if 'error' in box:
        raise box['error']
    return box.get('result')


def wait_for(cond, seconds):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if cond():
            return True
        time.sleep(0.05)
    return cond()


class Driver:
    def __init__(self, fake):
        self.fake = fake
        self.failures = 0
        self.bus = dbus.bus.BusConnection(ADDRESS, mainloop=dbus.mainloop.NULL_MAIN_LOOP)
        self.adapter = dbus.Interface(self.bus.get_object(BLUEZ, ADAPTER_PATH, introspect=False), PROPERTIES)
        self.agent = None
        self.service = None
        self.service_props = None

    def check(self, cond, message):
        if not cond:
            self.failures += 1
            print(f'FAIL {message}', file=sys.stderr)

    def adapter_get(self, name):
        return self.adapter.Get(ADAPTER, name)

    def service_get(self, name):
        return self.service_props.Get(SERVICE, name)

    def active(self):
        return bool(self.service_get('PairingActive'))

    def expect_rejected(self, what, fn, *args):
        self.expect_error(what, REJECTED, fn, *args)

    def expect_invalid(self, what, fn, *args):
        self.expect_error(what, INVALID_ARGS, fn, *args)

    def expect_error(self, what, name, fn, *args):
        try:
            fn(*args)
        except dbus.DBusException as e:
            self.check(e.get_dbus_name() == name, f'{what}: {e.get_dbus_name()}')
            return
        self.check(False, f'{what} accepted')

    def expect_ok(self, what, fn, *args):
        try:
            fn(*args)
        except dbus.DBusException as e:
            self.check(False, f'{what}: {e.get_dbus_name()}')

    def check_restored(self, label):
        self.check(bool(self.adapter_get('Pairable')) is False, f'{label}: Pairable not restored')
        self.check(int(self.adapter_get('PairableTimeout')) == 30, f'{label}: PairableTimeout not restored')
        self.check(int(self.adapter_get('DiscoverableTimeout')) == 180, f'{label}: DiscoverableTimeout not restored')
        self.check(bool(self.adapter_get('Discoverable')) is False, f'{label}: Discoverable not false')

    def check_open(self, label, seconds):
        self.check(bool(self.adapter_get('Pairable')) is True, f'{label}: Pairable not true')
        self.check(int(self.adapter_get('PairableTimeout')) == seconds, f'{label}: PairableTimeout not {seconds}')
        self.check(bool(self.adapter_get('Discoverable')) is True, f'{label}: Discoverable not true')
        self.check(int(self.adapter_get('DiscoverableTimeout')) == seconds, f'{label}: DiscoverableTimeout not {seconds}')
        self.check(str(self.adapter_get('Alias')) == socket.gethostname(), f'{label}: Alias is {self.adapter_get("Alias")}')
        self.check(self.active() is True, f'{label}: PairingActive not true')

    def connect(self):
        agent = self.bus.get_object(self.fake.agent[2], AGENT_PATH, introspect=False)
        self.agent = dbus.Interface(agent, AGENT)
        service = self.bus.get_object(SERVICE, SERVICE_PATH, introspect=False)
        self.service = dbus.Interface(service, SERVICE)
        self.service_props = dbus.Interface(service, PROPERTIES)

    def test_registration(self):
        self.check(self.fake.agent[0] == AGENT_PATH, f'agent path {self.fake.agent[0]}')
        self.check(self.fake.agent[1] == 'NoInputNoOutput', f'agent capability {self.fake.agent[1]}')
        self.check(self.fake.default_agent == AGENT_PATH, f'default agent {self.fake.default_agent}')
        self.check(bool(self.adapter_get('Discoverable')) is False, 'startup left Discoverable true')
        self.check(bool(self.adapter_get('Pairable')) is False, 'Pairable true after RequestDefaultAgent')
        self.check(self.active() is False, 'PairingActive true at startup')
        self.check(int(self.service_get('PairingRemaining')) == 0, 'PairingRemaining not 0 at startup')

    def test_closed_window(self):
        phone = dbus.ObjectPath(PHONE)
        self.expect_rejected('RequestConfirmation closed', self.agent.RequestConfirmation, phone, dbus.UInt32(123456))
        self.expect_rejected('RequestAuthorization closed', self.agent.RequestAuthorization, phone)
        self.expect_rejected('RequestPinCode closed', self.agent.RequestPinCode, phone)
        self.expect_rejected('RequestPasskey closed', self.agent.RequestPasskey, phone)
        self.expect_rejected('DisplayPasskey closed', self.agent.DisplayPasskey, phone, dbus.UInt32(1), dbus.UInt16(0))

    def test_authorize(self):
        phone = dbus.ObjectPath(PHONE)
        for short in ('110b', '110c', '110d', '110e'):
            self.expect_ok(f'AuthorizeService {short}', self.agent.AuthorizeService, phone, dbus.String(uuid(short)))
        for short in ('111e', '111f', '1108', '1112', '1124', '1812'):
            self.expect_rejected(f'AuthorizeService {short}', self.agent.AuthorizeService, phone, dbus.String(uuid(short)))
        self.expect_rejected('AuthorizeService custom', self.agent.AuthorizeService, phone,
                             dbus.String('12345678-1234-1234-1234-123456789abc'))

    def test_timeout(self):
        phone = dbus.ObjectPath(PHONE)
        self.fake.signals.clear()
        started = time.monotonic()
        self.expect_ok('StartPairing(2)', self.service.StartPairing, dbus.UInt32(2))
        self.check_open('window', 2)
        self.check(int(self.service_get('PairingRemaining')) == 2, 'PairingRemaining not 2')
        self.expect_ok('RequestConfirmation open', self.agent.RequestConfirmation, phone, dbus.UInt32(123456))
        self.expect_ok('RequestAuthorization open', self.agent.RequestAuthorization, phone)
        self.expect_rejected('RequestPinCode open', self.agent.RequestPinCode, phone)
        self.check(wait_for(lambda: not self.active(), 4), 'window did not time out')
        elapsed = time.monotonic() - started
        self.check(1.5 <= elapsed <= 3.5, f'window closed after {elapsed:.1f} s')
        self.check_restored('timeout')
        self.check(int(self.service_get('PairingRemaining')) == 0, 'PairingRemaining not 0 after timeout')
        time.sleep(0.2)
        actives = [bool(s['PairingActive']) for s in self.fake.signals if 'PairingActive' in s]
        remaining = [int(s['PairingRemaining']) for s in self.fake.signals if 'PairingRemaining' in s]
        self.check(actives == [True, False], f'PairingActive signals {actives}')
        self.check(remaining == [2, 1, 0], f'PairingRemaining signals {remaining}')

    def test_stop(self):
        self.expect_ok('StartPairing(10)', self.service.StartPairing, dbus.UInt32(10))
        self.check_open('stop window', 10)
        self.expect_ok('StopPairing', self.service.StopPairing)
        self.check(self.active() is False, 'PairingActive true after StopPairing')
        self.check_restored('stop')
        self.expect_ok('StopPairing idle', self.service.StopPairing)
        self.check_restored('stop idle')

    def test_bounds(self):
        self.expect_invalid('StartPairing(0)', self.service.StartPairing, dbus.UInt32(0))
        self.expect_invalid('StartPairing(601)', self.service.StartPairing, dbus.UInt32(601))
        self.check(self.active() is False, 'PairingActive true after rejected StartPairing')
        self.check_restored('bounds')
        self.expect_ok('StartPairing(600)', self.service.StartPairing, dbus.UInt32(600))
        self.check_open('bounds window', 600)
        self.expect_ok('StopPairing bounds', self.service.StopPairing)
        self.check_restored('bounds stop')

    def test_gate(self):
        keyboard = ADAPTER_PATH + '/dev_10_00_00_00_00_01'
        phone = ADAPTER_PATH + '/dev_10_00_00_00_00_02'
        late_phone = ADAPTER_PATH + '/dev_10_00_00_00_00_03'
        late_pad = ADAPTER_PATH + '/dev_10_00_00_00_00_04'
        eir_phone = ADAPTER_PATH + '/dev_10_00_00_00_00_05'
        in_loop(self.fake.add_device, keyboard, [uuid('1124'), uuid('1200')])
        in_loop(self.fake.add_device, phone, [uuid('110a'), uuid('110c'), uuid('110e'), uuid('1124')])
        in_loop(self.fake.add_device, late_phone, [])
        in_loop(self.fake.add_device, late_pad, [])
        in_loop(self.fake.add_device, eir_phone, [uuid('110c'), uuid('110e')])
        in_loop(self.fake.flip, keyboard, 'Paired', dbus.Boolean(True))
        time.sleep(0.3)
        self.check(keyboard in self.fake.devices, 'keyboard removed before ServicesResolved')
        in_loop(self.fake.flip, keyboard, 'ServicesResolved', dbus.Boolean(True))
        self.check(wait_for(lambda: keyboard in self.fake.removed, 2), 'keyboard without a2dp source not removed')
        in_loop(self.fake.flip, phone, 'Paired', dbus.Boolean(True))
        in_loop(self.fake.flip, phone, 'ServicesResolved', dbus.Boolean(True))
        in_loop(self.fake.flip, late_phone, 'Paired', dbus.Boolean(True))
        in_loop(self.fake.flip, late_pad, 'Paired', dbus.Boolean(True))
        in_loop(self.fake.flip, eir_phone, 'Paired', dbus.Boolean(True))
        time.sleep(0.5)
        self.check(phone in self.fake.devices, 'phone with a2dp source and hid removed')
        self.check(late_phone in self.fake.devices, 'device with empty UUIDs removed before ServicesResolved')
        self.check(late_pad in self.fake.devices, 'device with empty UUIDs removed before ServicesResolved')
        self.check(eir_phone in self.fake.devices, 'device with EIR UUIDs removed before ServicesResolved')
        in_loop(self.fake.flip, late_phone, 'UUIDs', dbus.Array([dbus.String(uuid('110a'))], signature='s'))
        in_loop(self.fake.flip, late_phone, 'ServicesResolved', dbus.Boolean(True))
        in_loop(self.fake.flip, late_pad, 'UUIDs', dbus.Array([dbus.String(uuid('1812'))], signature='s'))
        in_loop(self.fake.flip, late_pad, 'ServicesResolved', dbus.Boolean(True))
        in_loop(self.fake.flip, eir_phone, 'UUIDs',
                dbus.Array([dbus.String(uuid('110a')), dbus.String(uuid('110c')), dbus.String(uuid('110e'))],
                           signature='s'))
        in_loop(self.fake.flip, eir_phone, 'ServicesResolved', dbus.Boolean(True))
        self.check(wait_for(lambda: late_pad in self.fake.removed, 2), 'late pad without a2dp source not removed')
        time.sleep(0.3)
        self.check(late_phone in self.fake.devices, 'late phone with a2dp source removed')
        self.check(eir_phone in self.fake.devices, 'phone with a2dp source only after SDP removed')
        self.check(self.fake.removed == [keyboard, late_pad], f'removed {self.fake.removed}')

    def test_paired_closes(self):
        phone = ADAPTER_PATH + '/dev_10_00_00_00_00_06'
        keyboard = ADAPTER_PATH + '/dev_10_00_00_00_00_07'
        in_loop(self.fake.add_device, phone, [uuid('110a'), uuid('110c'), uuid('110e')])
        in_loop(self.fake.add_device, keyboard, [uuid('1124'), uuid('1200')])
        self.fake.signals.clear()
        self.expect_ok('StartPairing(10) paired', self.service.StartPairing, dbus.UInt32(10))
        self.check_open('paired window', 10)
        in_loop(self.fake.flip, phone, 'Paired', dbus.Boolean(True))
        time.sleep(0.3)
        self.check(self.active() is True, 'window closed before ServicesResolved')
        in_loop(self.fake.flip, phone, 'ServicesResolved', dbus.Boolean(True))
        self.check(wait_for(lambda: not self.active(), 2), 'window did not close on paired phone')
        self.check(int(self.service_get('PairingRemaining')) == 0, 'PairingRemaining not 0 after paired close')
        self.check_restored('paired')
        self.check(phone in self.fake.devices, 'paired phone removed')
        time.sleep(0.2)
        actives = [bool(s['PairingActive']) for s in self.fake.signals if 'PairingActive' in s]
        self.check(actives == [True, False], f'paired PairingActive signals {actives}')
        self.expect_ok('StartPairing(10) keyboard', self.service.StartPairing, dbus.UInt32(10))
        self.check_open('keyboard window', 10)
        in_loop(self.fake.flip, keyboard, 'Paired', dbus.Boolean(True))
        in_loop(self.fake.flip, keyboard, 'ServicesResolved', dbus.Boolean(True))
        self.check(wait_for(lambda: keyboard in self.fake.removed, 2), 'keyboard paired in window not removed')
        time.sleep(0.3)
        self.check_open('keyboard window after removal', 10)
        self.expect_ok('StopPairing keyboard', self.service.StopPairing)
        self.check_restored('keyboard stop')
        in_loop(self.fake.flip, phone, 'Connected', dbus.Boolean(False))
        self.expect_ok('StartPairing(10) known phone', self.service.StartPairing, dbus.UInt32(10))
        self.check_open('known phone window', 10)
        in_loop(self.fake.flip, phone, 'Connected', dbus.Boolean(True))
        self.check(wait_for(lambda: not self.active(), 2), 'window did not close when a paired phone connected')
        self.check_restored('known phone')

    def terminate(self, proc, label):
        proc.send_signal(signal.SIGTERM)
        try:
            code = proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            code = proc.wait()
            self.check(False, f'{label}: microdap-bt did not exit on SIGTERM')
        self.check(code == 0, f'{label}: microdap-bt exit {code}')
        self.check(wait_for(lambda: not self.bus.name_has_owner(SERVICE), 2), f'{label}: {SERVICE} still owned')

    def searching(self):
        return bool(self.service_get('SearchActive'))

    def test_search(self):
        headset = ADAPTER_PATH + '/dev_20_00_00_00_00_01'
        late_headset = ADAPTER_PATH + '/dev_20_00_00_00_00_02'
        stray = ADAPTER_PATH + '/dev_20_00_00_00_00_03'
        in_loop(self.fake.add_device, headset, [uuid('110b'), uuid('110c'), uuid('110e')])
        in_loop(self.fake.add_device, late_headset, [])
        in_loop(self.fake.add_device, stray, [uuid('110b'), uuid('110c')])
        self.fake.discovery.clear()
        self.fake.signals.clear()
        self.check(self.searching() is False, 'SearchActive true at start')
        self.check(int(self.service_get('SearchRemaining')) == 0, 'SearchRemaining not 0 at start')
        self.expect_invalid('StartSearch(0)', self.service.StartSearch, dbus.UInt32(0))
        self.expect_invalid('StartSearch(601)', self.service.StartSearch, dbus.UInt32(601))
        self.check(self.fake.discovery == [], f'discovery after a refused StartSearch {self.fake.discovery}')
        started = time.monotonic()
        self.expect_ok('StartSearch(2)', self.service.StartSearch, dbus.UInt32(2))
        self.check(self.searching() is True, 'SearchActive not true')
        self.check(int(self.service_get('SearchRemaining')) == 2, 'SearchRemaining not 2')
        self.check(self.active() is False, 'PairingActive true during search')
        self.check(bool(self.adapter_get('Discoverable')) is False, 'Discoverable true during search')
        self.check(bool(self.adapter_get('Pairable')) is True, 'Pairable false during search')
        self.check(int(self.adapter_get('PairableTimeout')) == 2, 'PairableTimeout not 2 during search')
        self.check(self.fake.discovery == [('filter', {'Transport': 'bredr'}), 'start'],
                   f'discovery at search {self.fake.discovery}')
        self.expect_ok('RequestConfirmation search', self.agent.RequestConfirmation, dbus.ObjectPath(headset),
                       dbus.UInt32(123456))
        in_loop(self.fake.flip, headset, 'Paired', dbus.Boolean(True))
        in_loop(self.fake.flip, headset, 'ServicesResolved', dbus.Boolean(True))
        in_loop(self.fake.flip, late_headset, 'Paired', dbus.Boolean(True))
        time.sleep(0.3)
        self.check(headset in self.fake.devices, 'headset paired from SEARCH removed')
        self.check(self.searching() is True, 'search closed by a paired headset')
        self.check(wait_for(lambda: not self.searching(), 4), 'search did not time out')
        elapsed = time.monotonic() - started
        self.check(1.5 <= elapsed <= 3.5, f'search closed after {elapsed:.1f} s')
        self.check(self.fake.discovery[-1] == 'stop', f'discovery after search {self.fake.discovery}')
        self.check_restored('search timeout')
        in_loop(self.fake.flip, late_headset, 'UUIDs', dbus.Array([dbus.String(uuid('110b'))], signature='s'))
        in_loop(self.fake.flip, late_headset, 'ServicesResolved', dbus.Boolean(True))
        time.sleep(0.3)
        self.check(late_headset in self.fake.devices, 'headset paired during SEARCH, resolved after it, removed')
        self.expect_rejected('RequestConfirmation after search', self.agent.RequestConfirmation,
                             dbus.ObjectPath(stray), dbus.UInt32(1))
        in_loop(self.fake.flip, stray, 'Paired', dbus.Boolean(True))
        in_loop(self.fake.flip, stray, 'ServicesResolved', dbus.Boolean(True))
        self.check(wait_for(lambda: stray in self.fake.removed, 2), 'unsolicited headset pair not removed')
        time.sleep(0.2)
        actives = [bool(s['SearchActive']) for s in self.fake.signals if 'SearchActive' in s]
        remaining = [int(s['SearchRemaining']) for s in self.fake.signals if 'SearchRemaining' in s]
        self.check(actives == [True, False], f'SearchActive signals {actives}')
        self.check(remaining == [2, 1, 0], f'SearchRemaining signals {remaining}')
        self.check(not any('PairingActive' in s for s in self.fake.signals), 'PairingActive signalled by search')

    def test_search_and_pairing(self):
        speaker = ADAPTER_PATH + '/dev_20_00_00_00_00_04'
        in_loop(self.fake.add_device, speaker, [uuid('110b')])
        self.fake.discovery.clear()
        self.expect_ok('StartSearch(10)', self.service.StartSearch, dbus.UInt32(10))
        self.expect_ok('StartPairing(10) over search', self.service.StartPairing, dbus.UInt32(10))
        self.check(self.searching() is False, 'search kept under a pairing window')
        self.check_open('pairing over search', 10)
        self.check(self.fake.discovery == [('filter', {'Transport': 'bredr'}), 'start', 'stop'],
                   f'discovery after pairing over search {self.fake.discovery}')
        in_loop(self.fake.flip, speaker, 'Paired', dbus.Boolean(True))
        in_loop(self.fake.flip, speaker, 'ServicesResolved', dbus.Boolean(True))
        self.check(wait_for(lambda: speaker in self.fake.removed, 2), 'headset paired in the phone window not removed')
        self.expect_ok('StopSearch under pairing', self.service.StopSearch)
        self.check_open('pairing after StopSearch', 10)
        self.expect_ok('StartSearch(10) over pairing', self.service.StartSearch, dbus.UInt32(10))
        self.check(self.active() is False, 'pairing kept under a search window')
        self.check(bool(self.adapter_get('Discoverable')) is False, 'search over pairing: Discoverable true')
        self.check(int(self.adapter_get('DiscoverableTimeout')) == 180, 'search over pairing: DiscoverableTimeout')
        self.check(bool(self.adapter_get('Pairable')) is True, 'search over pairing: Pairable false')
        self.check(self.searching() is True, 'search not open over pairing')
        self.expect_ok('StopPairing under search', self.service.StopPairing)
        self.check(self.searching() is True, 'StopPairing closed the search')
        self.expect_ok('StopSearch', self.service.StopSearch)
        self.check(self.searching() is False, 'StopSearch left the search open')
        self.check(self.fake.discovery[-1] == 'stop', f'discovery after StopSearch {self.fake.discovery}')
        self.check_restored('search stop')

    def test_roles(self):
        phone = ADAPTER_PATH + '/dev_20_00_00_00_00_05'
        headset = ADAPTER_PATH + '/dev_20_00_00_00_00_06'
        unknown = ADAPTER_PATH + '/dev_20_00_00_00_00_07'
        in_loop(self.fake.add_device, phone, [uuid('110a'), uuid('110c'), uuid('110e')])
        in_loop(self.fake.add_device, headset, [uuid('110b'), uuid('110c'), uuid('110e')])
        in_loop(self.fake.add_device, unknown, [])
        self.fake.signals.clear()
        self.check(str(self.service_get('Role')) == 'receiver', f'role at start {self.service_get("Role")}')
        self.expect_invalid('SetRole(source)', self.service.SetRole, dbus.String('source'))
        for short in ('110b', '110e'):
            self.expect_ok(f'receiver phone {short}', self.agent.AuthorizeService, dbus.ObjectPath(phone),
                           dbus.String(uuid(short)))
        self.expect_rejected('receiver 110a', self.agent.AuthorizeService, dbus.ObjectPath(unknown),
                             dbus.String(uuid('110a')))
        self.expect_rejected('receiver headset 110c', self.agent.AuthorizeService, dbus.ObjectPath(headset),
                             dbus.String(uuid('110c')))
        self.expect_ok('SetRole(headphones)', self.service.SetRole, dbus.String('headphones'))
        self.check(str(self.service_get('Role')) == 'headphones', 'role not headphones')
        self.expect_ok('SetRole(headphones) again', self.service.SetRole, dbus.String('headphones'))
        for short in ('110a', '110c', '110d', '110e'):
            self.expect_ok(f'headphones headset {short}', self.agent.AuthorizeService, dbus.ObjectPath(headset),
                           dbus.String(uuid(short)))
        self.expect_ok('headphones unknown 110a', self.agent.AuthorizeService, dbus.ObjectPath(unknown),
                       dbus.String(uuid('110a')))
        self.expect_rejected('headphones 110b', self.agent.AuthorizeService, dbus.ObjectPath(unknown),
                             dbus.String(uuid('110b')))
        for short in ('110d', '110e'):
            self.expect_rejected(f'headphones phone {short}', self.agent.AuthorizeService, dbus.ObjectPath(phone),
                                 dbus.String(uuid(short)))
        both = ADAPTER_PATH + '/dev_20_00_00_00_00_08'
        in_loop(self.fake.add_device, both, [uuid('110a'), uuid('110b')])
        self.expect_ok('headphones both 110a', self.agent.AuthorizeService, dbus.ObjectPath(both),
                       dbus.String(uuid('110a')))
        self.expect_ok('SetRole(receiver)', self.service.SetRole, dbus.String('receiver'))
        self.expect_ok('receiver both 110b', self.agent.AuthorizeService, dbus.ObjectPath(both),
                       dbus.String(uuid('110b')))
        time.sleep(0.2)
        roles = [str(s['Role']) for s in self.fake.signals if 'Role' in s]
        self.check(roles == ['headphones', 'receiver'], f'Role signals {roles}')

    def test_sigterm(self, proc):
        phone = ADAPTER_PATH + '/dev_10_00_00_00_00_06'
        idle_phone = ADAPTER_PATH + '/dev_10_00_00_00_00_08'
        speaker = ADAPTER_PATH + '/dev_10_00_00_00_00_09'
        in_loop(self.fake.add_device, idle_phone, [uuid('110a'), uuid('110c')])
        in_loop(self.fake.add_device, speaker, [uuid('110b'), uuid('110c')])
        in_loop(self.fake.flip, speaker, 'Connected', dbus.Boolean(True))
        self.check(bool(self.fake.devices[phone].props['Connected']) is True, 'phone not connected before SIGTERM')
        self.expect_ok('StartPairing(10) before SIGTERM', self.service.StartPairing, dbus.UInt32(10))
        self.check_open('sigterm window', 10)
        self.terminate(proc, 'sigterm')
        self.check_restored('sigterm')
        self.check(bool(self.adapter_get('Pairable')) is False, 'Pairable not restored to its value before start')
        self.check(self.fake.disconnected == [phone], f'disconnected on exit {self.fake.disconnected}')
        self.check(bool(self.fake.devices[phone].props['Connected']) is False, 'phone still connected after exit')
        self.check(bool(self.fake.devices[speaker].props['Connected']) is True, 'non source device disconnected on exit')

    def test_sigterm_keep(self, proc):
        phone = ADAPTER_PATH + '/dev_10_00_00_00_00_06'
        in_loop(self.fake.flip, phone, 'Connected', dbus.Boolean(True))
        self.fake.disconnected.clear()
        self.terminate(proc, 'sigterm keep')
        self.check_restored('sigterm keep')
        self.check(self.fake.disconnected == [], f'disconnected with keep on {self.fake.disconnected}')
        self.check(bool(self.fake.devices[phone].props['Connected']) is True, 'phone disconnected with keep on')

    def test_print_keep(self, env):
        cases = (
            (None, 'drop'),
            (BOOT_ID + '\n', 'keep'),
            ('  ' + BOOT_ID + '  \n', 'keep'),
            ('00000000-0000-0000-0000-000000000000\n', 'drop'),
            ('', 'drop'),
        )
        for content, expected in cases:
            write_marker(content)
            result = subprocess.run([sys.executable, MICRODAP_BT, '--print-keep'], env=env,
                                    capture_output=True, text=True)
            self.check(result.returncode == 0, f'--print-keep exit {result.returncode} for {content!r}')
            self.check(result.stdout.strip() == expected,
                       f'--print-keep printed {result.stdout.strip()!r} for {content!r}')
        write_marker(BOOT_ID + '\n')
        missing = dict(env, MICRODAP_KEEP_BIN=os.path.join(os.path.dirname(LOG), 'missing', 'microdap-keep'))
        result = subprocess.run([sys.executable, MICRODAP_BT, '--print-keep'], env=missing,
                                capture_output=True, text=True)
        self.check(result.returncode == 0, f'--print-keep without microdap-keep exit {result.returncode}')
        self.check(result.stdout.strip() == 'drop',
                   f'--print-keep without microdap-keep printed {result.stdout.strip()!r}')

    def test_disconnect_sources(self, env):
        phone = ADAPTER_PATH + '/dev_10_00_00_00_00_06'
        speaker = ADAPTER_PATH + '/dev_10_00_00_00_00_09'
        write_marker(BOOT_ID + '\n')
        self.check(bool(self.fake.devices[phone].props['Connected']) is True, 'phone not connected before the one shot')
        self.fake.disconnected.clear()
        for label, expected in (('first', [phone]), ('second', [phone])):
            with open(LOG, 'a') as log:
                code = subprocess.run([sys.executable, MICRODAP_BT, '--disconnect-sources'], env=env,
                                      stdout=log, stderr=log, timeout=10).returncode
            self.check(code == 0, f'--disconnect-sources {label} exit {code}')
            self.check(self.fake.disconnected == expected, f'--disconnect-sources {label} {self.fake.disconnected}')
        self.check(bool(self.fake.devices[phone].props['Connected']) is False, 'phone connected after the one shot')
        self.check(bool(self.fake.devices[speaker].props['Connected']) is True, 'non source device disconnected')
        self.check(not self.bus.name_has_owner(SERVICE), f'--disconnect-sources took {SERVICE}')


def write_marker(content):
    if content is None:
        if os.path.exists(KEEP_FILE):
            os.remove(KEEP_FILE)
        return
    with open(KEEP_FILE, 'w') as f:
        f.write(content)


def launch(fake, driver, env):
    fake.agent = None
    fake.default_agent = None
    with open(LOG, 'a') as log:
        proc = subprocess.Popen([sys.executable, MICRODAP_BT], env=env, stdout=log, stderr=log)
    ready = wait_for(lambda: fake.agent is not None and fake.default_agent is not None
                     and driver.bus.name_has_owner(SERVICE), 5)
    if not ready or proc.poll() is not None:
        if proc.poll() is None:
            proc.kill()
            proc.wait()
        with open(LOG) as log:
            sys.stderr.write(log.read())
        print('FAIL microdap-bt did not register', file=sys.stderr)
        return None
    driver.connect()
    return proc


def main():
    dbus.mainloop.glib.threads_init()
    dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
    fake = Fake(dbus.bus.BusConnection(ADDRESS))
    loop = GLib.MainLoop()
    threading.Thread(target=loop.run, daemon=True).start()

    driver = Driver(fake)
    env = dict(os.environ, MICRODAP_BT_SYSTEM_BUS=ADDRESS)
    with open(BOOT_ID_FILE, 'w') as f:
        f.write(BOOT_ID + '\n')
    write_marker(None)
    open(LOG, 'w').close()
    proc = launch(fake, driver, env)
    if proc is None:
        return 1
    try:
        driver.test_registration()
        driver.test_closed_window()
        driver.test_authorize()
        driver.test_timeout()
        driver.test_stop()
        driver.test_bounds()
        driver.test_gate()
        driver.test_paired_closes()
        driver.test_search()
        driver.test_search_and_pairing()
        driver.test_roles()
        driver.test_sigterm(proc)
        write_marker(BOOT_ID + '\n')
        proc = launch(fake, driver, env)
        if proc is None:
            return 1
        driver.test_sigterm_keep(proc)
        driver.test_print_keep(env)
        driver.test_disconnect_sources(env)
    finally:
        if proc is not None and proc.poll() is None:
            proc.kill()
            proc.wait()
    with open(LOG) as log:
        text = log.read()
    driver.check(f'Rejected {uuid("111e")} {PHONE}' in text, 'no Rejected 0000111e journal line')
    driver.check(f'Rejected {uuid("1124")} {PHONE}' in text, 'no Rejected 00001124 journal line')
    driver.check(f'Rejected RequestConfirmation {PHONE}' in text, 'no Rejected RequestConfirmation line')
    driver.check(f'pairing window closed: paired {ADAPTER_PATH}/dev_10_00_00_00_00_06' in text,
                 'no pairing window closed: paired line')
    driver.check(f'paired headset {ADAPTER_PATH}/dev_20_00_00_00_00_01' in text, 'no paired headset line')
    driver.check('search window timed out' in text, 'no search window timed out line')
    driver.check(f'disconnected {ADAPTER_PATH}/dev_10_00_00_00_00_06 on exit' in text,
                 'no disconnected on exit line')
    driver.check(f'keeping {ADAPTER_PATH}/dev_10_00_00_00_00_06 connected' in text,
                 'no keeping connected line')
    driver.check(f'disconnected {ADAPTER_PATH}/dev_10_00_00_00_00_06 on close' in text,
                 'no disconnected on close line')
    if driver.failures:
        sys.stderr.write(text)
        print(f'bt-test: {driver.failures} failure(s)', file=sys.stderr)
        return 1
    print('bt-test: ok')
    return 0


if __name__ == '__main__':
    sys.exit(main())
