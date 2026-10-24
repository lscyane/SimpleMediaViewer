#define SDL_MAIN_HANDLED

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <Windows.h>
#include <shellapi.h>

#include <array>
#include <filesystem>
#include <string>
#include <vector>

#pragma comment(lib, "Shell32.lib")

namespace
{
    // ウィンドウの初期サイズと市松模様の見た目をここでまとめて調整します。
    constexpr int WINDOW_INITIAL_WIDTH = 960;
    constexpr int WINDOW_INITIAL_HEIGHT = 640;
    constexpr int CHECKER_CELL_SIZE = 32;
    constexpr SDL_Color CHECKER_LIGHT_COLOR{ 112, 112, 112, 255 };
    constexpr SDL_Color CHECKER_DARK_COLOR{ 72, 72, 72, 255 };

    /// <summary>WindowsのUTF-16文字列をSDL API用のUTF-8文字列へ変換します。</summary>
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

    /// <summary>SDLイベントのUTF-8パスをWindows API用のUTF-16文字列へ変換します。</summary>
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

    /// <summary>起動引数から最初のファイルパスを取り出します。</summary>
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

    /// <summary>SDL3のウィンドウ、描画ループ、市松背景を所有するアプリケーションです。</summary>
    class SdlApplication final
    {
    public:
        /// <summary>SDLのウィンドウと描画リソースを初期化します。</summary>
        bool Initialize(const std::wstring& initialFilePath)
        {
            if (!SDL_Init(SDL_INIT_VIDEO))
            {
                ShowSdlError(L"SDLの初期化に失敗しました");
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
            return true;
        }

        /// <summary>イベントを待機し、必要な場合だけ画面を再描画します。</summary>
        void Run()
        {
            bool isRunning = true;
            bool needsRendering = true;

            while (isRunning)
            {
                SDL_Event event{};
                if (!needsRendering)
                {
                    if (!SDL_WaitEvent(&event))
                    {
                        continue;
                    }
                    HandleEvent(event, isRunning, needsRendering);
                }

                while (SDL_PollEvent(&event))
                {
                    HandleEvent(event, isRunning, needsRendering);
                }

                if (needsRendering && isRunning)
                {
                    Render();
                    needsRendering = false;
                }
            }
        }

        /// <summary>SDLリソースを破棄し、SDLを終了します。</summary>
        void Shutdown()
        {
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
        /// <summary>市松背景を一枚の小さな反復テクスチャとして作成します。</summary>
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

            SDL_Texture* texture = SDL_CreateTexture(
                renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, textureSize, textureSize);
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

        /// <summary>SDLイベントを処理し、終了・再描画・ドロップされたファイルを反映します。</summary>
        void HandleEvent(const SDL_Event& event, bool& isRunning, bool& needsRendering)
        {
            switch (event.type)
            {
            case SDL_EVENT_QUIT:
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                isRunning = false;
                break;
            case SDL_EVENT_KEY_DOWN:
                if (event.key.key == SDLK_ESCAPE)
                {
                    isRunning = false;
                }
                break;
            case SDL_EVENT_WINDOW_EXPOSED:
            case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                needsRendering = true;
                break;
            case SDL_EVENT_DROP_FILE:
                if (event.drop.data != nullptr)
                {
                    currentFilePath_ = ToWide(event.drop.data);
                    UpdateWindowTitle();
                    needsRendering = true;
                    SDL_free(const_cast<char*>(event.drop.data));
                }
                break;
            default:
                break;
            }
        }

        /// <summary>市松背景を描画し、SDLの描画内容をウィンドウへ提示します。</summary>
        void Render()
        {
            SDL_RenderClear(renderer_);
            SDL_RenderTextureTiled(renderer_, checkerTexture_, nullptr, 1.0f, nullptr);
            SDL_RenderPresent(renderer_);
        }

        /// <summary>現在のファイル名をウィンドウタイトルに反映します。</summary>
        void UpdateWindowTitle()
        {
            const std::wstring title = currentFilePath_.empty()
                ? L"SimpleMediaViewer"
                : std::filesystem::path(currentFilePath_).filename().wstring();
            const std::string utf8Title = ToUtf8(title);
            SDL_SetWindowTitle(window_, utf8Title.c_str());
        }

        /// <summary>SDLエラーをWindowsのメッセージボックスで通知します。</summary>
        void ShowSdlError(const wchar_t* message)
        {
            const std::string error = SDL_GetError();
            const std::wstring fullMessage = std::wstring(message) + L"\n" + ToWide(error.c_str());
            MessageBoxW(nullptr, fullMessage.c_str(), L"SimpleMediaViewer", MB_OK | MB_ICONERROR);
        }

        SDL_Window* window_ = nullptr;
        SDL_Renderer* renderer_ = nullptr;
        SDL_Texture* checkerTexture_ = nullptr;
        std::wstring currentFilePath_;
    };
}

/// <summary>SDL3を初期化し、単体メディアプレイヤーのイベントループを開始します。</summary>
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
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
