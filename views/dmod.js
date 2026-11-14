/*
 * dmod in a browser - settings as dmodOS shows it, without dmodOS: open the page in a browser to design it.
 *
 * On the device the page is a view shown by dmview and the application (src/settings_model.c) is on the other side of the channel:
 * dmod.prop / get / set / onchange / emit are dmview's properties and events, compiled into the view. The conversion does not run this
 * file. Here it plays a small part of the application: some networks, the pages, a password that is always accepted.
 */
(function () {
    const props = {};
    const listeners = {};
    const NETWORKS = [['Home network', 'WPA2, saved', 4], ['Cafe', 'open', 2], ['Neighbour', 'WPA3', 1]];

    function set(name, value) {
        if (props[name] === value) return;
        props[name] = value;
        (listeners[name] || []).forEach(fn => fn());
    }
    function scan() {
        NETWORKS.forEach((n, i) => { set('r' + i + '.ssid', n[0]); set('r' + i + '.sub', n[1]); set('r' + i + '.bars', n[2]); set('r' + i + '.on', 1); });
        set('wifi.status', 'Not connected');
    }

    window.dmod = {
        prop(name, initial) { if (!(name in props)) props[name] = initial; return props[name]; },
        get(name) { return props[name]; },
        set(name, value) { props[name] = value; },
        onchange(name, fn) { (listeners[name] = listeners[name] || []).push(fn); },
        emit(name, value) {
            console.log('emit', name, value === undefined ? 0 : value);
            const page = props['page'];
            if (name === 'open.wifi') { set('page', 1); scan(); }
            else if (name === 'back') set('page', page === 2 || page === 3 ? 1 : 0);
            else if (name === 'wifi.toggle') { set('wifi.on', props['wifi.on'] ? 0 : 1); if (!props['wifi.on']) set('wifi.status', 'Wi-Fi is off'); else scan(); }
            else if (name === 'scan') scan();
            else if (name === 'row') { set('pw.ssid', NETWORKS[value][0]); set('pw.text', '|'); set('page', 2); }
            else if (name === 'pw.show') set('pw.show', props['pw.show'] ? 0 : 1);
            else if (name === 'pw.connect') { set('wifi.state', props['pw.ssid']); set('wifi.status', 'Connected to ' + props['pw.ssid']); set('page', 1); }
            else if (name === 'nt.disconnect' || name === 'nt.forget') { set('wifi.state', 'Not connected'); set('page', 1); }
        },
    };
})();
