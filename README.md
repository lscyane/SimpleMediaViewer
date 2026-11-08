# SimpleMediaViewer Native

Windows向けのネイティブ画像・動画ビューアーです。画像や動画を開いて表示し、同じフォルダー内の対応ファイルを順に閲覧できます。

> 現在開発中です。一般配布に向けて、依存ライブラリのライセンス表示、配布パッケージ、入力ファイルに対する堅牢性などを整備しています。

## 主な機能

- 画像・動画の表示、アニメーション再生
- 拡大・縮小、パン、回転、ウィンドウに合わせた表示、等倍表示
- フォルダー内の前後ファイルへの移動、スライドショー
- 対応するJPEG Motion Photoの動画部分の再生
- ファイルのドラッグ＆ドロップと、起動時のファイルパス指定

### 対応形式

- 画像: JPEG、PNG/APNG、BMP、TIFF、GIF、WebP、AVIF、DIB
- 動画: WebM、MP4、AVI、MKV、MOV、FLV、WMV、MPEG

実際に再生できる形式やコーデックは、同梱するLibVLCのビルドとプラグインに依存します。

### Motion Photoの一時ファイル

Motion Photoの動画部分は再生のため、`%LOCALAPPDATA%\TeaSoundProject\SimpleMediaViewer\Temp\<実行ID>` に一時保存されます。通常の再生終了、ファイル切替、アプリ終了時に削除されます。異常終了などで削除されなかったファイルは、次回起動時に24時間以上経過し、他プロセスが使用していない実行領域から削除されます。削除はOSの通常のファイル削除であり、記憶媒体上のデータの完全消去を保証するものではありません。

## 操作方法

| 操作 | キー／マウス |
| --- | --- |
| 再生／一時停止 | Space |
| 前／次のファイル | ←／→ |
| ウィンドウに合わせる | F |
| 等倍表示 | G |
| 右／左に回転 | R／L |
| スライドショーの開始／停止 | A |
| 動画を10秒戻す／進める | 4／6（テンキーも対応） |
| 動画の音量を上げる／下げる | 8／2（テンキーも対応） |
| 拡大／縮小 | マウスホイール |
| 表示位置の移動 | 左ボタンを押しながらドラッグ |
| コンテキストメニュー | 右クリック |
| 終了 | Esc |

## ビルド

### 必要な環境

- Windows x64
- Visual Studio 2026 または対応する Visual Studio C++ x64 ビルドツール
- Windows SDK、Visual Studioに含まれるMSVCとvcpkg
- VideoLAN.LibVLC.Windows 3.0.24 のネイティブSDKファイル

プロジェクトは `SimpleMediaViewer.Native/vcpkg.json` のマニフェストを使い、SDL3とSDL3_imageおよび画像形式用の依存ライブラリを復元します。

### ビルド手順

リポジトリのルートで次を実行します。

```bat
build_native.bat
```

スクリプトはNative版のRelease / x64をビルドします。成功時の出力先は次のとおりです。

```text
SimpleMediaViewer.Native\bin\x64\Release
```

現状のプロジェクト設定はLibVLC SDKを次のNuGetグローバルパッケージキャッシュから参照します。ビルド前にこのパッケージが取得済みであることを確認してください。ビルドスクリプトによるLibVLCパッケージの復元は行われません。

```text
%USERPROFILE%\.nuget\packages\videolan.libvlc.windows\3.0.24\build\x64
```

## ライセンス

本プロジェクトのソースコードは **Apache License 2.0** で公開する予定です。リポジトリ直下の `LICENSE` ファイルはまだ追加していません。

確認できた第三者ライブラリのライセンス・著作権資料は [`licenses/`](licenses/) にあります。対象とした依存関係と未確認事項は [`licenses/THIRD-PARTY-NOTICES.md`](licenses/THIRD-PARTY-NOTICES.md) を参照してください。Apache License 2.0は第三者ライブラリのライセンスを置き換えるものではありません。

現在の通知一覧はローカルのvcpkg構成を基に作成したもので、VLCプラグイン等の全同梱ファイルのライセンス確認は完了していません。一般配布前に最終配布物の依存ファイルと各ライセンス条件を確認してください。
