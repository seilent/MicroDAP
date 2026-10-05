#!/usr/bin/python3
import os
import signal
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
CASDAP_BT = os.environ['CASDAP_BT']
LOG = os.environ['CASDAP_BT_LOG']

BLUEZ = 'org.bluez'
OBJECT_MANAGER = 'org.freedesktop.DBus.ObjectManager'
PROPERTIES = 'org.freedesktop.DBus.Properties'
ADAPTER = 'org.bluez.Adapter1'
DEVICE = 'org.bluez.Device1'
AGENT = 'org.bluez.Agent1'
AGENT_MANAGER = 'org.bluez.AgentManager1'
AGENT_PATH = '/org/casdap/agent'
SERVICE = 'org.casdap.Bluetooth'
SERVICE_PATH = '/org/casdap/Bluetooth'
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
            'Alias': dbus.String('armada'),
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

    @dbus.service.method(ADAPTER, in_signature='o', out_signature='')
    def RemoveDevice(self, path):
        device = self.fake.devices.pop(str(path), None)
        if device is None:
            raise DoesNotExist(str(path))
        device.remove_from_connection()
        self.fake.removed.append(str(path))


class Device(dbus.service.Object):
    def __init__(self, bus, path, uuids):
        super().__init__(bus, path)
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

    def flip(self, name, value):
        self.props[name] = value
        self.PropertiesChanged(DEVICE, dbus.Dictionary({name: value}, signature='sv'), dbus.Array([], signature='s'))


class Fake:
    def __init__(self, bus):
        self.bus = bus
        self.devices = {}
        self.removed = []
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
        device = Device(self.bus, path, [dbus.String(u) for u in uuids])
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
        self.check(str(self.adapter_get('Alias')) == 'Test Device', f'{label}: Alias is {self.adapter_get("Alias")}')
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

    def test_sigterm(self, proc):
        self.expect_ok('StartPairing(10) before SIGTERM', self.service.StartPairing, dbus.UInt32(10))
        self.check_open('sigterm window', 10)
        proc.send_signal(signal.SIGTERM)
        try:
            code = proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            code = proc.wait()
            self.check(False, 'casdap-bt did not exit on SIGTERM')
        self.check(code == 0, f'casdap-bt exit {code}')
        self.check_restored('sigterm')
        self.check(bool(self.adapter_get('Pairable')) is False, 'Pairable not restored to its value before start')
        self.check(wait_for(lambda: not self.bus.name_has_owner(SERVICE), 2), f'{SERVICE} still owned')


def main():
    dbus.mainloop.glib.threads_init()
    dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
    fake = Fake(dbus.bus.BusConnection(ADDRESS))
    loop = GLib.MainLoop()
    threading.Thread(target=loop.run, daemon=True).start()

    driver = Driver(fake)
    env = dict(os.environ, CASDAP_BT_SYSTEM_BUS=ADDRESS)
    with open(LOG, 'w') as log:
        proc = subprocess.Popen([sys.executable, CASDAP_BT], env=env, stdout=log, stderr=log)
    try:
        ready = wait_for(lambda: fake.agent is not None and fake.default_agent is not None
                         and driver.bus.name_has_owner(SERVICE), 5)
        if not ready or proc.poll() is not None:
            with open(LOG) as log:
                sys.stderr.write(log.read())
            print('FAIL casdap-bt did not register', file=sys.stderr)
            return 1
        driver.connect()
        driver.test_registration()
        driver.test_closed_window()
        driver.test_authorize()
        driver.test_timeout()
        driver.test_stop()
        driver.test_bounds()
        driver.test_gate()
        driver.test_paired_closes()
        driver.test_sigterm(proc)
    finally:
        if proc.poll() is None:
            proc.kill()
            proc.wait()
    with open(LOG) as log:
        text = log.read()
    driver.check(f'Rejected {uuid("111e")} {PHONE}' in text, 'no Rejected 0000111e journal line')
    driver.check(f'Rejected {uuid("1124")} {PHONE}' in text, 'no Rejected 00001124 journal line')
    driver.check(f'Rejected RequestConfirmation {PHONE}' in text, 'no Rejected RequestConfirmation line')
    driver.check(f'pairing window closed: paired {ADAPTER_PATH}/dev_10_00_00_00_00_06' in text,
                 'no pairing window closed: paired line')
    if driver.failures:
        sys.stderr.write(text)
        print(f'bt-test: {driver.failures} failure(s)', file=sys.stderr)
        return 1
    print('bt-test: ok')
    return 0


if __name__ == '__main__':
    sys.exit(main())
