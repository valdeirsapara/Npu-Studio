@echo off
rem Runs hfstudio.exe with the OpenVINO DLLs on PATH. Same arguments as hfstudio.exe.
call "C:\Intel\openvino_genai\setupvars.bat" >nul
"%~dp0build\cli\Release\hfstudio.exe" %*
