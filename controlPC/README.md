# CYD PC Launchpad

Touch launcher for the **Cheap Yellow Display (ESP32-2432S028)**. Tap an icon to open apps on your Windows PC over USB serial.

Two pages: **Apps & tools** (Chrome, Steam, Fusion 360, etc.) and **Microsoft Office**. Swipe left/right to change page.

## What you need

| Part | Details |
|------|---------|
| Board | ESP32 CYD (2.8" ILI9341 + XPT2046 touch) |
| PC | Windows 10/11, USB cable |
| Arduino IDE | 2.x recommended |
| Python 3 | For the PC listener |

## Project files

```
controlPC/
├── controlPC.ino      # Upload this sketch to the CYD
├── icons.h            # Icon bitmaps (included)
├── pc_listener.py     # Runs on PC — opens apps from serial commands
├── start_listener.bat # Quick start (edit COM port if needed)
├── requirements.txt   # Python: pyserial only
└── tools/
    ├── generate_icons.py   # Optional: rebuild icons.h
    └── requirements.txt    # pillow, requests, etc. (icons only)
```

## 1. Arduino setup (one time)

### Install libraries

In Arduino IDE → **Sketch → Include Library → Manage Libraries**, install:

- **TFT_eSPI** (Bodmer)
- **XPT2046_Touchscreen**

### Configure TFT_eSPI for CYD

Edit `User_Setup.h` in your TFT_eSPI library folder (e.g. `Documents/Arduino/libraries/TFT_eSPI/User_Setup.h`).

Use a **CYD / ESP32-2432S028** profile (ILI9341, correct pins). The [Random Nerd Tutorials CYD guide](https://randomnerdtutorials.com/esp32-cheap-yellow-display-cyd-pinout-esp32-2432s028r/) is a good reference.

Important for this project:

- Driver: **ILI9341** (or ILI9341_2 if that’s what your board uses)
- Leave color order at the default that works for your board (this sketch draws icons without `setSwapBytes` on the main display path)
- Touch is handled by **XPT2046_Touchscreen**, not TFT_eSPI touch

### Board settings

- **Board:** ESP32 Dev Module (or your CYD variant)
- **Upload speed:** 921600 or 115200 if upload fails
- **Port:** your CYD COM port (e.g. `COM3`)

### Upload

1. Open `controlPC.ino` in Arduino IDE (folder name must match: `controlPC/controlPC.ino`).
2. Select the correct **COM port**.
3. Click **Upload**.
4. If upload fails with “Wrong boot mode”, hold **BOOT**, press **RESET**, release **BOOT**, then upload again.

**Close the Serial Monitor** before running the PC listener (only one program can use the COM port).

## 2. PC listener setup (one time)

```powershell
cd path\to\controlPC
python -m pip install -r requirements.txt
```

Or double-click **`start_listener.bat`** (installs pyserial and starts the listener).

### COM port

Default is **COM3**. Change it in either place:

- **`start_listener.bat`** — edit the line `python pc_listener.py COM3`
- **Command line:** `python pc_listener.py COM4`

To find your port: Device Manager → **Ports (COM & LPT)** → `USB-SERIAL CH340 (COMx)` (name may vary).

## 3. Run everything (every day)

1. Plug in the CYD via USB.
2. Upload the sketch (only when you change code).
3. **Do not** open Arduino Serial Monitor.
4. Start the PC listener:
   - Double-click **`start_listener.bat`**, or  
   - `python pc_listener.py COM3`
5. On the display:
   - **Tap** an icon → opens that app on the PC.
   - **Swipe** left/right in the grid → second page (Office apps).

You should see `Listening on COM3 at 115200 baud...` in the terminal.

## Apps launched

| Page | Apps |
|------|------|
| Apps & tools | Fusion 360, eufyMake, Steam, Chrome, Cursor, Arduino IDE |
| Microsoft Office | Word, Excel, PowerPoint, Outlook, OneNote, Teams |

If an app is not found, edit paths in **`pc_listener.py`** (`CHROME_PATHS`, `FUSION_PATTERNS`, `OFFICE_EXES`, etc.) and restart the listener.

## Troubleshooting

| Problem | Fix |
|---------|-----|
| Upload fails | Hold BOOT during upload; try 115200 baud; use a data USB cable |
| `Serial error` / port busy | Close Serial Monitor and any other app using the COM port |
| Tap opens wrong app / swipe by accident | Press firmly on the icon; avoid dragging |
| Wrong colors on icons | Regenerate `icons.h` with `tools/generate_icons.py` or restore from repo |
| App not opening | Check listener terminal for errors; fix path in `pc_listener.py` |

## Optional: regenerate icons

```powershell
pip install -r tools/requirements.txt
python tools/generate_icons.py
```

Then re-upload `controlPC.ino` (uses the new `icons.h`).

You can drop custom PNGs in `assets/icons/` (e.g. `icon_chrome.png`) before running the script.

## Display rotation

The sketch uses **rotation 3** (180°). To flip orientation, change both in `controlPC.ino`:

```cpp
touchscreen.setRotation(3);
tft.setRotation(3);
```

Use `0`, `1`, `2`, or `3` until the UI matches how you hold the board.

## License

Part of [cheap_yellow_display](https://github.com/emad555/cheap_yellow_display).
