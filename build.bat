@echo off

if not exist build/ mkdir build

set options=-nologo -W4 -WX
set options=%options% -D_DEBUG -Zi
set options=%options% -Febuild/sugar.exe -Fobuild/ -Fdbuild/
set options=%options% src/*.c

cl %options%