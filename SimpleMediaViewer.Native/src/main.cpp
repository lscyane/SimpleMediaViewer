#define SDL_MAIN_HANDLED
#define NOMINMAX

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include "ImagePlayer.h"
#include "VideoPlayer.h"
#include <Windows.h>
#include <shellapi.h>
#include <shlwapi.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cwctype>
#include <filesystem>
#include <future>
#include <optional>
#include <string>
#include <vector>

#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Shlwapi.lib")

namespace
{
    // ウィンドウの初期サイズと市松模様の見た目をここでまとめて調整します。
    constexpr int WINDOW_INITIAL_WIDTH = 960;
    constexpr int WINDOW_INITIAL_HEIGHT = 640;
    constexpr int CHECKER_CELL_SIZE = 32;
    constexpr SDL_Color CHECKER_LIGHT_COLOR{ 112, 112, 112, 255 };
    constexpr SDL_Color CHECKER_DARK_COLOR{ 72, 72, 72, 255 };
    constexpr float CONTEXT_MENU_WIDTH = 196.0f;
    constexpr float CONTEXT_MENU_ROW_HEIGHT = 30.0f;


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

            videoFrameEventType_ = SDL_RegisterEvents(1);
            if (videoFrameEventType_ == static_cast<Uint32>(-1))
            {
                ShowSdlError(L"動画フレームイベントの登録に失敗しました");
                return false;
            }

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
                ProcessFolderScan();
                SDL_Event event{};
                if (!needsRendering)
                {
                    const bool receivedEvent = folderScanFuture_.valid()
                        ? SDL_WaitEventTimeout(&event, 20)
                        : SDL_WaitEvent(&event);
                    if (!receivedEvent)
                    {
                        continue;
                    }
                    HandleEvent(event, isRunning, needsRendering);
                }

                while (SDL_PollEvent(&event))
                {
                    HandleEvent(event, isRunning, needsRendering);
                }

                ProcessFolderScan();
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
            imagePlayer_.Clear();
            if (checkerTexture_ != nullptr)
            {
                SDL_DestroyTexture(checkerTexture_);
                checkerTexture_ = nullptr;
            }
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
        struct FolderScanResult
        {
            std::wstring directory;
            std::vector<std::wstring> files;
        };


        static std::vector<std::wstring> EnumerateFolderFiles(const std::wstring& directory)
        {
            static const std::array<std::wstring, 18> SUPPORTED_EXTENSIONS{
                L".jpg", L".jpeg", L".jpe", L".png", L".gif", L".bmp", L".dib", L".webp", L".tif", L".tiff",
                L".webm", L".mp4", L".avi", L".mkv", L".mov", L".flv", L".wmv", L".mpeg"
            };

            std::vector<std::wstring> files;
            std::error_code error;
            for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(directory, error))
            {
                if (error)
                {
                    break;
                }
                if (!entry.is_regular_file(error))
                {
                    error.clear();
                    continue;
                }

                std::wstring extension = entry.path().extension().wstring();
                std::transform(extension.begin(), extension.end(), extension.begin(), towlower);
                if (std::find(SUPPORTED_EXTENSIONS.begin(), SUPPORTED_EXTENSIONS.end(), extension) != SUPPORTED_EXTENSIONS.end())
                {
                    files.push_back(entry.path().lexically_normal().wstring());
                }
                error.clear();
            }

