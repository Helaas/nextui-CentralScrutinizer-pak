# The Central Scrutinizer

The Central Scrutinizer is a device-hosted web manager for NextUI handhelds. Launch it on your device, open the URL shown on screen from a phone, tablet, or computer on the same network, and manage your ROM library, save states, artwork, BIOS files, collections, screenshots, logs, and more from a modern browser.

It runs an HTTP server while the pak is open, and it can hand that server off to a background mode after you have already paired at least one trusted browser. Reopen the pak on the handheld to stop background mode and return to pairing/settings control.

## What It Does

- Shows your library grouped by platform family with dedicated console icons, emulator availability warnings, and installed/all emulator filters
- Lets you browse ROMs, saves, save-state bundles, BIOS, overlays, and cheats by system
- Supports upload, download, rename, delete, and folder creation where the current workspace allows it
- Lets you replace ROM artwork from the browser with PNG files
- Includes a full SD card file browser workspace
- Includes collection management for `Collections/*.txt` playlists, collection icons, and shared collection background art
- Includes a screenshot browser with preview, individual download/delete, and bulk zip download
- Includes a Mac dot-cleanup tool for removing safe Finder and archive artifacts from the SD card
- Includes a log viewer with live tail plus single-file and bulk log download
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

Install `Central Scrutinizer.pakz` the same way you install other NextUI paks.

Typical options:

1. Install it from your preferred pak source/store if provided there.
2. Or copy the unpacked pak into your device's `Tools/<platform>/Central Scrutinizer.pak` directory.

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

The Library view groups systems by family and shows counts for ROMs, saves, save states, BIOS files, overlays, and cheats.

From the library and per-system workspaces you can:

- Browse ROM folders
- Upload files into managed ROM, save, BIOS, overlay, and cheat folders
- Upload folders and create new folders inside ROM workspaces
- Download files directly
- Rename and delete managed items
- Replace ROM artwork with PNG images
- Inspect saves, save states, BIOS, overlays, and cheats
- Open a dedicated Save States view with previews plus grouped download/delete actions
- Search platforms and current folder contents
- Filter to installed emulators only, and hide or show empty platforms

### Tools

The Tools workspace includes:

- **File Browser**: browse the SD card, upload files or folders, create folders, rename items, delete items, run recursive name searches, preview common image files, and edit plaintext files
- **Collections**: create and edit `Collections/*.txt` playlists, reorder ROM paths, manage collection icons, and set or remove the shared collections background
- **Screenshots**: preview images from `Screenshots/`, download or delete them individually, or download all screenshots as a zip
- **Mac Dot Cleanup**: scan for `.DS_Store`, `._*`, `__MACOSX`, and top-level macOS transfer folders, then delete them in one pass
- **Log Viewer**: browse logs under `.userdata`, live-tail a log file, and download one or all logs
- **Terminal**: open a real shell in the browser if terminal access has been enabled on the handheld

## Handheld Settings

Press `A` on the handheld server screen to open settings.

Available device-side controls:

- **Terminal**: enable or disable browser terminal access
- **Keep Awake in Background**: prevent the device from sleeping while Central Scrutinizer is running in background mode
- **Revoke Trusted Browsers**: clear trusted browser sessions and force re-pairing
- **Run in Background**: leave the web server running after you exit back to NextUI, once at least one trusted browser has already paired

Terminal access is disabled by default on handheld builds.
Enabling **Keep Awake in Background** temporarily changes NextUI's **Screen timeout** setting to **Never** while background mode is active.
The server screen itself refreshes automatically while the pak is open.

## Important Notes

- The web dashboard can keep running in background mode after you exit, but new pairing is unavailable until you reopen the pak on the handheld.
- Enabling **Keep Awake in Background** prevents device sleep while background mode is active, temporarily changes NextUI's **Screen timeout** to **Never**, and will use more battery.
- The handheld and browser device must be on the same network.
- The default port is `8877`. If that port is unavailable, the app may fall back to another nearby port shown on screen.
- Browser terminal access opens a real shell on the device. Enable it only if you understand the risk.
- Quitting the pak from the normal foreground screen stops the local server unless you explicitly switch to background mode first.

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
