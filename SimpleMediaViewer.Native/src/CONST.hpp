#pragma once

#include <SDL3/SDL.h>

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

// -------------------------------------------------------------
// 動画関連設定
// -------------------------------------------------------------
// 動画フレームのピッチと高さを切り上げるアラインメント幅（バイト）。
inline constexpr unsigned VIDEO_ALIGNMENT = 32;
// 動画として受け入れる幅・高さの上限（ピクセル）。超える寸法は拒否する。
inline constexpr unsigned MAX_VIDEO_DIMENSION = 32768;