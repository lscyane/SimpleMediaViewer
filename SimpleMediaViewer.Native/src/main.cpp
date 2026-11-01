#define SDL_MAIN_HANDLED
#define NOMINMAX

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include "CONST.hpp"
#include "FileManager.h"
#include "ImagePlayer.h"
#include "MotionPhotoHelper.h"
#include "VideoPlayer.h"
#include <Windows.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cwctype>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#pragma comment(lib, "Shell32.lib")

namespace
{
    /// <summary>
    /// WindowsのUTF-16文字列をSDL API用のUTF-8文字列へ変換します。
    /// </summary>
    std::string ToUtf8(const std::wstring& text)
    {
        if (text.empty())
        {
            return {};
        }

        const int requiredLength = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (requiredLength <= 1)
        {
            return {};
        }

        std::string result(static_cast<size_t>(requiredLength), '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, result.data(), requiredLength, nullptr, nullptr);
        result.resize(static_cast<size_t>(requiredLength - 1));
        return result;
    }


    /// <summary>
    /// SDLイベントのUTF-8パスをWindows API用のUTF-16文字列へ変換します。
    /// </summary>
    std::wstring ToWide(const char* text)
    {
        if (text == nullptr || *text == '\0')
        {
            return {};
        }

        const int requiredLength = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
        if (requiredLength <= 1)
        {
            return {};
        }

        std::wstring result(static_cast<size_t>(requiredLength), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text, -1, result.data(), requiredLength);
        result.resize(static_cast<size_t>(requiredLength - 1));
        return result;
    }


    /// <summary>
    /// 起動引数から最初のファイルパスを取り出します。
    /// </summary>
    std::wstring GetStartupFilePath()
    {
        int argumentCount = 0;
        LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
        if (arguments == nullptr)
        {
            return {};
        }

        std::wstring filePath = argumentCount > 1 ? arguments[1] : L"";
        LocalFree(arguments);
        return filePath;
    }


    /// <summary>
    /// SDL3のウィンドウ、描画ループ、市松背景を所有するアプリケーションです。
    /// </summary>
    class SdlApplication final
    {
    public:
        /// <summary>
        /// SDLのウィンドウと描画リソースを初期化します。
        /// </summary>
        bool Initialize(const std::wstring& initialFilePath)
        {
            if (!SDL_Init(SDL_INIT_VIDEO))
            {
                ShowSdlError(L"SDLの初期化に失敗しました");
                return false;
            }

            videoFrameEventType_ = SDL_RegisterEvents(2);
            if (videoFrameEventType_ == static_cast<Uint32>(-1))
            {
                ShowSdlError(L"動画フレームイベントの登録に失敗しました");
                return false;
            }
            videoPlaybackEndedEventType_ = videoFrameEventType_ + 1;

            window_ = SDL_CreateWindow(
                "SimpleMediaViewer",
                WINDOW_INITIAL_WIDTH,
                WINDOW_INITIAL_HEIGHT,
                SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
            if (window_ == nullptr)
            {
                ShowSdlError(L"ウィンドウの作成に失敗しました");
                return false;
            }

            renderer_ = SDL_CreateRenderer(window_, nullptr);
            if (renderer_ == nullptr)
            {
                ShowSdlError(L"SDLレンダラーの作成に失敗しました");
                return false;
            }

            SDL_SetRenderVSync(renderer_, 1);
            checkerTexture_ = CreateCheckerTexture();
            if (checkerTexture_ == nullptr)
            {
                ShowSdlError(L"市松模様テクスチャの作成に失敗しました");
                return false;
            }

            currentFilePath_ = initialFilePath;
            UpdateWindowTitle();
            Render();
            if (!currentFilePath_.empty())
            {
                OpenPath(currentFilePath_);
            }
            return true;
        }


        /// <summary>
        /// イベントを待機し、必要な場合だけ画面を再描画します。
        /// </summary>
        void Run()
        {
            bool isRunning = true;
            bool needsRendering = true;

            while (isRunning)
            {
                fileManager_.ProcessPendingScan();
                SDL_Event event{};
                if (!needsRendering)
                {
					// 描画が不要な場合は、SDL_WaitEventTimeoutで待機する
                    int waitTimeout = fileManager_.IsScanPending() ? 20 : -1;
                    const int animationTimeout = imagePlayer_.MillisecondsUntilNextFrame();
                    if (animationTimeout >= 0)
                    {
                        waitTimeout = waitTimeout < 0 ? animationTimeout : std::min(waitTimeout, animationTimeout);
                    }

					// 音量表示のフェードアウト更新を待つ時間を計算する
                    const int volumeOverlayTimeout = MillisecondsUntilVolumeOverlayFrame();
                    if (volumeOverlayTimeout >= 0)
                    {
                        waitTimeout = waitTimeout < 0 ? volumeOverlayTimeout : std::min(waitTimeout, volumeOverlayTimeout);
                    }

					// SDL_WaitEventTimeoutでイベントを待機する。タイムアウトが発生した場合は、needsRenderingをtrueにして再描画する
                    const bool receivedEvent = (waitTimeout < 0) ? SDL_WaitEvent(&event) : SDL_WaitEventTimeout(&event, waitTimeout);
                    if (receivedEvent)
                    {
                        HandleEvent(event, isRunning, needsRendering);
                    }
                    else if (volumeOverlayTimeout >= 0 && waitTimeout == volumeOverlayTimeout)
                    {
                        needsRendering = true;
                    }
                }

				// SDL_PollEventでイベントを処理する。描画が必要な場合は、Render()を呼び出す
                while (SDL_PollEvent(&event))
                {
                    HandleEvent(event, isRunning, needsRendering);
                }

				// ファイルスキャンの処理とアニメーションの進行を行う。描画が必要な場合は、Render()を呼び出す
                fileManager_.ProcessPendingScan();
                if (imagePlayer_.AdvanceAnimation())
                {
                    needsRendering = true;
                }
                if (needsRendering && isRunning)
                {
                    Render();
                    needsRendering = false;
                }
            }
        }


        /// <summary>
        /// SDLリソースを破棄し、SDLを終了します。
        /// </summary>
        void Shutdown()
        {
            videoPlayer_.Shutdown();
            CleanupMotionPhotoTempFile();
            imagePlayer_.Clear();
            if (checkerTexture_ != nullptr)
            {
                SDL_DestroyTexture(checkerTexture_);
                checkerTexture_ = nullptr;
            }
            HideVolumeOverlay();
            if (renderer_ != nullptr)
            {
                SDL_DestroyRenderer(renderer_);
                renderer_ = nullptr;
            }
            if (window_ != nullptr)
            {
                SDL_DestroyWindow(window_);
                window_ = nullptr;
            }
            SDL_Quit();
        }


    private:
        /// <summary>
        /// 市松背景を一枚の小さな反復テクスチャとして作成します。
        /// </summary>
        SDL_Texture* CreateCheckerTexture()
        {
            constexpr int textureSize = CHECKER_CELL_SIZE * 2;
            std::array<Uint8, textureSize * textureSize * 4> pixels{};

            // 2x2セルのパターンをCPU上で一度だけ生成し、毎フレームの多数の矩形描画を避けます。
            for (int y = 0; y < textureSize; ++y)
            {
                for (int x = 0; x < textureSize; ++x)
                {
                    const bool isLightCell = ((x / CHECKER_CELL_SIZE) + (y / CHECKER_CELL_SIZE)) % 2 == 0;
                    const SDL_Color color = isLightCell ? CHECKER_LIGHT_COLOR : CHECKER_DARK_COLOR;
                    const size_t pixelIndex = static_cast<size_t>((y * textureSize + x) * 4);
                    pixels[pixelIndex] = color.r;
                    pixels[pixelIndex + 1] = color.g;
                    pixels[pixelIndex + 2] = color.b;
                    pixels[pixelIndex + 3] = color.a;
                }
            }

            SDL_Texture* texture = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, textureSize, textureSize);
            if (texture == nullptr)
            {
                return nullptr;
            }

            if (!SDL_UpdateTexture(texture, nullptr, pixels.data(), textureSize * 4))
            {
                SDL_DestroyTexture(texture);
                return nullptr;
            }

            SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
            return texture;
        }


        /// <summary>
        /// SDLイベントを処理し、終了・再描画・ドロップされたファイルを反映します。
        /// </summary>
        void HandleEvent(const SDL_Event& event, bool& isRunning, bool& needsRendering)
        {
            if (event.type == videoFrameEventType_)
            {
                videoPlayer_.UpdateTexture();
                needsRendering = true;
                return;
            }
            if (event.type == videoPlaybackEndedEventType_)
            {
                if (isMotionPhotoVideoPlaying_ && event.user.code == videoPlayer_.PlaybackGeneration())
                {
                    videoPlayer_.Stop();
                    isMotionPhotoVideoPlaying_ = false;
                    CleanupMotionPhotoTempFile();
                    needsRendering = true;
                }
                return;
            }

            switch (event.type)
            {
			// SDL_EVENT_QUITとSDL_EVENT_WINDOW_CLOSE_REQUESTEDは、アプリケーションを終了するためのイベントです。isRunningフラグをfalseに設定して、メインループを終了させます。
            case SDL_EVENT_QUIT:
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                isRunning = false;
                break;
			// SDL_EVENT_WINDOW_FOCUS_GAINEDは、ウィンドウがフォーカスを取得したときに発生するイベントです。動画の再生制御キーの状態をリセットします。
            case SDL_EVENT_WINDOW_FOCUS_LOST:
                ResetVideoControlKeys();
                break;
			// SDL_EVENT_KEY_DOWNは、キーが押されたときに発生するイベントです。動画の再生制御キーの状態を更新し、ESCキーでアプリケーションを終了、SPACEキーでアニメーションまたは動画の再生/一時停止を切り替え、左右キーで前後のファイルを開きます。
            case SDL_EVENT_KEY_DOWN:
                if (HandleVideoControlKeyDown(event.key, needsRendering))
                {
                    break;
                }
                if (!event.key.repeat && event.key.key == SDLK_ESCAPE)
                {
                    isRunning = false;
                }
                else if (!event.key.repeat && event.key.key == SDLK_SPACE)
                {
                    if (imagePlayer_.IsAnimated())
                    {
                        imagePlayer_.TogglePause();
                    }
                    else
                    {
                        videoPlayer_.TogglePause();
                    }
                }
                else if (!event.key.repeat && event.key.key == SDLK_RIGHT)
                {
                    OpenRelativeFile(1);
                    needsRendering = true;
                }
                else if (!event.key.repeat && event.key.key == SDLK_LEFT)
                {
                    OpenRelativeFile(-1);
                    needsRendering = true;
                }
                break;
			// SDL_EVENT_KEY_UPは、キーが離されたときに発生するイベントです。動画の再生制御キーの状態を更新します。
            case SDL_EVENT_KEY_UP:
                HandleVideoControlKeyUp(event.key.scancode);
                break;
			// SDL_EVENT_MOUSE_BUTTON_DOWNは、マウスボタンが押されたときに発生するイベントです。右クリックでコンテキストメニューを表示し、左クリックでコンテキストメニューの項目を選択するか、パン操作を開始します。
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (event.button.button == SDL_BUTTON_RIGHT)
                {
                    int windowWidth = 0;
                    int windowHeight = 0;
                    SDL_GetWindowSize(window_, &windowWidth, &windowHeight);
                    contextMenuX_ = std::clamp(event.button.x, 0.0f, std::max(0.0f, windowWidth - CONTEXT_MENU_WIDTH));
                    contextMenuY_ = std::clamp(event.button.y, 0.0f, std::max(0.0f, windowHeight - CONTEXT_MENU_ROW_HEIGHT * 4.0f));
                    contextMenuOpen_ = true;
                    isPanning_ = false;
                    needsRendering = true;
                }
                else if (event.button.button == SDL_BUTTON_LEFT)
                {
                    if (contextMenuOpen_)
                    {
                        HandleContextMenuClick(event.button.x, event.button.y, isRunning);
                        needsRendering = true;
                    }
                    else
                    {
                        isPanning_ = true;
                    }
                }
                break;
			// SDL_EVENT_MOUSE_BUTTON_UPは、マウスボタンが離されたときに発生するイベントです。左クリックでパン操作を終了します。
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (event.button.button == SDL_BUTTON_LEFT)
                {
                    isPanning_ = false;
                }
                break;
			// SDL_EVENT_MOUSE_MOTIONは、マウスが移動したときに発生するイベントです。パン操作中の場合は、マウスの移動量に応じて表示位置を更新します。
            case SDL_EVENT_MOUSE_MOTION:
                if (isPanning_)
                {
                    panX_ += event.motion.xrel;
                    panY_ += event.motion.yrel;
                    needsRendering = true;
                }
                break;

            // マウスホイールイベント
            case SDL_EVENT_MOUSE_WHEEL:
            {
                const float direction = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f;
                const float previousZoom = zoom_;
                zoom_ = std::clamp(zoom_ * std::pow(1.15f, event.wheel.y * direction), 0.05f, 32.0f);
                if (zoom_ != previousZoom)
                {
                    int windowWidth = 0;
                    int windowHeight = 0;
                    int outputWidth = 0;
                    int outputHeight = 0;
                    SDL_GetWindowSize(window_, &windowWidth, &windowHeight);
                    if (SDL_GetRenderOutputSize(renderer_, &outputWidth, &outputHeight) && windowWidth > 0 && windowHeight > 0)
                    {
						// マウスカーソルの位置を基準にズームイン・ズームアウトする
                        const float scaleRatio = zoom_ / previousZoom;
                        const float pointerX = event.wheel.mouse_x * static_cast<float>(outputWidth) / static_cast<float>(windowWidth);
                        const float pointerY = event.wheel.mouse_y * static_cast<float>(outputHeight) / static_cast<float>(windowHeight);
                        panX_ = pointerX - static_cast<float>(outputWidth) / 2.0f -  (pointerX - static_cast<float>(outputWidth) / 2.0f - panX_) * scaleRatio;
                        panY_ = pointerY - static_cast<float>(outputHeight) / 2.0f - (pointerY - static_cast<float>(outputHeight) / 2.0f - panY_) * scaleRatio;
                    }
                }
                needsRendering = true;
                break;
            }
            // ウィンドウの表示内容が変更されたときに発生するイベントです。再描画が必要であることを示します。
            case SDL_EVENT_WINDOW_EXPOSED:
            case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                needsRendering = true;
                break;
			// SDL_EVENT_DROP_FILEは、ファイルがウィンドウにドラッグアンドドロップされたときに発生するイベントです。ドロップされたファイルのパスを取得し、OpenPath()を呼び出してファイルを開きます。
            case SDL_EVENT_DROP_FILE:
                if (event.drop.data != nullptr)
                {
                    OpenPath(ToWide(event.drop.data));
                    needsRendering = true;
                }
                break;
            default:
                break;
            }
        }


        /// <summary>
        /// 市松背景を描画し、SDLの描画内容をウィンドウへ提示します。
        /// </summary>
        void Render()
        {
			// 動画テクスチャを更新する
            videoPlayer_.UpdateTexture();

			// ウィンドウの描画内容をクリアし、市松模様を描画する
            SDL_RenderClear(renderer_);

			// 市松模様をウィンドウ全体に繰り返し描画する
            SDL_RenderTextureTiled(renderer_, checkerTexture_, nullptr, 1.0f, nullptr);
            int outputWidth = 0;
            int outputHeight = 0;
            if (SDL_GetRenderOutputSize(renderer_, &outputWidth, &outputHeight))
            {
                const bool renderVideo = videoPlayer_.IsActive() &&
                    (!imagePlayer_.IsLoaded() || (videoPlayer_.Width() > 0 && videoPlayer_.Height() > 0));
                const int mediaWidth = renderVideo ? videoPlayer_.Width() : imagePlayer_.Width();
                const int mediaHeight = renderVideo ? videoPlayer_.Height() : imagePlayer_.Height();
                if (mediaWidth > 0 && mediaHeight > 0)
                {
                    const float fitScale = std::min(
                        static_cast<float>(outputWidth) / static_cast<float>(mediaWidth),
                        static_cast<float>(outputHeight) / static_cast<float>(mediaHeight));
                    const float baseScale = fitToWindow_ ? fitScale : 1.0f;
                    const float width = static_cast<float>(mediaWidth) * baseScale * zoom_;
                    const float height = static_cast<float>(mediaHeight) * baseScale * zoom_;
                    const SDL_FRect destination{
                        (static_cast<float>(outputWidth) - width) / 2.0f + panX_,
                        (static_cast<float>(outputHeight) - height) / 2.0f + panY_,
                        width,
                        height
                    };
                    if (renderVideo)
                    {
                        videoPlayer_.Render(renderer_, destination);
                    }
                    else if (imagePlayer_.IsLoaded())
                    {
                        imagePlayer_.Render(renderer_, destination);
                    }
                }
            }

			// コンテキストメニューと音量表示を描画する
            RenderContextMenu(outputWidth, outputHeight);

			// 音量表示のフェードアウトを描画する
            RenderVolumeOverlay(outputWidth, outputHeight);

			// SDLの描画内容をウィンドウへ提示する
            SDL_RenderPresent(renderer_);
        }


        /// <summary>
        /// 音量表示の次回更新までの時間を返します。
        /// </summary>
        int MillisecondsUntilVolumeOverlayFrame() const
        {
            if (volumeDisplayValue_ < 0)
            {
                return -1;
            }

			// 音量表示の開始からの経過時間を計算する
            const Uint64 elapsed = SDL_GetTicks() - volumeDisplayStartTime_;
            if (elapsed < VOLUME_DISPLAY_DURATION_MS)
            {
                return static_cast<int>(VOLUME_DISPLAY_DURATION_MS - elapsed);
            }

			// 音量表示のフェードアウトが完了している場合は、-1を返す
            const Uint64 fadeElapsed = elapsed - VOLUME_DISPLAY_DURATION_MS;
            if (fadeElapsed >= VOLUME_FADE_DURATION_MS)
            {
                return 0;
            }

			// フェードアウト中の場合は、次のフレームまでの時間を計算して返す
            return static_cast<int>(std::min<Uint64>(
                VOLUME_FADE_FRAME_INTERVAL_MS,
                VOLUME_FADE_DURATION_MS - fadeElapsed));
        }


        /// <summary>
        /// 音量表示用の日本語テキストをWindows GDIでSDLテクスチャへ描画します。
        /// </summary>
        void CreateVolumeOverlayTexture(int volume)
        {
            constexpr size_t pixelCount = static_cast<size_t>(VOLUME_OVERLAY_WIDTH * VOLUME_OVERLAY_HEIGHT);

			// 既存のテクスチャがあれば破棄する
            if (volumeOverlayTexture_ != nullptr)
            {
                SDL_DestroyTexture(volumeOverlayTexture_);
                volumeOverlayTexture_ = nullptr;
            }

			// GDIで描画したピクセルを格納するバッファを作成する
            std::vector<Uint8> bitmapPixels(pixelCount * 4, 0);
            BITMAPINFO bitmapInfo{};
            bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bitmapInfo.bmiHeader.biWidth = VOLUME_OVERLAY_WIDTH;
            bitmapInfo.bmiHeader.biHeight = -VOLUME_OVERLAY_HEIGHT;
            bitmapInfo.bmiHeader.biPlanes = 1;
            bitmapInfo.bmiHeader.biBitCount = 32;
            bitmapInfo.bmiHeader.biCompression = BI_RGB;

			// GDIのデバイスコンテキストを作成し、DIBセクションを作成して描画する
            HDC deviceContext = CreateCompatibleDC(nullptr);
            if (deviceContext == nullptr)
            {
                return;
            }

			// DIBセクションを作成し、ピクセルデータへのポインタを取得する
            void* dibPixels = nullptr;
            HBITMAP bitmap = CreateDIBSection(deviceContext, &bitmapInfo, DIB_RGB_COLORS, &dibPixels, nullptr, 0);
            if (bitmap == nullptr || dibPixels == nullptr)
            {
                if (bitmap != nullptr)
                {
                    DeleteObject(bitmap);
                }
                DeleteDC(deviceContext);
                return;
            }

			// DIBセクションをデバイスコンテキストに選択し、フォントを作成して描画する
            HGDIOBJ originalBitmap = SelectObject(deviceContext, bitmap);
            HFONT font = CreateFontW(
                -32, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                DEFAULT_PITCH | FF_DONTCARE, L"Yu Gothic UI");
            HGDIOBJ originalFont = font != nullptr ? SelectObject(deviceContext, font) : nullptr;
            SetBkMode(deviceContext, TRANSPARENT);
            SetTextColor(deviceContext, RGB(255, 255, 255));

			// 音量表示のテキストを描画する。アウトラインを描画するために、8方向にオフセットして描画する
            const std::wstring label = L"音量" + std::to_wstring(volume) + L"%";
            constexpr std::array<POINT, 8> outlineOffsets{
                POINT{ -2, 0 }, POINT{ 2, 0 }, POINT{ 0, -2 }, POINT{ 0, 2 },
                POINT{ -1, -1 }, POINT{ 1, -1 }, POINT{ -1, 1 }, POINT{ 1, 1 }
            };
            const auto drawTextMask = [&](bool drawOutline)
            {
                std::fill_n(static_cast<Uint32*>(dibPixels), pixelCount, 0u);
                const int offsetCount = drawOutline ? static_cast<int>(outlineOffsets.size()) : 1;
                for (int index = 0; index < offsetCount; ++index)
                {
                    const POINT offset = drawOutline ? outlineOffsets[static_cast<size_t>(index)] : POINT{};
                    RECT textRectangle{ 8 + offset.x, offset.y, VOLUME_OVERLAY_WIDTH - 8 + offset.x, VOLUME_OVERLAY_HEIGHT + offset.y };
                    DrawTextW(deviceContext, label.c_str(), static_cast<int>(label.size()), &textRectangle, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                }

                const auto* sourcePixels = static_cast<const Uint8*>(dibPixels);
                std::vector<Uint8> mask(pixelCount);
                for (size_t index = 0; index < pixelCount; ++index)
                {
                    const size_t pixelOffset = index * 4;
                    mask[index] = std::max({ sourcePixels[pixelOffset], sourcePixels[pixelOffset + 1], sourcePixels[pixelOffset + 2] });
                }
                return mask;
            };

			// アウトラインと塗りつぶしのマスクを作成し、アルファ値を計算してピクセルデータを更新する
            const std::vector<Uint8> outlineMask = drawTextMask(true);
            const std::vector<Uint8> fillMask = drawTextMask(false);
            std::vector<Uint8> alphaMask(pixelCount);
            for (size_t index = 0; index < pixelCount; ++index)
            {
                const size_t pixelOffset = index * 4;
                const unsigned fill = fillMask[index];
                const unsigned outline = outlineMask[index];
                const unsigned alpha = fill + (outline * (255 - fill) + 127) / 255;
                alphaMask[index] = static_cast<Uint8>(alpha);
                const Uint8 color = alpha > 0 ? static_cast<Uint8>((fill * 255 + alpha / 2) / alpha) : 0;
                bitmapPixels[pixelOffset] = color;
                bitmapPixels[pixelOffset + 1] = color;
                bitmapPixels[pixelOffset + 2] = color;
                bitmapPixels[pixelOffset + 3] = static_cast<Uint8>(alpha);
            }

			// GDIリソースを解放する
            if (originalFont != nullptr)
            {
                SelectObject(deviceContext, originalFont);
            }
            if (font != nullptr)
            {
                DeleteObject(font);
            }
            SelectObject(deviceContext, originalBitmap);
            DeleteObject(bitmap);
            DeleteDC(deviceContext);

			// SDLテクスチャを作成し、ピクセルデータを転送する
            SDL_Texture* texture = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, VOLUME_OVERLAY_WIDTH, VOLUME_OVERLAY_HEIGHT);
            if (texture == nullptr
             || !SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND)
             || !SDL_UpdateTexture(texture, nullptr, bitmapPixels.data(), VOLUME_OVERLAY_WIDTH * 4)
            ) {
                if (texture != nullptr)
                {
                    SDL_DestroyTexture(texture);
                }
                return;
            }

            volumeOverlayTexture_ = texture;
            volumeOverlayPixels_ = std::move(bitmapPixels);
            volumeOverlayAlphaMask_ = std::move(alphaMask);
        }


        /// <summary>
        /// 音量表示を右上に描画し、1秒経過後にフェードアウトします。
        /// </summary>
        void RenderVolumeOverlay(int outputWidth, int outputHeight)
        {
            if (volumeDisplayValue_ < 0)
            {
                return;
            }

			// 音量表示の経過時間を計算し、フェードアウトの進行度を決定する
            const Uint64 elapsed = SDL_GetTicks() - volumeDisplayStartTime_;
            const Uint64 totalDuration = VOLUME_DISPLAY_DURATION_MS + VOLUME_FADE_DURATION_MS;
            if (elapsed >= totalDuration)
            {
                volumeDisplayValue_ = -1;
                return;
            }
            if (volumeOverlayTexture_ == nullptr || outputWidth <= 0 || outputHeight <= 0)
            {
                return;
            }

			// フェードアウトの進行度に応じてアルファ値を計算し、ピクセルデータを更新する
            float opacity = 1.0f;
            if (elapsed > VOLUME_DISPLAY_DURATION_MS)
            {
                opacity = 1.0f - static_cast<float>(elapsed - VOLUME_DISPLAY_DURATION_MS) / static_cast<float>(VOLUME_FADE_DURATION_MS);
            }
            for (size_t index = 0; index < volumeOverlayAlphaMask_.size(); ++index)
            {
                volumeOverlayPixels_[index * 4 + 3] = static_cast<Uint8>(volumeOverlayAlphaMask_[index] * opacity);
            }
            SDL_UpdateTexture(volumeOverlayTexture_, nullptr, volumeOverlayPixels_.data(), VOLUME_OVERLAY_WIDTH * 4);

			// 音量表示テクスチャを右上に描画する
            const SDL_FRect destination{
                std::max(0.0f, static_cast<float>(outputWidth) - VOLUME_OVERLAY_WIDTH - VOLUME_OVERLAY_MARGIN),
                static_cast<float>(VOLUME_OVERLAY_MARGIN),
                static_cast<float>(VOLUME_OVERLAY_WIDTH),
                static_cast<float>(VOLUME_OVERLAY_HEIGHT)
            };
            SDL_RenderTexture(renderer_, volumeOverlayTexture_, nullptr, &destination);
        }


        /// <summary>
        /// 音量表示テクスチャと表示状態を解放します。
        /// </summary>
        void HideVolumeOverlay()
        {
            volumeDisplayValue_ = -1;
            volumeOverlayPixels_.clear();
            volumeOverlayAlphaMask_.clear();
            if (volumeOverlayTexture_ != nullptr)
            {
                SDL_DestroyTexture(volumeOverlayTexture_);
                volumeOverlayTexture_ = nullptr;
            }
        }


        /// <summary>
        /// 右クリックメニューを背景画像の上へ描画します。
        /// </summary>
        void RenderContextMenu(int outputWidth, int outputHeight)
        {
            if (!contextMenuOpen_)
            {
                return;
            }

			// コンテキストメニューの位置とサイズをウィンドウの描画サイズに合わせてスケーリングする
            int windowWidth = 0;
            int windowHeight = 0;
            SDL_GetWindowSize(window_, &windowWidth, &windowHeight);
            const float scaleX = windowWidth > 0 ? static_cast<float>(outputWidth) / windowWidth : 1.0f;
            const float scaleY = windowHeight > 0 ? static_cast<float>(outputHeight) / windowHeight : 1.0f;
            const SDL_FRect panel{
                contextMenuX_ * scaleX,
                contextMenuY_ * scaleY,
                CONTEXT_MENU_WIDTH * scaleX,
                CONTEXT_MENU_ROW_HEIGHT * 4.0f * scaleY
            };

			// コンテキストメニューの背景を半透明で描画する
            SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer_, 34, 34, 34, 238);
            SDL_RenderFillRect(renderer_, &panel);
            SDL_SetRenderDrawColor(renderer_, 240, 240, 240, 255);

			// コンテキストメニューの項目を描画する
            constexpr std::array<const char*, 4> MENU_ITEMS{
                "Fit to window", "Original size", "Play / pause", "Exit"
            };
            for (size_t index = 0; index < MENU_ITEMS.size(); ++index)
            {
                const float textX = panel.x + 10.0f * scaleX;
                const float textY = panel.y + (static_cast<float>(index) * CONTEXT_MENU_ROW_HEIGHT + 10.0f) * scaleY;
                SDL_RenderDebugText(renderer_, textX, textY, MENU_ITEMS[index]);
            }
        }


