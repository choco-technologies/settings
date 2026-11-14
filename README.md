# settings

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![CI](https://github.com/choco-technologies/settings/actions/workflows/ci.yml/badge.svg)](https://github.com/choco-technologies/settings/actions/workflows/ci.yml)

The settings of the system - an application of the phone's launcher: it has an **icon**, a **page** and it runs in the part of the screen that
phoneui gives it (the status bar and the dock stay).

For now: **Wi-Fi** - the switch of the radio, the networks in range, the password of a network (typed on the keyboard), and keeping the
network, so that `wifid` connects to it by itself after a restart.

## Using it

The launcher lists it (the icon is in `/eviews/icons`) and starts it with `settings --display <rent>`. By hand:
`settings [--display NAME] [--wifi DEVICE] [<view>]` - `DEVICE` is the Wi-Fi node of the driver (default `/dev/wlan0`).

| | |
|---|---|
| Wi-Fi switch | the radio off and on (`DMDRVI_IOCTL_WIFI_SET_ENABLED`); turning it on scans |
| a network | tap it: an open network connects at once; one that is saved connects with its password; a secured one asks for it; the one that is connected opens its page (Disconnect, Forget) |
| the password | the keyboard types it (Backspace erases, Enter connects), *Show* shows it; it is wiped when the connection is done |
| keeping | a network that connected is saved in the `wifi` vault of dmsecrets (`wifi/<SSID>` = the password, the device key opens it): where the system has no vault the network works until the next restart and the page says it was not kept |

A connection is given up after 20 s (a wrong password never connects): the radio is disconnected and the page says so.

## How it is made

| | |
|---|---|
| `src/settings_model.c` | what the application is, without the screen and the radio: the pages, the networks, the password - told what happens, it publishes what the page shows. Tested on a PC with a radio that is made up |
| `src/wifi_node.c` | the radio (the `DMDRVI_IOCTL_WIFI_*` of the node: every Wi-Fi driver works) and the vault of the networks |
| `src/settings.c` | joins them to the claim of the display |
| `views/settings.html` | the page (HTML, CSS, JavaScript); dmod converts it into a view at build time - there is no conversion in this repository. [docs/contract.md](docs/contract.md) |
| `views/fonts/` | Inter (OFL, [LICENSE-Inter.txt](views/fonts/LICENSE-Inter.txt)), as the phone's own, in the weights that an e-paper keeps |
| `icons/` | the icon |

## Building

```bash
cmake -S . -B build -DDMOD_TOOLS_NAME=arch/xtensa/esp32s3    # an architecture (-DDMOD_DIR=<local dmod> for a local checkout)
cmake --build build
ctest --test-dir build                                       # the model
```

It needs dmod with the pages (`DMOD_ASSETS_PATHS`: `*.html`), libdmview 0.20 or newer, dmdrvi 2.15 (the Wi-Fi switch) and dmsecrets.

## License

MIT - see [LICENSE](LICENSE).
