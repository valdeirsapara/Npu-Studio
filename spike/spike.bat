@echo off
rem Runs spike.exe with the OpenVINO DLLs on PATH. Same arguments as spike.exe.
call "C:\Intel\openvino_genai\setupvars.bat" >nul
"%~dp0build\Release\spike.exe" %*
