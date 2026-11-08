#include "ContextMenu.h"

#include <Windows.h>

#include <array>


/// <summary>
/// コンテキストメニュー用のコマンドIDと表示文字列の対応表を、名前空間内の定数として定義します。
/// </summary>
namespace
{
	constexpr UINT FIT_TO_WINDOW_COMMAND = 1;
	constexpr UINT ORIGINAL_SIZE_COMMAND = 2;
	constexpr UINT TOGGLE_PLAYBACK_COMMAND = 3;
	constexpr UINT EXIT_COMMAND = 4;

	struct MenuItem
	{
		UINT flags;
		UINT command;
		LPCWSTR label;
	};


	/// <summary>
	/// メニュー項目を保持する定数配列を定義します。
	/// </summary>
	constexpr std::array<MenuItem, 5> MENU_ITEMS{{
		{ MF_STRING, FIT_TO_WINDOW_COMMAND, L"Fit to window" },
		{ MF_STRING, ORIGINAL_SIZE_COMMAND, L"Original size" },
		{ MF_STRING, TOGGLE_PLAYBACK_COMMAND, L"Play / pause" },
		{ MF_SEPARATOR, 0, nullptr },
		{ MF_STRING, EXIT_COMMAND, L"Exit" }
	}};
}


/// <summary>
/// 指定されたウィンドウに対してカーソル位置へコンテキストメニューを表示し、選択されたコマンドを返します。ウィンドウが無効な場合やメニュー表示に失敗した場合は None を返します。
/// </summary>
/// <param name="window">コンテキストメニューの表示対象となる SDL ウィンドウへのポインターです。</param>
/// <returns>ユーザーが選択した ContextMenuCommand を返します。キャンセル時または処理失敗時は ContextMenuCommand::None です。</returns>
ContextMenuCommand ContextMenu::Show(SDL_Window* window)
{
	// ウィンドウが無効な場合は None を返します。
	if (window == nullptr)
	{
		return ContextMenuCommand::None;
	}

	// SDL ウィンドウからネイティブの HWND を取得します。失敗した場合は None を返します。
	const SDL_PropertiesID windowProperties = SDL_GetWindowProperties(window);
	HWND nativeWindow = reinterpret_cast<HWND>(SDL_GetPointerProperty(
		windowProperties,
		SDL_PROP_WINDOW_WIN32_HWND_POINTER,
		nullptr));
	if (nativeWindow == nullptr)
	{
		return ContextMenuCommand::None;
	}

	// カーソル位置を取得します。失敗した場合は None を返します。
	POINT cursorPosition{};
	if (!GetCursorPos(&cursorPosition))
	{
		return ContextMenuCommand::None;
	}

	// コンテキストメニューを作成し、メニュー項目を追加して表示します。
	HMENU menu = CreatePopupMenu();
	if (menu == nullptr)
	{
		return ContextMenuCommand::None;
	}

	// メニュー項目を追加します。失敗した場合はメニューを破棄して None を返します。
	for (const MenuItem& item : MENU_ITEMS)
	{
		if (!AppendMenuW(menu, item.flags, item.command, item.label))
		{
			DestroyMenu(menu);
			return ContextMenuCommand::None;
		}
	}

	// ウィンドウを最前面に表示して、メニューが正しく表示されるようにします。
	SetForegroundWindow(nativeWindow);
	const UINT selectedCommand = TrackPopupMenuEx(
		menu,
		TPM_RETURNCMD | TPM_RIGHTBUTTON,
		cursorPosition.x,
		cursorPosition.y,
		nativeWindow,
		nullptr);
	DestroyMenu(menu);
	PostMessageW(nativeWindow, WM_NULL, 0, 0);

	// 選択されたコマンドIDを ContextMenuCommand 列挙型に変換して返します。
	switch (selectedCommand)
	{
		// 選択されたコマンドIDに応じて ContextMenuCommand を返します。未定義のコマンドIDの場合は None を返します。
		case FIT_TO_WINDOW_COMMAND:
			return ContextMenuCommand::FitToWindow;
		// ここで、FIT_TO_WINDOW_COMMAND の場合は ContextMenuCommand::FitToWindow を返します。
		case ORIGINAL_SIZE_COMMAND:
			return ContextMenuCommand::OriginalSize;
		// ここで、ORIGINAL_SIZE_COMMAND の場合は ContextMenuCommand::OriginalSize を返します。
		case TOGGLE_PLAYBACK_COMMAND:
			return ContextMenuCommand::TogglePlayback;
		// ここで、TOGGLE_PLAYBACK_COMMAND の場合は ContextMenuCommand::TogglePlayback を返します。
		case EXIT_COMMAND:
			return ContextMenuCommand::Exit;
		// ここで、EXIT_COMMAND の場合は ContextMenuCommand::Exit を返します。
		default:
			return ContextMenuCommand::None;
	}
}