        /// <summary>
        /// 数字列またはテンキーの動画操作キーから操作番号を取得します。
        /// </summary>
        static int GetVideoControlKeyIndex(SDL_Scancode scancode)
        {
            switch (scancode)
            {
            case SDL_SCANCODE_4:
            case SDL_SCANCODE_KP_4:
                return 0;
            case SDL_SCANCODE_6:
            case SDL_SCANCODE_KP_6:
                return 1;
            case SDL_SCANCODE_8:
            case SDL_SCANCODE_KP_8:
                return 2;
            case SDL_SCANCODE_2:
            case SDL_SCANCODE_KP_2:
                return 3;
            default:
                return -1;
            }
        }


        /// <summary>
        /// 音量変更後の描画
        /// </summary>
        void ShowVolumeOverlay(int volume, bool& needsRendering)
        {
            volumeDisplayValue_ = volume;
            volumeDisplayStartTime_ = SDL_GetTicks();
            CreateVolumeOverlayTexture(volume);
            needsRendering = true;
        }


        /// <summary>
		/// 動画操作キーに対応する操作を実行します。
        /// </summary>
        void PerformVideoControlAction(int actionIndex, bool& needsRendering)
        {
            switch (actionIndex)
            {
				// Key4: 10秒巻き戻し
                case 0:
                    videoPlayer_.SeekBySeconds(-VIDEO_SEEK_SECONDS);
                    break;
				// Key6: 10秒早送り
                case 1:
                    videoPlayer_.SeekBySeconds(VIDEO_SEEK_SECONDS);
                    break;
				// Key8: 音量アップ
                case 2:
                {
                    const int volume = videoPlayer_.AdjustVolume(VIDEO_VOLUME_STEP);
                    if (volume >= 0)
                    {
                        ShowVolumeOverlay(volume, needsRendering);
                    }
                    break;
                }
				// Key2: 音量ダウン
                case 3:
                {
                    const int volume = videoPlayer_.AdjustVolume(-VIDEO_VOLUME_STEP);
                    if (volume >= 0)
                    {
                        ShowVolumeOverlay(volume, needsRendering);
                    }
                    break;
                }
                default:
                    break;
            }
        }


