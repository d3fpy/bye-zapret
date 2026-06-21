@echo off
chcp 65001 >nul
color 05
title bye zapret by d3fpy
mode con: cols=65 lines=24

for /f %%A in ('echo prompt $E ^| cmd') do set "ESC=%%A"
set "RED=%ESC%[91m"
set "GREEN=%ESC%[92m"
set "YELLOW=%ESC%[93m"
set "CYAN=%ESC%[96m"
set "MAGENTA=%ESC%[95m"
:: Возврат к основному фиолетовому цвету консоли (вместо стандартного белого)
set "RESET=%ESC%[35m"

:: Проверка прав администратора
net session >nul 2>&1
if %errorLevel% neq 0 (
    color 0C
    echo [ОШИБКА] Запустите скрипт ОТ ИМЕНИ АДМИНИСТРАТОРА!
    pause
    exit /b
)

echo %CYAN%=================================================================%RESET%
echo             bye zapret powered by github.com/d3fpy
echo %CYAN%=================================================================%RESET%
echo.

for /f %%A in ('echo prompt $H ^| cmd') do set "back=%%A"

echo [%YELLOW%ИНФО%RESET%] Подготовка очистки трафика от winws.exe 
echo.


echo [%YELLOW%ПРОЦЕСС%RESET%] Начинается полная очистка старых служб...
echo.

:: 1. Остановка службы zapret
sc query "zapret" >nul 2>&1
if %errorlevel%==0 (
    <nul set /p ="  Останавливаем службу zapret... "
    net stop zapret >nul 2>&1
    sc delete zapret >nul 2>&1
    call :animate 2
    echo [%GREEN%УСПЕШНО%RESET%]
) else (
    echo   Служба zapret не найдена в системе.
)

:: 2. Закрытие процессов winws.exe
tasklist /FI "IMAGENAME eq winws.exe" 2>nul | find /I "winws.exe" > nul
if %errorlevel%==0 (
    <nul set /p ="  Закрываем процессы winws.exe... "
    taskkill /IM winws.exe /F > nul 2>&1
    call :animate 2
    echo [%GREEN%УСПЕШНО%RESET%]
) else (
    echo   Активных процессов winws.exe не обнаружено.
)

:: 3. Очистка драйверов WinDivert
<nul set /p ="  Выгружаем сетевые драйверы...   "
net stop "WinDivert" >nul 2>&1
sc delete "WinDivert" >nul 2>&1
net stop "WinDivert14" >nul 2>&1
sc delete "WinDivert14" >nul 2>&1
call :animate 3
echo [%GREEN%ОЧИЩЕНО%RESET%]

<nul set /p ="  Очистка кэша DNS и ARP...       "
ipconfig /flushdns >nul 2>&1
arp -d * >nul 2>&1
call :animate 2
echo [%GREEN%ГОТОВО%RESET%]

<nul set /p ="  Сброс протоколов IP и Winsock... "
netsh int ip reset >nul 2>&1
netsh winsock reset >nul 2>&1
call :animate 3
echo [%GREEN%УСПЕШНО%RESET%]

echo.
echo %GREEN%=================================================================%RESET%
echo                         %RED%Готово! Проблемы? пишите в Issues%RESET%
echo %GREEN%=================================================================%RESET%
echo.
pause
exit /b


:animate
set "steps=%~1"
for /L %%i in (1,1,%steps%) do (
    <nul set /p ="[ | ]"
    powershell -NoProfile -Command "Start-Sleep -m 150"
    <nul set /p ="%back%%back%%back%%back%%back%[ / ]"
    powershell -NoProfile -Command "Start-Sleep -m 150"
    <nul set /p ="%back%%back%%back%%back%%back%[ - ]"
    powershell -NoProfile -Command "Start-Sleep -m 150"
    <nul set /p ="%back%%back%%back%%back%%back%[ \ ]"
    powershell -NoProfile -Command "Start-Sleep -m 150"
    <nul set /p ="%back%%back%%back%%back%%back%"
)
exit /b
