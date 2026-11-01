@echo off
chcp 65001 > nul
setlocal

rem =================================================================
rem 設定項目
rem =================================================================
rem 1. ビルド対象
set "TARGET_PROJECT=SimpleMediaViewer.Native\SimpleMediaViewer.vcxproj"

rem 2. Visual Studio / Build Tools のインストールパス
set "VS_PATH=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools"
set "VCPKG_EXE=%VS_PATH%\VC\vcpkg\vcpkg.exe"
rem =================================================================

pushd "%~dp0"
if not exist "%TARGET_PROJECT%" (
    echo 【エラー】ビルド対象が見つかりません: %TARGET_PROJECT%
    goto :error
)

echo [1/3] 環境変数の初期化中...
if not exist "%VS_PATH%\VC\Auxiliary\Build\vcvars64.bat" goto :vsvars_missing

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

echo [2/4] vcpkg 依存関係を復元中...
"%VCPKG_EXE%" install --triplet x64-windows --x-manifest-root="%CD%\SimpleMediaViewer.Native"
if errorlevel 1 goto :error

echo [3/4] 旧ビルドファイルのクリーンアップ中...
msbuild "%TARGET_PROJECT%" /t:Clean /p:Configuration=Release /p:Platform=x64
if errorlevel 1 goto :error

echo [4/4] ビルド中 (Native / Release / x64)...
msbuild "%TARGET_PROJECT%" /t:Build /p:Configuration=Release /p:Platform=x64 /p:RuntimeLibrary=MultiThreaded
if errorlevel 1 goto :error

echo =================================================================
echo 【成功】ビルドが正常に完了しました！
echo SimpleMediaViewer.Native\bin\x64\Release を確認してください。
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
echo 設定パス: %VS_PATH%\VC\Auxiliary\Build\vcvars64.bat
goto :error

:vcpkg_missing
echo 【エラー】vcpkg が見つかりません: %VCPKG_EXE%
goto :error
