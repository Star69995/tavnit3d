@echo off
rem Runs the development build (build\dune3d.exe) with MSYS2's libraries on PATH.
rem Only dist\dune3d\dune3d.exe runs without MSYS2.
set "PATH=C:\msys64\ucrt64\bin;%PATH%"
set "HOME=%USERPROFILE%"
"%~dp0..\build\dune3d.exe" %*
