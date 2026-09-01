@echo off
setlocal
cd /d "%~dp0"
title ExpeditionOnline Server
echo Starting ExpeditionOnline exploration server...
echo Keep this window open while playing.
echo.
ExpeditionOnlineServer.exe --config server.ini
echo.
echo The server stopped. Review ExpeditionOnlineServer.log for details.
pause