            std::sort(files.begin(), files.end(), [](const std::wstring& left, const std::wstring& right)
            {
                const std::wstring leftName = std::filesystem::path(left).filename().wstring();
                const std::wstring rightName = std::filesystem::path(right).filename().wstring();
                return StrCmpLogicalW(leftName.c_str(), rightName.c_str()) < 0;
            });
            return files;
        }


        void StartFolderScan(const std::wstring& directory)
        {
            folderScanFuture_ = std::async(std::launch::async, [directory]
            {
                return FolderScanResult{ directory, EnumerateFolderFiles(directory) };
            });
        }


        void ProcessFolderScan()
        {
            if (!folderScanFuture_.valid() ||
                folderScanFuture_.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
            {
                return;
            }

            FolderScanResult result = folderScanFuture_.get();
            if (result.directory == currentDirectory_)
            {
                fileList_ = std::move(result.files);
                currentFileIndex_ = 0;
                for (size_t index = 0; index < fileList_.size(); ++index)
                {
                    if (CompareStringOrdinal(fileList_[index].c_str(), -1, currentFilePath_.c_str(), -1, TRUE) == CSTR_EQUAL)
                    {
                        currentFileIndex_ = index;
                        break;
                    }
                }
            }

            if (pendingFolderDirectory_.has_value())
            {
                std::wstring directory = std::move(*pendingFolderDirectory_);
                pendingFolderDirectory_.reset();
                if (directory != result.directory)
                {
                    StartFolderScan(directory);
                }
            }
        }


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

            switch (event.type)
            {
            case SDL_EVENT_QUIT:
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                isRunning = false;
                break;
            case SDL_EVENT_KEY_DOWN:
                if (!event.key.repeat && event.key.key == SDLK_ESCAPE)
                {
                    isRunning = false;
                }
                else if (!event.key.repeat && event.key.key == SDLK_SPACE)
                {
                    videoPlayer_.TogglePause();
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
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (event.button.button == SDL_BUTTON_LEFT)
                {
                    isPanning_ = false;
                }
                break;
            case SDL_EVENT_MOUSE_MOTION:
                if (isPanning_)
                {
                    panX_ += event.motion.xrel;
                    panY_ += event.motion.yrel;
                    needsRendering = true;
                }
                break;
            case SDL_EVENT_MOUSE_WHEEL:
            {
                const float direction = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f;
                zoom_ = std::clamp(zoom_ * std::pow(1.15f, event.wheel.y * direction), 0.05f, 32.0f);
                needsRendering = true;
                break;
            }
            case SDL_EVENT_WINDOW_EXPOSED:
            case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                needsRendering = true;
                break;
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
            videoPlayer_.UpdateTexture();
            SDL_RenderClear(renderer_);
            SDL_RenderTextureTiled(renderer_, checkerTexture_, nullptr, 1.0f, nullptr);
            int outputWidth = 0;
            int outputHeight = 0;
            if (SDL_GetRenderOutputSize(renderer_, &outputWidth, &outputHeight))
            {
                const int mediaWidth = imagePlayer_.IsLoaded() ? imagePlayer_.Width() : videoPlayer_.Width();
                const int mediaHeight = imagePlayer_.IsLoaded() ? imagePlayer_.Height() : videoPlayer_.Height();
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
                    if (imagePlayer_.IsLoaded())
                    {
                        imagePlayer_.Render(renderer_, destination);
                    }
                    else
                    {
                        videoPlayer_.Render(renderer_, destination);
                    }
                }
            }
            RenderContextMenu(outputWidth, outputHeight);
            SDL_RenderPresent(renderer_);
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

            SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer_, 34, 34, 34, 238);
            SDL_RenderFillRect(renderer_, &panel);
            SDL_SetRenderDrawColor(renderer_, 240, 240, 240, 255);

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
                videoPlayer_.TogglePause();
                break;
            case 3:
                isRunning = false;
                break;
            default:
                break;
            }
        }


        /// <summary>
        /// WPF版と同じ拡張子集合から対応メディアを同一フォルダー内で収集します。
        /// </summary>
        void RefreshFolderFiles(const std::wstring& path)
        {
            std::error_code error;
            const std::filesystem::path absolutePath = std::filesystem::absolute(path, error).lexically_normal();
            const std::filesystem::path sourcePath = error ? std::filesystem::path(path) : absolutePath;
            const std::wstring directory = sourcePath.parent_path().wstring();
            if (directory == currentDirectory_)
            {
                return;
            }

            currentDirectory_ = directory;
            fileList_.clear();
            currentFileIndex_ = 0;
            if (folderScanFuture_.valid())
            {
                pendingFolderDirectory_ = directory;
                return;
            }

            StartFolderScan(directory);
        }


        /// <summary>
        /// 一覧を前後へ循環し、選択ファイルを開き直します。
        /// </summary>
        void OpenRelativeFile(int direction)
        {
            if (fileList_.empty())
            {
                return;
            }

            const int count = static_cast<int>(fileList_.size());
            const int nextIndex = (static_cast<int>(currentFileIndex_) + direction + count) % count;
            OpenPath(fileList_[static_cast<size_t>(nextIndex)]);
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

			// 同じフォルダー内の対応メディアを収集し、現在のファイルインデックスを更新する
            RefreshFolderFiles(path);

            std::error_code absoluteError;
            const std::filesystem::path absolutePath = std::filesystem::absolute(path, absoluteError).lexically_normal();
            currentFilePath_ = absoluteError ? path : absolutePath.wstring();
            currentFileIndex_ = 0;
            for (size_t index = 0; index < fileList_.size(); ++index)
            {
                if (CompareStringOrdinal(fileList_[index].c_str(), -1, currentFilePath_.c_str(), -1, TRUE) == CSTR_EQUAL)
                {
                    currentFileIndex_ = index;
                    break;
                }
            }
            UpdateWindowTitle();
            videoPlayer_.Stop();
            imagePlayer_.Clear();
            fitToWindow_ = true;
            zoom_ = 1.0f;
            panX_ = 0.0f;
            panY_ = 0.0f;

            std::wstring extension = std::filesystem::path(path).extension().wstring();
            std::transform(extension.begin(), extension.end(), extension.begin(), towlower);
            static const std::array<std::wstring, 8> VIDEO_EXTENSIONS{
                L".webm", L".mp4", L".avi", L".mkv", L".mov", L".flv", L".wmv", L".mpeg"
            };
            static const std::array<std::wstring, 10> IMAGE_EXTENSIONS{
                L".jpg", L".jpeg", L".png", L".bmp", L".tif", L".tiff", L".gif", L".webp", L".dib", L".jpe"
            };
            if (std::find(VIDEO_EXTENSIONS.begin(), VIDEO_EXTENSIONS.end(), extension) != VIDEO_EXTENSIONS.end())
            {
                if (!videoPlayer_.Play(renderer_, videoFrameEventType_, path))
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
            }
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


        SDL_Window* window_ = nullptr;
        SDL_Renderer* renderer_ = nullptr;
        SDL_Texture* checkerTexture_ = nullptr;
        Uint32 videoFrameEventType_ = 0;
        VideoPlayer videoPlayer_;
        ImagePlayer imagePlayer_;
        bool fitToWindow_ = true;
        float zoom_ = 1.0f;
        float panX_ = 0.0f;
        float panY_ = 0.0f;
        bool isPanning_ = false;
        bool contextMenuOpen_ = false;
        float contextMenuX_ = 0.0f;
        float contextMenuY_ = 0.0f;
        std::wstring currentFilePath_;
        std::wstring currentDirectory_;
        std::vector<std::wstring> fileList_;
        size_t currentFileIndex_ = 0;
        std::future<FolderScanResult> folderScanFuture_;
        std::optional<std::wstring> pendingFolderDirectory_;
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
