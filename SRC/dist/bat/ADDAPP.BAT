@ECHO OFF
REM NOS-DOS: ADDAPP - Add an entry to the F9 application launcher
REM Usage: ADDAPP Label Dir Exec
REM Example: ADDAPP Rogue C:\GAMES\ROGUE C:\GAMES\ROGUE\ROGUE.EXE
IF "%1"=="" GOTO USAGE
IF "%2"=="" GOTO USAGE
IF "%3"=="" GOTO USAGE
SET NOS_APP=%1^|%2^|%3
ECHO %NOS_APP%>> C:\NOS\SHELL\LAUNCHER.CFG
ECHO Added to launcher: %1
SET NOS_APP=
GOTO END
:USAGE
ECHO Usage:   ADDAPP Label Dir Exec
ECHO Example: ADDAPP Rogue C:\GAMES\ROGUE C:\GAMES\ROGUE\ROGUE.EXE
ECHO.
ECHO Entries appear immediately in the F9 launcher in NOS-SHELL.
ECHO To remove an entry, edit C:\NOS\SHELL\LAUNCHER.CFG directly.
:END