        /// <summary>
        /// 動画操作キーの初回押下を処理し、連続操作開始時刻を記録します。
        /// </summary>
        bool HandleVideoControlKeyDown(const SDL_KeyboardEvent& key, bool& needsRendering)
        {
            const int actionIndex = GetVideoControlKeyIndex(key.scancode);
            if (actionIndex < 0)
            {
                return false;
            }

            const size_t index = static_cast<size_t>(actionIndex);
            if (!key.repeat && !videoControlKeysHeld_[index])
            {
                videoControlKeysHeld_[index] = true;
                videoControlKeyDownTimes_[index] = SDL_GetTicks();
                PerformVideoControlAction(actionIndex, needsRendering);
            }
            else if (key.repeat && videoControlKeysHeld_[index]
             && SDL_GetTicks() - videoControlKeyDownTimes_[index] >= VIDEO_KEY_REPEAT_DELAY_MS)
            {
                PerformVideoControlAction(actionIndex, needsRendering);
            }

            return true;
        }


        /// <summary>
        /// 動画操作キーの押下状態を解除します。
        /// </summary>
        void HandleVideoControlKeyUp(SDL_Scancode scancode)
        {
            const int actionIndex = GetVideoControlKeyIndex(scancode);
            if (actionIndex >= 0)
            {
                videoControlKeysHeld_[static_cast<size_t>(actionIndex)] = false;
            }
        }


