# SimpleMediaViewer.Native 引き継ぎメモ

## ワークスペース

- ルート: `D:\work\Program\TSP\WindowsWPF\SimpleMediaViewer`
- ソリューション: `SimpleMediaViewer.slnx`
- IDE: Visual Studio Community 2026 (18.11.1)
- Gitブランチ: `Native実装検討`
- プロジェクト: `SimpleMediaViewer.Native\SimpleMediaViewer.vcxproj`（C++/SDL3）と `SimpleMediaViewer.Wpf`（.NET 10/WPF）
- 作業範囲はネイティブ単体アプリ。C# IPC連携は作らない。

## 目的・仕様

既存WPFビューアを参照しながら、C++/SDL3のみで静止画・動画ビューアを成立させる。LibVLCは動画再生要求時に初期化し、動画フレームコールバックは複数スレッドから安全に受け、SDL描画APIは描画スレッドで扱う。

GIF/WebPのアニメーション再生は対象外で、静止フレーム表示のみとする。

## 現在までの実装

- SDL3のウィンドウ、イベントループ、市松背景と画像描画
- 静止画読み込みにSDL3_imageを使用
- ズーム・パン、右クリックメニュー
- 同一フォルダーの対応メディア列挙、`StrCmpLogicalW` による自然順ソート、左右矢印による循環ナビゲーション
- 対応拡張子の列挙と動画判定に `.mpeg` を含む
- LibVLCの遅延初期化、RV32フレーム用の複数バッファ、SDLイベントによる描画スレッドへの通知
- 起動引数のファイルパス表示に対応

主要ファイル:

- `SimpleMediaViewer.Native\src\main.cpp`: SDLアプリ、イベント、メディア列挙・ナビゲーション
- `SimpleMediaViewer.Native\src\ImagePlayer.cpp/.h`: 静止画テクスチャ読み込み
- `SimpleMediaViewer.Native\src\VideoPlayer.cpp/.h`: LibVLC初期化、フレームコールバック、SDLテクスチャ更新
- `SimpleMediaViewer.Native\SimpleMediaViewer.vcxproj`: SDL/VLC依存関係と出力配置

## ビルド・配置確認

ユーザーがVisual StudioでNativeをスタートアップに切り替え、Release x64ビルドを実行。ビルド出力は成功:

`SimpleMediaViewer.vcxproj -> artifacts\SimpleMediaViewer\x64\Release\SimpleMediaViewer.exe`

出力先 `artifacts\SimpleMediaViewer\x64\Release` に以下を確認:

- `SimpleMediaViewer.exe`, `SDL3.dll`, `SDL3_image.dll`, `libvlc.dll`, `libvlccore.dll`
- `plugins`（323ファイル）, `hrtfs`（1ファイル）, `lua`（94ファイル）
- Releaseアプリの起動とSDL3/SDL3_image/LibVLC DLLのロード

DLLがロードされたことは確認したが、LibVLCによる実際の動画再生・プラグイン利用までは未確認。DLLはリンクにより起動時にロードされる一方、LibVLCインスタンスの遅延生成が動画再生時だけであることも実再生ではまだ検証していない。

## 右矢印入力の調査結果

- `main.cpp` の `SDL_EVENT_KEY_DOWN` で `event.key.key == SDLK_RIGHT` のとき `OpenRelativeFile(1)` を呼び、その後 `OpenPath` がタイトルを更新する経路を確認。
- デバッグ実行中、UIからの右矢印でSDLイベントのキーコード `1073741903`（SDLK_RIGHT）を捕捉し、`OpenRelativeFile(1)` へ到達した。別の試行では一覧0件・選択パス空だったため、タイトルが変わらないのはナビゲーション対象がない状態では当然。
- Releaseで一時PNG 2枚を同じフォルダーに置いて起動し、初期タイトル `image1.png` を確認した。`WScript.Shell.AppActivate` は成功したが、`SendKeys('{RIGHT}')` 後もタイトルは `image1.png` のまま。Win32 `keybd_event` でもタイトルは変化しなかった。
- 以上から、SDL側の右矢印処理経路はデバッグ実入力で確認済みだが、Releaseの画像ペアを使ったナビゲーションは未確定。SendKeys系の合成入力がアプリに届いたかは確認できておらず、これだけを根拠にコード不具合とは判断しない。ユーザー報告の以前の試験も、当時のテスト用画像ペアが既に削除されていたため判定材料にならなかった。
- 一時テスト用画像フォルダーは後片付け済み。ソースコードの変更は行っていない。

## 次に行うこと

1. Releaseアプリを、対応画像が2枚以上ある実フォルダーまたは新しい一時テストフォルダーから起動する。
2. アプリウィンドウを実際にフォーカスして、物理的な右矢印入力後にタイトルが次のファイル名へ変わるか確認する。キーが届かない場合は、SDLイベントの受信と `OpenRelativeFile` 到達をデバッガーで分けて判定する。
3. 利用可能な短い動画で再生を試し、LibVLC初期化、プラグイン読込、フレーム表示、停止・終了時の解放を確認する。
4. 必要な不具合だけ修正し、Release x64を再ビルドする。
5. GIF/WebPアニメーションの非対応とC# IPC未実装は維持する。

## チーム表記

「壁時計」「Wall Clock Time」ではなく「実時間」を使う。
