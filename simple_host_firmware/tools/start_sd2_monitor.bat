@echo off
REM SD2 Copilot Monitor — Auto-start script
REM Place shortcut in: shell:startup
REM Or run manually to start monitoring

cd /d "C:\Users\Lanto\Documents\VibeSider\simple_host_firmware"
set SD2_PORT=COM4
start "SD2 Monitor" python tools\sd2_signal_hook.py --watch