        /// <summary>
        /// フォーカス喪失や動画切替時に動画操作キーの押下状態を解除します。
        /// </summary>
        void ResetVideoControlKeys()
        {
            videoControlKeysHeld_.fill(false);
        }


        /// <summary>
        /// 右クリックメニュー項目を選択し、表示状態や再生状態を更新します。
        /// </summary>
        void HandleContextMenuClick(float x, float y, bool& isRunning)
        {
            const bool inside = x >= contextMenuX_ && x < contextMenuX_ + CONTEXT_MENU_WIDTH &&
                y >= contextMenuY_ && y < contextMenuY_ + CONTEXT_MENU_ROW_HEIGHT * 4.0f;
            contextMenuOpen_ = false;
            if (!inside)
            {
                return;
            }

            const int selectedItem = static_cast<int>((y - contextMenuY_) / CONTEXT_MENU_ROW_HEIGHT);
            switch (selectedItem)
            {
            case 0:
                fitToWindow_ = true;
                zoom_ = 1.0f;
                panX_ = 0.0f;
                panY_ = 0.0f;
                break;
            case 1:
                fitToWindow_ = false;
                zoom_ = 1.0f;
                panX_ = 0.0f;
                panY_ = 0.0f;
                break;
            case 2:
                if (imagePlayer_.IsAnimated())
                {
                    imagePlayer_.TogglePause();
                }
                else
                {
                    videoPlayer_.TogglePause();
                }
                break;
            case 3:
                isRunning = false;
                break;
            default:
                break;
            }
        }


