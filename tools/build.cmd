@echo off
rem Builds from a normal Windows shell by running tools/build.sh in MSYS2 UCRT64.
rem Usage: tools\build.cmd [test] [dist]
set MSYSTEM=UCRT64
set CHERE_INVOKING=1
C:\msys64\usr\bin\bash.exe -lc "tools/build.sh %*"
