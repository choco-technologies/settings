# settings API Reference

## Usage

```bash
settings [--display NAME] [--wifi DEVICE] [<view>]
```

| Argument | Meaning |
|----------|---------|
| `--display NAME` | the display to claim, or the rent of one (`<display>/<n>`, which the launcher passes); default: the first display |
| `--wifi DEVICE` | the Wi-Fi node of the driver, default `/dev/wlan0` (it may come after the application starts) |
| `<view>` | the view to show, default `/eviews/settings/settings.dmv` |

Exit code: 0 when the claim is gone (the rent was taken back), else the error of `libdmview_claim()` (`-ENODEV` when there is no such display
or rent after 10 seconds).

The page and the application: [contract.md](contract.md). The graphical interface: [gui.md](gui.md).