        /// <summary>
        /// 一覧を前後へ循環し、選択ファイルを開き直します。
        /// </summary>
        void OpenRelativeFile(int direction)
        {
            const std::optional<std::wstring> path = fileManager_.GetRelativeFile(direction);
            if (!path.has_value())
            {
                return;
            }

            OpenPath(*path);
        }


        /// <summary>
        /// 対応する動画ファイルを開き、SDLウィンドウ表示後にLibVLCを遅延初期化します。
        /// </summary>
        void OpenPath(const std::wstring& path)
        {
            std::error_code fileError;
            if (path.empty() || !std::filesystem::is_regular_file(path, fileError) || fileError)
            {
                return;
            }

            fileManager_.RefreshFolderFiles(path);

            std::error_code absoluteError;
            const std::filesystem::path absolutePath = std::filesystem::absolute(path, absoluteError).lexically_normal();
            currentFilePath_ = absoluteError ? path : absolutePath.wstring();
            fileManager_.SetCurrentFilePath(currentFilePath_);
            UpdateWindowTitle();
            HideVolumeOverlay();
            videoPlayer_.Stop();
            ResetVideoControlKeys();
            isMotionPhotoVideoPlaying_ = false;
            CleanupMotionPhotoTempFile();
            imagePlayer_.Clear();
            fitToWindow_ = true;
            zoom_ = 1.0f;
            panX_ = 0.0f;
            panY_ = 0.0f;

            std::wstring extension = std::filesystem::path(path).extension().wstring();
            std::transform(extension.begin(), extension.end(), extension.begin(), towlower);
            if (std::find(VIDEO_EXTENSIONS.begin(), VIDEO_EXTENSIONS.end(), extension) != VIDEO_EXTENSIONS.end())
            {
                if (!videoPlayer_.Play(renderer_, videoFrameEventType_, videoPlaybackEndedEventType_, path, LOOP_VIDEO_PLAYBACK))
                {
                    MessageBoxW(nullptr,
                        L"動画を開けませんでした。LibVLCの初期化またはメディアの読込に失敗しました。",
                        L"SimpleMediaViewer", MB_OK | MB_ICONERROR);
                }
            }
            else if (std::find(IMAGE_EXTENSIONS.begin(), IMAGE_EXTENSIONS.end(), extension) != IMAGE_EXTENSIONS.end())
            {
                if (!imagePlayer_.Load(renderer_, path))
                {
                    MessageBoxW(nullptr, L"画像を開けませんでした。", L"SimpleMediaViewer", MB_OK | MB_ICONERROR);
                }
                else if (extension == L".jpg" || extension == L".jpeg")
                {
                    TryPlayMotionPhoto(path);
                }
            }
        }


