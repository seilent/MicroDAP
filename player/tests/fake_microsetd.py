#!/usr/bin/python3
import json
import os
import signal
import sys

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

SERVICE = 'net.seilent.MicroSet'
PATH = '/net/seilent/MicroSet'
IFACE = 'net.seilent.MicroSet1'
CONTROL = 'net.seilent.MicroDap.FakeMicroSet'
CONTROL_PATH = '/net/seilent/MicroDap/FakeMicroSet'
ERROR = 'net.seilent.MicroSet1.Error.'

KEYS = {
    'audio.output': ('s', 'auto', {'enum': ['auto', 'speaker', 'headphones', 'usb', 'bt']}),
    'audio.hp_gain': ('s', 'high', {'enum': ['low', 'normal', 'high']}),
    'audio.hp_amp': ('s', 'lohifi', {'enum': ['lohifi', 'ulp', 'hifi']}),
    'audio.speaker_gain': ('u', 102, {'enum': [84, 90, 96, 99, 102, 105, 108]}),
    'audio.volume_step': ('u', 5, {'enum': [2, 5, 10]}),
    'audio.bt_codec': ('s', 'auto', {'enum': ['auto', 'aptx', 'aac', 'ldac', 'sbc']}),
    'display.orientation': ('s', 'portrait', {}),
    'display.auto_rotate': ('b', True, {}),
    'display.rotation': ('s', 'portrait', {'live': True, 'writable': False}),
    'session.default': ('s', 'steam', {'enum': ['steam', 'microdap', 'microemu']}),
    'rgb.enabled': ('b', False, {'override': True}),
    'rgb.hue': ('u', 0, {'override': True}),
    'rgb.saturation': ('u', 0, {'override': True}),
    'rgb.brightness': ('u', 100, {}),
    'microdap.output_level': ('s', 'off', {}),
    'microdap.sample_rate': ('s', 'auto', {}),
    'microdap.play_through': ('s', 'off', {'enum': ['off', 'folders', 'albums']}),
    'microdap.resume': ('s', 'position', {'enum': ['off', 'track', 'position']}),
    'microdap.auto_now_playing': ('b', False, {}),
    'microdap.clock': ('s', '24h', {'enum': ['24h', '12h', 'off']}),
    'microdap.battery_percent': ('b', False, {}),
    'microdap.roots': ('as', [], {}),
    'microdap.accent_hue': ('u', 350, {}),
    'microdap.accent_from_art': ('b', False, {}),
    'microdap.scale': ('d', 1.0, {}),
    'microdap.saver_seconds': ('u', 30, {}),
    'microdap.saver_lyrics': ('b', False, {}),
    'microdap.touch': ('b', True, {}),
    'microdap.rgb': ('s', 'system', {'enum': ['system', 'accent', 'off']}),
    'microdap.overlay_toasts': ('b', False, {}),
    'microdap.screen_off_seconds': ('u', 300, {}),
    'microdap.suspend_minutes': ('u', 10, {}),
    'microdap.wifi_off_screen_off': ('b', False, {}),
}
for slot in ('headphones', 'speaker', 'usb', 'bt'):
    KEYS['audio.eq.%s.on' % slot] = ('b', False, {})
    KEYS['audio.eq.%s.name' % slot] = ('s', '', {})
    KEYS['audio.eq.%s.filters' % slot] = ('as', ['preamp 0'], {})

TYPES = {
    's': (dbus.String,),
    'b': (dbus.Boolean,),
    'u': (dbus.UInt32,),
    'd': (dbus.Double,),
}


def wrap(sig, value):
    if sig == 's':
        return dbus.String(value)
    if sig == 'b':
        return dbus.Boolean(value)
    if sig == 'u':
        return dbus.UInt32(value)
    if sig == 'd':
        return dbus.Double(value)
    return dbus.Array([dbus.String(v) for v in value], signature='s')


def plain(sig, value):
    if sig == 'as':
        return [str(v) for v in value]
    if sig == 'b':
        return bool(value)
    if sig == 'u':
        return int(value)
    if sig == 'd':
        return float(value)
    return str(value)


def accepts(key, value):
    sig, _, extra = KEYS[key]
    if sig == 'as':
        if not isinstance(value, dbus.Array) or any(not isinstance(v, dbus.String) for v in value):
            return False
    elif not isinstance(value, TYPES[sig]):
        return False
    if 'enum' in extra and plain(sig, value) not in extra['enum']:
        return False
    return True


