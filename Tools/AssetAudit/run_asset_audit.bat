@echo off
setlocal
set PYTHON=python.exe
set SCRIPT_DIR=%~dp0

if "%~1"=="" (
    echo �÷������ʲ���Ƶ����� CSV �ϵ���� bat �ļ��ϣ�
    echo       �������ն��run_asset_audit.bat CSV·�� [���xlsx·��]
    pause
    exit /b 1
)

set INPUT=%~1
if "%~2"=="" (
    set OUTPUT=%~dpn1_report.xlsx
) else (
    set OUTPUT=%~2
)

"%PYTHON%" "%SCRIPT_DIR%asset_audit_report.py" "%INPUT%" -o "%OUTPUT%" --config "%SCRIPT_DIR%asset_audit_config.sample.json"

echo.
echo ��ɣ�������: %OUTPUT%
pause
