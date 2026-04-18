# The Central Scrutinizer

The Central Scrutinizer is a device-hosted web manager for NextUI handhelds. Launch it on your device, open the URL shown on screen from a phone, tablet, or computer on the same network, and manage your ROM library, artwork, BIOS files, collections, screenshots, logs, and more from a modern browser.

It runs an HTTP server only while the pak is open. Quit the pak on the handheld to stop access.

## What It Does

- Shows your library grouped by platform family with dedicated console icons
- Lets you browse ROMs, saves, BIOS, overlays, and cheats by system
- Supports file upload, folder upload, folder creation, rename, delete, and plaintext file editing
- Lets you replace ROM artwork from the browser
- Includes a full SD card file browser workspace
- Includes collection management for `Collections/*.txt` playlists and collection artwork
- Includes a screenshot browser with preview, delete, and bulk zip download
- Includes a log viewer with live tail and bulk log download
- Includes optional browser terminal access when enabled on the handheld

## Supported Platforms

Current release builds target these NextUI device platforms:

- `tg5040`
- `tg5050`
- `my355`

## Requirements

- A device running NextUI
- Wi-Fi enabled on the device
- A phone, tablet, or computer on the same local network
- A modern browser

No cloud account or external service is required. The dashboard is served directly by your handheld.

## Installation

Install `CentralScrutinizer.pakz` the same way you install other NextUI paks.

Typical options:

1. Install it from your preferred pak source/store if provided there.
2. Or copy the unpacked pak into your device's `Tools/<platform>/CentralScrutinizer.pak` directory.

After installation, launch **The Central Scrutinizer** from the Tools menu on the handheld.

## First Launch

1. Open **The Central Scrutinizer** on the device.
2. If Wi-Fi is not connected, the handheld will prompt you to connect Wi-Fi in NextUI first.
3. Once the server starts, the device shows:
   - the local URL to open in your browser
   - a 4-digit pairing PIN
   - the current trusted browser count
   - whether terminal access is enabled or disabled
4. On your phone, tablet, or computer, open the URL shown on the device.
5. Pair in one of two ways:
   - Enter the 4-digit PIN shown on the handheld
   - Or press `Y` on the handheld to show a QR code and scan it

After pairing, the browser becomes a trusted client until you revoke trusted browsers from the handheld settings.

## Everyday Use

### Library

The Library view groups systems by family and shows counts for ROMs, saves, and required BIOS files.

From the library and per-system workspaces you can:

- Browse ROM folders
- Download files directly
- Replace ROM artwork with PNG images
- Inspect saves, BIOS, overlays, and cheats
- Search platforms and folder contents
- Hide or show empty platforms

### Tools

The Tools workspace includes:

- **File Browser**: browse the SD card, upload files or folders, create folders, rename items, delete items, and edit plaintext files
- **Collections**: create and edit `Collections/*.txt` playlists, reorder ROM paths, and manage collection icons/backgrounds
- **Screenshots**: preview images from `Screenshots/`, delete them, or download all screenshots as a zip
- **Log Viewer**: browse logs under `.userdata`, live-tail a log file, and download one or all logs
- **Terminal**: open a real shell in the browser if terminal access has been enabled on the handheld

## Handheld Settings

Press `A` on the handheld server screen to open settings.

Available device-side controls:

- **Terminal**: enable or disable browser terminal access
- **Refresh**: refresh the server screen state
- **Revoke Trusted Browsers**: clear trusted browser sessions and force re-pairing

Terminal access is disabled by default on handheld builds.

## Important Notes

- The web dashboard is only available while the pak is open.
- The handheld and browser device must be on the same network.
- The default port is `8877`. If that port is unavailable, the app may fall back to another nearby port shown on screen.
- Browser terminal access opens a real shell on the device. Enable it only if you understand the risk.
- Quitting the pak immediately stops the local server.

## Managed Locations

The Central Scrutinizer works with the standard NextUI storage layout on your SD card, including:

- `Roms/`
- `Saves/`
- `Bios/`
- `Overlays/`
- `Cheats/`
- `Collections/`
- `Screenshots/`

Launch logs for the pak itself are written under the shared userdata log directory, and the browser Log Viewer can scan `.userdata` logs for you.

## Credits

Platform icons used by the dashboard are derived from the libretro Systematic theme in RetroArch assets (https://git.libretro.com/libretro-assets/retroarch-assets/-/tree/e11d6708b49a893f392b238effc713c6c7cfadef/xmb/systematic). Those assets are used here under their upstream license terms.

## Acknowledgements

Special thanks to [Aaron Hoogstraten](https://github.com/aaronhoogstraten) and [nextui-web-dashboard](https://github.com/aaronhoogstraten/nextui-web-dashboard), and to [Brandon T. Kowalski](https://github.com/BrandonKowalski) and [kitchen](https://github.com/CannoliHQ/kitchen), for inspiring this application.

Additional thanks to [ro8inmorgan](https://github.com/ro8inmorgan), [frysee](https://github.com/frysee), and the rest of the [NextUI contributors](https://github.com/LoveRetro/NextUI/graphs/contributors) for developing [NextUI](https://github.com/LoveRetro/NextUI).
