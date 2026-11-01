#pragma once

#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <string>

// -------------------------------------------------------------
// メディア関連設定
// -------------------------------------------------------------
// サポートする画像形式の拡張子一覧
inline static const std::array<std::wstring, 12> IMAGE_EXTENSIONS{
	L".jpg", L".jpeg", L".png", L".apng", L".bmp", L".tif", L".tiff", L".gif", L".webp", L".avif", L".dib", L".jpe"
};
// サポートする動画形式の拡張子一覧
inline static const std::array<std::wstring, 8> VIDEO_EXTENSIONS{
    L".webm", L".mp4", L".avi", L".mkv", L".mov", L".flv", L".wmv", L".mpeg"
};
inline static const auto SUPPORTED_EXTENSIONS = [] {
	std::array<std::wstring, IMAGE_EXTENSIONS.size() + VIDEO_EXTENSIONS.size()> extensions{};
	std::copy(IMAGE_EXTENSIONS.begin(), IMAGE_EXTENSIONS.end(), extensions.begin());
	std::copy(VIDEO_EXTENSIONS.begin(), VIDEO_EXTENSIONS.end(), extensions.begin() + IMAGE_EXTENSIONS.size());
	return extensions;
}();
// アニメーションデコーダーに対応する拡張子一覧
inline static const std::array<std::wstring, 4> ANIMATION_DECODER_EXTENSIONS{
	L".gif", L".webp", L".png", L".apng"
};

// -------------------------------------------------------------
// ウインドウ関連設定
// -------------------------------------------------------------
// SDLウィンドウの作成時に使用する初期幅（ピクセル）。
inline constexpr int WINDOW_INITIAL_WIDTH = 960;
// SDLウィンドウの作成時に使用する初期高さ（ピクセル）。
inline constexpr int WINDOW_INITIAL_HEIGHT = 640;
// 画像背景の市松模様を構成する1マスの幅・高さ（ピクセル）。
inline constexpr int CHECKER_CELL_SIZE = 32;
// 市松模様の明るいマスの色（RGBA）。
inline constexpr SDL_Color CHECKER_LIGHT_COLOR{ 112, 112, 112, 255 };
// 市松模様の暗いマスの色（RGBA）。
inline constexpr SDL_Color CHECKER_DARK_COLOR{ 72, 72, 72, 255 };
// コンテキストメニューの幅（論理ピクセル）。
inline constexpr float CONTEXT_MENU_WIDTH = 196.0f;
// コンテキストメニューの1項目あたりの高さ（論理ピクセル）。
inline constexpr float CONTEXT_MENU_ROW_HEIGHT = 30.0f;

// -------------------------------------------------------------
// 画像関連設定
// -------------------------------------------------------------
// true: アニメーション画像のメタデータにある再生回数を適用する。
// false: 再生回数を無視し、無限に繰り返す。
inline constexpr bool HONOR_ANIMATION_LOOP_COUNT = true;
// true: 画像を拡大・縮小するとき最近傍補間を使う。
// false: 線形補間を使う。
inline constexpr bool USE_NEAREST_NEIGHBOR_IMAGE_SCALING = true;
// モーションフォト用：XMPヘッダーの最大サイズ（バイト）。これを超える場合はXMPを読み取らない。
inline constexpr std::size_t XMP_HEADER_LIMIT = 131072;
// モーションフォト用：コピー用のバッファサイズ（バイト）。
inline constexpr std::size_t COPY_BUFFER_SIZE = 64 * 1024;

// -------------------------------------------------------------
// 動画関連設定
// -------------------------------------------------------------
// true: 動画をループ再生する。
// false: 動画を一度だけ再生する。
inline constexpr bool LOOP_VIDEO_PLAYBACK = true;
// 動画再生時の初期音量（0～100）。
inline constexpr int VIDEO_INITIAL_VOLUME = 0;
// 動画のシーク時に前後する秒数。
inline constexpr int VIDEO_SEEK_SECONDS = 10;
// 動画操作キーを押し続けた場合に連続操作を開始するまでの時間（ミリ秒）。
inline constexpr Uint64 VIDEO_KEY_REPEAT_DELAY_MS = 500;
// 動画の音量操作1回あたりの変更量（0～100）。
inline constexpr int VIDEO_VOLUME_STEP = 5;
// 音量表示を完全な不透明度で維持する時間（ミリ秒）。
inline constexpr Uint64 VOLUME_DISPLAY_DURATION_MS = 1000;
// 音量表示のフェードアウト時間（ミリ秒）。
inline constexpr Uint64 VOLUME_FADE_DURATION_MS = 500;
// フェード表示を更新する間隔（ミリ秒）。
inline constexpr int VOLUME_FADE_FRAME_INTERVAL_MS = 16;
// 音量表示テクスチャの幅・高さ（ピクセル）。
inline constexpr int VOLUME_OVERLAY_WIDTH = 220;
inline constexpr int VOLUME_OVERLAY_HEIGHT = 56;
// 音量表示をウィンドウ端から離す余白（ピクセル）。
inline constexpr int VOLUME_OVERLAY_MARGIN = 24;
// 動画フレームのピッチと高さを切り上げるアラインメント幅（バイト）。
inline constexpr unsigned VIDEO_ALIGNMENT = 32;
// 動画として受け入れる幅・高さの上限（ピクセル）。超える寸法は拒否する。
inline constexpr unsigned MAX_VIDEO_DIMENSION = 32768;