class MicroSet(dbus.service.Object):
    def __init__(self, bus, state):
        super().__init__(bus, PATH)
        self.state = state
        self.stored = {}
        self.live = {}
        self.overrides = []
        self.subscribers = {}
        self.calls = []
        self.refuse = set()
        self.drop_import = False
        if state and os.path.exists(state):
            with open(state) as f:
                self.stored = json.load(f)
        bus.add_signal_receiver(self.on_owner, 'NameOwnerChanged', 'org.freedesktop.DBus',
                                'org.freedesktop.DBus', '/org/freedesktop/DBus')

    def save(self):
        if self.state:
            with open(self.state, 'w') as f:
                json.dump(self.stored, f)

    def effective(self, key):
        if key in self.live:
            return self.live[key]
        for k, _, value in reversed(self.overrides):
            if k == key:
                return value
        if key in self.stored:
            return self.stored[key]
        return KEYS[key][1]

    def changed_after(self, key, before):
        after = self.effective(key)
        if after != before:
            self.Changed(key, wrap(KEYS[key][0], after))

    def on_owner(self, name, old, new):
        if not name.startswith(':') or new:
            return
        gone = [k for k, s, _ in self.overrides if s == name]
        befores = {k: self.effective(k) for k in gone}
        self.overrides = [o for o in self.overrides if o[1] != name]
        for k in gone:
            self.changed_after(k, befores[k])
        for subs in self.subscribers.values():
            subs.discard(str(name))

    def known(self, key):
        if key not in KEYS:
            raise dbus.exceptions.DBusException('unknown key %s' % key, name=ERROR + 'UnknownKey')

    @dbus.service.method(IFACE, in_signature='s', out_signature='v')
    def Get(self, key):
        self.calls.append('Get %s' % key)
        self.known(key)
        return wrap(KEYS[key][0], self.effective(key))

    @dbus.service.method(IFACE, in_signature='sv', out_signature='')
    def Set(self, key, value):
        self.calls.append('Set %s' % key)
        self.known(key)
        if key in self.refuse:
            self.refuse.discard(key)
            raise dbus.exceptions.DBusException('refused', name=ERROR + 'InvalidValue')
        if not KEYS[key][2].get('writable', True):
            raise dbus.exceptions.DBusException('read only', name=ERROR + 'ReadOnly')
        if not accepts(key, value):
            raise dbus.exceptions.DBusException('not a valid value', name=ERROR + 'InvalidValue')
        before = self.effective(key)
        self.stored[key] = plain(KEYS[key][0], value)
        self.save()
        self.changed_after(key, before)

    @dbus.service.method(IFACE, in_signature='s', out_signature='a{sv}')
    def List(self, prefix):
        out = {}
        for key, (sig, _, _) in KEYS.items():
            if key.startswith(prefix):
                out[key] = wrap(sig, self.effective(key))
        return dbus.Dictionary(out, signature='sv')

    @dbus.service.method(IFACE, in_signature='s', out_signature='a{sv}')
    def Stored(self, prefix):
        out = {}
        for key, value in self.stored.items():
            if key.startswith(prefix) and key in KEYS:
                out[key] = wrap(KEYS[key][0], value)
        return dbus.Dictionary(out, signature='sv')

    @dbus.service.method(IFACE, in_signature='s', out_signature='s')
    def Schema(self, prefix):
        out = {}
        for key, (sig, default, extra) in KEYS.items():
            if not key.startswith(prefix):
                continue
            entry = {'type': sig, 'default': default, 'override': bool(extra.get('override'))}
            if 'enum' in extra:
                entry['enum'] = extra['enum']
            if extra.get('live'):
                entry['live'] = True
            out[key] = entry
        return json.dumps(out)

    @dbus.service.method(IFACE, in_signature='a{sv}', out_signature='asa{ss}')
    def Import(self, values):
        self.calls.append('Import %d' % len(values))
        taken = []
        refused = {}
        befores = {}
        for key, value in values.items():
            key = str(key)
            if key not in KEYS:
                refused[key] = 'no key'
                continue
            if key in self.stored:
                continue
            if not accepts(key, value):
                refused[key] = 'not a valid value'
                continue
            befores[key] = self.effective(key)
            self.stored[key] = plain(KEYS[key][0], value)
            taken.append(key)
        self.save()
        if self.drop_import:
            os._exit(0)
        for key in taken:
            self.changed_after(key, befores[key])
        return dbus.Array(taken, signature='s'), dbus.Dictionary(refused, signature='ss')

    @dbus.service.method(IFACE, in_signature='sv', out_signature='', sender_keyword='sender')
    def Override(self, key, value, sender=None):
        self.calls.append('Override %s' % key)
        self.known(key)
        if not KEYS[key][2].get('override'):
            raise dbus.exceptions.DBusException('no override', name='org.freedesktop.DBus.Error.InvalidArgs')
        if not accepts(key, value):
            raise dbus.exceptions.DBusException('not a valid value', name=ERROR + 'InvalidValue')
        before = self.effective(key)
        self.overrides = [o for o in self.overrides if not (o[0] == key and o[1] == sender)]
        self.overrides.append((key, str(sender), plain(KEYS[key][0], value)))
        self.changed_after(key, before)

    @dbus.service.method(IFACE, in_signature='s', out_signature='', sender_keyword='sender')
    def ClearOverride(self, key, sender=None):
        self.calls.append('ClearOverride %s' % key)
        self.known(key)
        before = self.effective(key)
        self.overrides = [o for o in self.overrides if not (o[0] == key and o[1] == sender)]
        self.changed_after(key, before)

    @dbus.service.method(IFACE, in_signature='s', out_signature='', sender_keyword='sender')
    def Subscribe(self, key, sender=None):
        self.calls.append('Subscribe %s' % key)
        self.known(key)
        self.subscribers.setdefault(key, set()).add(str(sender))

    @dbus.service.method(IFACE, in_signature='s', out_signature='', sender_keyword='sender')
    def Unsubscribe(self, key, sender=None):
        self.calls.append('Unsubscribe %s' % key)
        self.subscribers.get(key, set()).discard(str(sender))

    @dbus.service.signal(IFACE, signature='sv')
    def Changed(self, key, value):
        pass


