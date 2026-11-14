# The contract between the page and the application

The page (`views/settings.html`) and the model (`src/settings_model.c`) talk through the channel of the claim: `dmod.prop` / `dmod.onchange` /
`dmod.emit` in the page, `libdmview_prop_set_*` / `libdmview_wait_event` in the application. The names are the contract. Only a change is
sent (an e-paper redraws on every change). Texts are at most 79 characters.

## Properties set by the application

| Property | Type | The page shows |
|----------|------|----------------|
| `page` | 0 settings, 1 Wi-Fi, 2 password, 3 network | which page is shown (set last: the page appears with what it shows) |
| `wifi.state` | text | the line of the list of settings: `Off`, `Not connected`, the network |
| `wifi.on` | 0/1 | the switch |
| `wifi.status` | text | the line under the switch: `Connected to Home (-52 dBm)`, `Scanning...`, what went wrong |
| `wifi.busy` | 0/1 | a scan is running |
| `r<0..4>.ssid`, `.sub` | text | a row of the networks: the name, `WPA2, saved` / `Connected, WPA2` / `open` |
| `r<i>.bars` | 0..4 | the bars of the signal |
| `r<i>.lock`, `.cur` | 0/1 | secured; the one the radio is connected to |
| `r<i>.on` | 0/1 | the row is there (set last: it appears with its texts) |
| `pw.ssid` | text | the network of the password page |
| `pw.text` | text | the password: stars, or the text (the end of it, ".." in front), and the cursor `|` |
| `pw.show` | 0/1 | the password is shown |
| `pw.hint`, `pw.status` | text | a line of help; what happened (`Connecting...`, `Could not connect`) |
| `pw.busy` | 0/1 | a connection is being made |
| `nt.ssid`, `nt.state` | text | the network page |
| `nt.saved`, `nt.connected` | 0/1 | it is kept (Forget is shown) / the radio is connected to it |

## Events the page emits

| Event | Value | |
|-------|-------|--|
| `open.wifi` | | from the list of settings |
| `back` | | one page up |
| `wifi.toggle` | | the switch |
| `scan` | | the button |
| `row` | the row | a network is tapped |
| `pw.show` | | Show / Hide |
| `pw.connect` | | Connect |
| `nt.disconnect`, `nt.forget` | | the network page |

The keyboard is not the page's: the application takes `char` (a character: the code point) and `key` (`0x2A` Backspace, `0x28` Enter) of the claim.

## Text the page may show

The fonts of the view have only the characters of the page and of its scripts: `CHARS` in the script lists what the application may send (ASCII,
Polish letters). A name of a network with another character is drawn without it.
