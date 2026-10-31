@echo off
if exist "%~dp0.tools\python312\python.exe" (
  "%~dp0.tools\python312\python.exe" "%~dp0tools\manage_simulator.py" start
) else (
  python "%~dp0tools\manage_simulator.py" start
)
if errorlevel 1 pause