class Control(dbus.service.Object):
    def __init__(self, bus, store, loop):
        super().__init__(bus, CONTROL_PATH)
        self.store = store
        self.loop = loop

    @dbus.service.method(CONTROL, in_signature='sv', out_signature='')
    def SetStored(self, key, value):
        before = self.store.effective(key)
        self.store.stored[key] = plain(KEYS[key][0], value)
        self.store.save()
        self.store.changed_after(key, before)

    @dbus.service.method(CONTROL, in_signature='sv', out_signature='')
    def SetLive(self, key, value):
        before = self.store.effective(key)
        self.store.live[key] = plain(KEYS[key][0], value)
        self.store.changed_after(key, before)

    @dbus.service.method(CONTROL, in_signature='', out_signature='a{sv}')
    def Overrides(self):
        out = {}
        for key, _, value in self.store.overrides:
            out[key] = wrap(KEYS[key][0], value)
        return dbus.Dictionary(out, signature='sv')

    @dbus.service.method(CONTROL, in_signature='s', out_signature='u')
    def Subscribers(self, key):
        return len(self.store.subscribers.get(key, set()))

    @dbus.service.method(CONTROL, in_signature='', out_signature='as')
    def Calls(self):
        return dbus.Array(self.store.calls, signature='s')

    @dbus.service.method(CONTROL, in_signature='', out_signature='')
    def Clear(self):
        self.store.calls = []

    @dbus.service.method(CONTROL, in_signature='s', out_signature='')
    def RefuseSet(self, key):
        self.store.refuse.add(str(key))

    @dbus.service.method(CONTROL, in_signature='b', out_signature='')
    def DropImport(self, drop):
        self.store.drop_import = bool(drop)

    @dbus.service.method(CONTROL, in_signature='', out_signature='')
    def Quit(self):
        GLib.idle_add(self.loop.quit)


def main():
    DBusGMainLoop(set_as_default=True)
    loop = GLib.MainLoop()
    bus = dbus.SessionBus()
    store = MicroSet(bus, sys.argv[1] if len(sys.argv) > 1 else '')
    control = Control(bus, store, loop)
    name = dbus.service.BusName(SERVICE, bus)
    for sig in (signal.SIGTERM, signal.SIGINT):
        unix_signal_add(GLib.PRIORITY_DEFAULT, sig, lambda: loop.quit() or False)
    loop.run()
    del control
    del name


if __name__ == '__main__':
    main()