        /// <summary>
        /// Motion Photoの動画部分を抽出して一度だけ再生します。
        /// </summary>
        bool TryPlayMotionPhoto(const std::wstring& path)
        {
			// Motion Photoの動画部分のオフセットを取得する
            const std::optional<std::uint64_t> videoOffset = MotionPhotoHelper::FindEmbeddedVideoOffset(path);
            if (!videoOffset.has_value())
            {
                return false;
            }

			// Motion Photoの動画部分を一時ファイルとして抽出する
            const std::optional<std::filesystem::path> extractedPath = MotionPhotoHelper::ExtractEmbeddedVideo(path, *videoOffset);
            if (!extractedPath.has_value())
            {
                return false;
            }

			// Motion Photoの動画部分を再生し、再生終了後に一時ファイルを削除する
            if (!videoPlayer_.Play(renderer_, videoFrameEventType_, videoPlaybackEndedEventType_, extractedPath->wstring(), false))
            {
                std::error_code removeError;
                std::filesystem::remove(*extractedPath, removeError);
                return false;
            }

			// 一時ファイルのパスを保持し、再生中フラグを設定する
            motionPhotoTempPath_ = *extractedPath;
            isMotionPhotoVideoPlaying_ = true;
            return true;
        }


        /// <summary>
        /// Motion Photoから抽出した一時動画を削除します。
        /// </summary>
        void CleanupMotionPhotoTempFile()
        {
            if (motionPhotoTempPath_.empty())
            {
                return;
            }

            std::error_code removeError;
            std::filesystem::remove(motionPhotoTempPath_, removeError);
            motionPhotoTempPath_.clear();
        }


