"""
Listens on the CYD serial port and launches apps when the ESP32 sends commands.
Close Arduino Serial Monitor before running this script.
"""

import glob
import os
import subprocess
import sys
from typing import Callable, Optional

import serial

PORT = "COM3"
BAUD = 115200

CHROME_PATHS = [
    r"C:\Program Files\Google\Chrome\Application\chrome.exe",
    r"C:\Program Files (x86)\Google\Chrome\Application\chrome.exe",
    os.path.expandvars(r"%LOCALAPPDATA%\Google\Chrome\Application\chrome.exe"),
]

STEAM_PATHS = [
    r"C:\Program Files (x86)\Steam\steam.exe",
    r"C:\Program Files\Steam\steam.exe",
]

EUFYMAKE_PATTERNS = [
    r"C:\Program Files\eufyMake Studio\*.exe",
    r"C:\Program Files\EufyMake Studio\*.exe",
    r"C:\Program Files\AnkerMake\eufyMake Studio\*.exe",
    r"C:\Program Files\AnkerMake\AnkerMake Studio\*.exe",
    os.path.expandvars(r"%LOCALAPPDATA%\Programs\eufyMake Studio\*.exe"),
    os.path.expandvars(r"%LOCALAPPDATA%\eufyMake Studio\*.exe"),
]

FUSION_PATTERNS = [
    os.path.expandvars(
        r"%LOCALAPPDATA%\Autodesk\webdeploy\production\*\Fusion360.exe"
    ),
    r"C:\Program Files\Autodesk\Fusion 360\Fusion360.exe",
]

CURSOR_PATHS = [
    os.path.expandvars(r"%LOCALAPPDATA%\Programs\cursor\Cursor.exe"),
    os.path.expandvars(r"%LOCALAPPDATA%\cursor\Cursor.exe"),
    os.path.expandvars(r"%ProgramFiles%\Cursor\Cursor.exe"),
]

ARDUINO_PATHS = [
    r"C:\Program Files\Arduino IDE\Arduino IDE.exe",
    os.path.expandvars(r"%LOCALAPPDATA%\Programs\Arduino IDE\Arduino IDE.exe"),
    r"C:\Program Files (x86)\Arduino\arduino.exe",
    r"C:\Program Files\Arduino\arduino.exe",
]

ARDUINO_GLOB = [
    r"C:\Program Files\**\Arduino IDE.exe",
    r"C:\Program Files\**\arduino.exe",
]


def first_existing(paths: list[str]) -> Optional[str]:
    for path in paths:
        if os.path.isfile(path):
            return path
    return None


def first_glob(patterns: list[str]) -> Optional[str]:
    for pattern in patterns:
        matches = sorted(glob.glob(pattern))
        if matches:
            return matches[0]
    return None


def launch(path: str, label: str) -> None:
    subprocess.Popen([path], shell=False)
    print(f"Opened {label}: {path}")


def open_chrome() -> None:
    chrome = first_existing(CHROME_PATHS)
    if chrome:
        launch(chrome, "Chrome")
    else:
        subprocess.Popen("start chrome", shell=True)
        print("Opened Chrome via shell")


def open_steam() -> None:
    steam = first_existing(STEAM_PATHS)
    if steam:
        launch(steam, "Steam")
    else:
        subprocess.Popen("start steam:", shell=True)
        print("Opened Steam via URI")


def open_fusion360() -> None:
    fusion = first_glob(FUSION_PATTERNS)
    if fusion:
        launch(fusion, "Fusion 360")
    else:
        print("Fusion 360 not found. Install it or add path in pc_listener.py")


def open_eufymake() -> None:
    exe = first_glob(EUFYMAKE_PATTERNS)
    if not exe:
        for pattern in (
            r"C:\Program Files\**\AnkerStudio.exe",
            r"C:\Program Files\**\eufyMake Studio.exe",
            r"C:\Program Files\**\eufyMakeStudio.exe",
        ):
            exe = first_glob([pattern])
            if exe:
                break
    if exe:
        launch(exe, "eufyMake Studio")
    else:
        print("eufyMake Studio not found. Install it or add path in pc_listener.py")


def open_cursor() -> None:
    cursor = first_existing(CURSOR_PATHS)
    if cursor:
        launch(cursor, "Cursor")
    else:
        subprocess.Popen("start cursor:", shell=True)
        print("Opened Cursor via shell")


def open_arduino() -> None:
    arduino = first_existing(ARDUINO_PATHS)
    if not arduino:
        arduino = first_glob(ARDUINO_GLOB)
    if arduino:
        launch(arduino, "Arduino IDE")
    else:
        print("Arduino IDE not found. Install it or add path in pc_listener.py")


OFFICE_EXES = {
    "OPEN_WORD": ("WINWORD.EXE", "Word"),
    "OPEN_EXCEL": ("EXCEL.EXE", "Excel"),
    "OPEN_POWERPOINT": ("POWERPNT.EXE", "PowerPoint"),
    "OPEN_OUTLOOK": ("OUTLOOK.EXE", "Outlook"),
    "OPEN_ONENOTE": ("ONENOTE.EXE", "OneNote"),
    "OPEN_TEAMS": ("MS-TEAMS.EXE", "Teams"),
}


def open_office(command: str) -> None:
    exe_name, label = OFFICE_EXES[command]
    patterns = [
        rf"C:\Program Files\Microsoft Office\**\{exe_name}",
        rf"C:\Program Files (x86)\Microsoft Office\**\{exe_name}",
        rf"C:\Program Files\Microsoft Office\root\Office*\{exe_name}",
    ]
    path = first_glob(patterns)
    if path:
        launch(path, label)
    else:
        print(f"{label} not found ({exe_name}). Check Office install path.")


def make_office_handler(cmd: str) -> Callable[[], None]:
    return lambda c=cmd: open_office(c)


HANDLERS: dict[str, Callable[[], None]] = {
    "OPEN_CHROME": open_chrome,
    "OPEN_STEAM": open_steam,
    "OPEN_FUSION360": open_fusion360,
    "OPEN_EUFYMAKE": open_eufymake,
    "OPEN_CURSOR": open_cursor,
    "OPEN_ARDUINO": open_arduino,
    "OPEN_WORD": make_office_handler("OPEN_WORD"),
    "OPEN_EXCEL": make_office_handler("OPEN_EXCEL"),
    "OPEN_POWERPOINT": make_office_handler("OPEN_POWERPOINT"),
    "OPEN_OUTLOOK": make_office_handler("OPEN_OUTLOOK"),
    "OPEN_ONENOTE": make_office_handler("OPEN_ONENOTE"),
    "OPEN_TEAMS": make_office_handler("OPEN_TEAMS"),
}


def main() -> None:
    port = sys.argv[1] if len(sys.argv) > 1 else PORT
    print(f"Listening on {port} at {BAUD} baud...")
    print("Apps:", ", ".join(HANDLERS.keys()))
    print("Press Ctrl+C to quit.\n")

    with serial.Serial(port, BAUD, timeout=1) as ser:
        while True:
            line = ser.readline().decode("utf-8", errors="ignore").strip()
            handler = HANDLERS.get(line)
            if handler:
                handler()
            elif line:
                print(f"[device] {line}")


if __name__ == "__main__":
    try:
        main()
    except serial.SerialException as e:
        print(f"Serial error: {e}")
        print("Close Arduino Serial Monitor and check COM port.")
        sys.exit(1)
    except KeyboardInterrupt:
        print("\nStopped.")
