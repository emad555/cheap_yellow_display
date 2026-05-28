@echo off
cd /d "%~dp0"
python -m pip install -q pyserial
python pc_listener.py COM3
pause
