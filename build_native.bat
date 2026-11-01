@echo off
chcp 65001 > nul
setlocal

rem =================================================================
rem 設定項目
rem =================================================================
rem 1. ビルド対象
set "TARGET_PROJECT=SimpleMediaViewer.Native\SimpleMediaViewer.vcxproj"

rem 2. Visual Studio / Build Tools のインストールパス
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VS_PATH="
set "NATIVE_DIR=%~dp0SimpleMediaViewer.Native"
set "VCPKG_BIN=%NATIVE_DIR%\vcpkg_installed\x64-windows\bin"
set "BUILD_OUTPUT_DIR=%NATIVE_DIR%\bin\x64\Release"
rem =================================================================

pushd "%~dp0"
if not exist "%TARGET_PROJECT%" (
    echo 【エラー】ビルド対象が見つかりません: %TARGET_PROJECT%
    goto :error
)

echo [1/5] 環境変数の初期化中...
if not exist "%VSWHERE%" goto :vswhere_missing
for /f "usebackq tokens=* delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_PATH=%%i"
if not defined VS_PATH goto :vsvars_missing
if not exist "%VS_PATH%\VC\Auxiliary\Build\vcvars64.bat" goto :vsvars_missing
set "VCPKG_EXE=%VS_PATH%\VC\vcpkg\vcpkg.exe"

rem 64bit(x64)向け環境変数を読み込む
call "%VS_PATH%\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 (
    echo 【エラー】環境変数の読み込みに失敗しました。
    goto :error
)

where msbuild > nul 2>&1
if errorlevel 1 (
    echo 【エラー】MSBuild が見つかりません。
    goto :error
)

if not exist "%VCPKG_EXE%" goto :vcpkg_missing

echo [2/5] vcpkg 依存関係を復元中...
"%VCPKG_EXE%" install --triplet x64-windows --x-manifest-root="%CD%\SimpleMediaViewer.Native"
if errorlevel 1 goto :error

echo [3/5] 旧ビルドファイルのクリーンアップ中...
msbuild "%TARGET_PROJECT%" /t:Clean /p:Configuration=Release /p:Platform=x64
if errorlevel 1 goto :error

echo [4/5] ビルド中 (Native / Release / x64)...
msbuild "%TARGET_PROJECT%" /t:Build /p:Configuration=Release /p:Platform=x64 /p:RuntimeLibrary=MultiThreaded
if errorlevel 1 goto :error

echo [5/5] vcpkg の実行時 DLL を配置中...
if not exist "%VCPKG_BIN%\SDL3.dll" goto :dependencies_missing
if not exist "%VCPKG_BIN%\SDL3_image.dll" goto :dependencies_missing
xcopy /Y /I "%VCPKG_BIN%\*.dll" "%BUILD_OUTPUT_DIR%\" > nul
if errorlevel 1 goto :error

echo =================================================================
echo 【成功】ビルドが正常に完了しました！
echo %BUILD_OUTPUT_DIR% を確認してください。
echo =================================================================
popd
exit /b 0

:error
echo =================================================================
echo 【失敗】ビルドプロセス中にエラーが発生しました。
echo =================================================================
popd
exit /b 1

:vsvars_missing
echo 【エラー】x64 ビルド環境が見つかりません。
echo Visual Studio または Build Tools に C++ x64 ビルドツールがインストールされているか確認してください。
if defined VS_PATH echo 検出したパス: %VS_PATH%
goto :error

:vswhere_missing
echo 【エラー】Visual Studio のインストール情報を検索できません。
echo vswhere.exe が見つかりません: %VSWHERE%
goto :error

:vcpkg_missing
echo 【エラー】vcpkg が見つかりません: %VCPKG_EXE%
goto :error

:dependencies_missing
echo 【エラー】vcpkg の実行時 DLL が見つかりません。
echo vcpkg の出力先: %VCPKG_BIN%
goto :error