        /// <summary>
        /// 現在のファイル名をウィンドウタイトルに反映します。
        /// </summary>
        void UpdateWindowTitle()
        {
            const std::wstring title = currentFilePath_.empty()
                ? L"SimpleMediaViewer"
                : std::filesystem::path(currentFilePath_).filename().wstring();
            const std::string utf8Title = ToUtf8(title);
            SDL_SetWindowTitle(window_, utf8Title.c_str());
        }


        /// <summary>
        /// SDLエラーをWindowsのメッセージボックスで通知します。
        /// </summary>
        void ShowSdlError(const wchar_t* message)
        {
            const std::string error = SDL_GetError();
            const std::wstring fullMessage = std::wstring(message) + L"\n" + ToWide(error.c_str());
            MessageBoxW(nullptr, fullMessage.c_str(), L"SimpleMediaViewer", MB_OK | MB_ICONERROR);
        }

		// SDLウィンドウを保持します。描画操作を行う際に使用されます。
        SDL_Window* window_ = nullptr;
		// SDLレンダラーを保持します。描画操作を行う際に使用されます。
        SDL_Renderer* renderer_ = nullptr;
		// 市松模様のSDLテクスチャを保持します。ウィンドウの背景として描画されます。
        SDL_Texture* checkerTexture_ = nullptr;
		// 音量表示用のSDLテクスチャを保持します。音量変更時に日本語テキストを描画するために使用されます。
        SDL_Texture* volumeOverlayTexture_ = nullptr;
		// SDLイベントの種類を保持します。LibVLCの動画再生中に発生するフレーム更新イベントを処理するために使用されます。
        Uint32 videoFrameEventType_ = 0;
		// 動画再生終了イベントの種類を保持します。LibVLCの動画再生が終了した際に発生するイベントを処理するために使用されます。
        Uint32 videoPlaybackEndedEventType_ = 0;
		// VideoPlayerクラスのインスタンスを保持します。動画ファイルを読み込み、再生する際に使用されます。
        VideoPlayer videoPlayer_;
		// ImagePlayerクラスのインスタンスを保持します。画像ファイルを読み込み、表示する際に使用されます。
        ImagePlayer imagePlayer_;
		// FileManagerクラスのインスタンスを保持します。現在のフォルダ内のファイル一覧を管理し、前後のファイルを開く際に使用されます。
        FileManager fileManager_;
		// Motion Photoの動画部分を一時的に保存するパスを保持します。再生終了後に削除されます。
        std::filesystem::path motionPhotoTempPath_;
		// ウィンドウの表示モードを保持します。trueの場合はウィンドウサイズに合わせて表示し、falseの場合は元のサイズで表示します。
        bool fitToWindow_ = true;
		// ズーム操作中の倍率を保持します。マウスホイールでズーム操作を行う際に使用されます。
        float zoom_ = 1.0f;
		// ズーム操作中のX座標のオフセットを保持します。マウスホイールでズーム操作を行う際に使用されます。
        float panX_ = 0.0f;
		// パン操作中のX座標のオフセットを保持します。左クリックでパン操作を開始する際に使用されます。
        float panY_ = 0.0f;
		// パン操作中かどうかを保持します。左クリックでパン操作を開始する際に使用されます。
        bool isPanning_ = false;
		// コンテキストメニューが開いているかどうかを保持します。右クリックでコンテキストメニューを表示する際に使用されます。
        bool contextMenuOpen_ = false;
		// Motion Photoの動画部分が再生中かどうかを保持します。再生終了後に一時ファイルを削除するために使用されます。
        bool isMotionPhotoVideoPlaying_ = false;
		// 音量表示の値を保持します。-1の場合は表示しません。
        int volumeDisplayValue_ = -1;
		// 音量表示の開始時刻を保持します。フェードアウトの進行度を計算するために使用されます。
        Uint64 volumeDisplayStartTime_ = 0;
		// 音量表示テクスチャのピクセルデータを保持します。フェードアウト時に使用されます。
        std::vector<Uint8> volumeOverlayPixels_;
		// 音量表示テクスチャのアルファ値を保持するマスクを作成します。フェードアウト時に使用されます。
        std::vector<Uint8> volumeOverlayAlphaMask_;
		// 動画操作キーの押下状態を保持します。連続操作の判定に使用されます。
        std::array<bool, 4> videoControlKeysHeld_{};
		// 動画操作キーの押下時刻を保持します。連続操作の開始判定に使用されます。
        std::array<Uint64, 4> videoControlKeyDownTimes_{};
		// コンテキストメニューのX座標を保持します。右クリックでコンテキストメニューを表示する際に使用されます。
        float contextMenuX_ = 0.0f;
		// コンテキストメニューのY座標を保持します。右クリックでコンテキストメニューを表示する際に使用されます。
        float contextMenuY_ = 0.0f;
		// 現在開いているファイルの絶対パスを保持します。ウィンドウタイトルの更新や、前後のファイルを開く際に使用されます。
        std::wstring currentFilePath_;
    };
}


/// <summary>
/// SDL3を初期化し、単体メディアプレイヤーのイベントループを開始します。
/// </summary>
int WINAPI wWinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ PWSTR, _In_ int)
{
    SDL_SetMainReady();

    SdlApplication application;
    if (!application.Initialize(GetStartupFilePath()))
    {
        application.Shutdown();
        return 1;
    }

    application.Run();
    application.Shutdown();
    return 0;
}
