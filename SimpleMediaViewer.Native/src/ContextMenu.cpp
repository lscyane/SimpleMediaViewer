#include "ContextMenu.h"
#include "FileManager.h"
#include "FilterDialog.h"

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
	constexpr UINT SORT_BY_NAME_ASCENDING_COMMAND = 5;
	constexpr UINT SORT_BY_NAME_DESCENDING_COMMAND = 6;
	constexpr UINT SORT_BY_DATE_ASCENDING_COMMAND = 7;
	constexpr UINT SORT_BY_DATE_DESCENDING_COMMAND = 8;
	constexpr UINT SORT_RANDOM_COMMAND = 9;
	constexpr UINT PREVIOUS_FILE_COMMAND = 10;
	constexpr UINT NEXT_FILE_COMMAND = 11;
	constexpr UINT FILTER_COMMAND = 12;
	constexpr UINT ROTATE_RIGHT_COMMAND = 13;
	constexpr UINT ROTATE_LEFT_COMMAND = 14;
	constexpr UINT TOGGLE_SLIDESHOW_COMMAND = 15;

	struct MenuItem
	{
		UINT flags;
		UINT command;
		LPCWSTR label;
	};

	constexpr std::array<MenuItem, 7> SORT_MENU_ITEMS{{
		{ MF_STRING, SORT_BY_NAME_ASCENDING_COMMAND, L"名前 (昇順)" },
		{ MF_STRING, SORT_BY_NAME_DESCENDING_COMMAND, L"名前 (降順)" },
		{ MF_SEPARATOR, 0, nullptr },
		{ MF_STRING, SORT_BY_DATE_ASCENDING_COMMAND, L"更新日時 (昇順)" },
		{ MF_STRING, SORT_BY_DATE_DESCENDING_COMMAND, L"更新日時 (降順)" },
		{ MF_SEPARATOR, 0, nullptr },
		{ MF_STRING, SORT_RANDOM_COMMAND, L"ランダム" }
	}};

	constexpr std::array<MenuItem, 2> MENU_ITEMS{{
		{ MF_SEPARATOR, 0, nullptr },
		{ MF_STRING, EXIT_COMMAND, L"終了\tEsc" }
	}};

	UINT GetSortOrderCommand(FileSortOrder sortOrder)
	{
		switch (sortOrder)
		{
		case FileSortOrder::NameAscending:
			return SORT_BY_NAME_ASCENDING_COMMAND;
		case FileSortOrder::NameDescending:
			return SORT_BY_NAME_DESCENDING_COMMAND;
		case FileSortOrder::DateAscending:
			return SORT_BY_DATE_ASCENDING_COMMAND;
		case FileSortOrder::DateDescending:
			return SORT_BY_DATE_DESCENDING_COMMAND;
		case FileSortOrder::Random:
			return SORT_RANDOM_COMMAND;
		default:
			return SORT_BY_NAME_ASCENDING_COMMAND;
		}
	}
}


/// <summary>
/// 指定されたウィンドウに対してカーソル位置へコンテキストメニューを表示し、選択されたコマンドを返します。ウィンドウが無効な場合やメニュー表示に失敗した場合は None を返します。
/// </summary>
/// <param name="window">コンテキストメニューの表示対象となる SDL ウィンドウへのポインターです。</param>
/// <returns>ユーザーが選択した ContextMenuCommand を返します。キャンセル時または処理失敗時は ContextMenuCommand::None です。</returns>
ContextMenuCommand ContextMenu::Show(SDL_Window* window, FileManager& fileManager, bool isSlideshowActive)
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
	HMENU listMenu = CreatePopupMenu();
	HMENU sortMenu = CreatePopupMenu();
	if (menu == nullptr || listMenu == nullptr || sortMenu == nullptr)
	{
		if (menu != nullptr)
		{
			DestroyMenu(menu);
		}
		if (listMenu != nullptr)
		{
			DestroyMenu(listMenu);
		}
		if (sortMenu != nullptr)
		{
			DestroyMenu(sortMenu);
		}
		return ContextMenuCommand::None;
	}

	// 並び順サブメニューを作成します。
	for (const MenuItem& item : SORT_MENU_ITEMS)
	{
		if (!AppendMenuW(sortMenu, item.flags, item.command, item.label))
		{
			DestroyMenu(sortMenu);
			DestroyMenu(listMenu);
			DestroyMenu(menu);
			return ContextMenuCommand::None;
		}
	}
	CheckMenuRadioItem(
		sortMenu,
		SORT_BY_NAME_ASCENDING_COMMAND,
		SORT_RANDOM_COMMAND,
		GetSortOrderCommand(fileManager.GetSortOrder()),
		MF_BYCOMMAND);
	if (!AppendMenuW(listMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(sortMenu), L"並び順"))
	{
		DestroyMenu(sortMenu);
		DestroyMenu(listMenu);
		DestroyMenu(menu);
		return ContextMenuCommand::None;
	}
	if (!AppendMenuW(listMenu, MF_SEPARATOR, 0, nullptr)
		|| !AppendMenuW(listMenu, MF_STRING, FILTER_COMMAND, L"フィルター..."))
	{
		DestroyMenu(listMenu);
		DestroyMenu(menu);
		return ContextMenuCommand::None;
	}
	if (!AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(listMenu), L"リスト"))
	{
		DestroyMenu(listMenu);
		DestroyMenu(menu);
		return ContextMenuCommand::None;
	}

	HMENU displayMenu = CreatePopupMenu();
	HMENU operationMenu = CreatePopupMenu();
	if (displayMenu == nullptr || operationMenu == nullptr)
	{
		if (displayMenu != nullptr)
		{
			DestroyMenu(displayMenu);
		}
		if (operationMenu != nullptr)
		{
			DestroyMenu(operationMenu);
		}
		DestroyMenu(menu);
		return ContextMenuCommand::None;
	}

	// 表示と操作のサブメニューを作成します。
	if (!AppendMenuW(displayMenu, MF_STRING, FIT_TO_WINDOW_COMMAND, L"ウィンドウに合わせる\tF")
		|| !AppendMenuW(displayMenu, MF_STRING, ORIGINAL_SIZE_COMMAND, L"等倍表示\tG")
		|| !AppendMenuW(displayMenu, MF_STRING, ROTATE_RIGHT_COMMAND, L"右回転\tR")
		|| !AppendMenuW(displayMenu, MF_STRING, ROTATE_LEFT_COMMAND, L"左回転\tL")
		|| !AppendMenuW(operationMenu, MF_STRING, TOGGLE_PLAYBACK_COMMAND, L"再生 / 一時停止\tSpace")
		|| !AppendMenuW(operationMenu, MF_STRING, TOGGLE_SLIDESHOW_COMMAND,
			isSlideshowActive ? L"スライドショー停止\tA" : L"スライドショー\tA")
		|| !AppendMenuW(operationMenu, MF_SEPARATOR, 0, nullptr)
		|| !AppendMenuW(operationMenu, MF_STRING, PREVIOUS_FILE_COMMAND, L"前のファイル\tLeft")
		|| !AppendMenuW(operationMenu, MF_STRING, NEXT_FILE_COMMAND, L"次のファイル\tRight"))
	{
		DestroyMenu(operationMenu);
		DestroyMenu(displayMenu);
		DestroyMenu(menu);
		return ContextMenuCommand::None;
	}
	if (!AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(displayMenu), L"表示")
		|| !AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(operationMenu), L"操作"))
	{
		DestroyMenu(menu);
		DestroyMenu(operationMenu);
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
	if (selectedCommand == FILTER_COMMAND)
	{
		FilterDialog::Show(nativeWindow, fileManager);
		return ContextMenuCommand::None;
	}

	// 選択されたコマンドIDを ContextMenuCommand 列挙型に変換して返します。
	switch (selectedCommand)
	{
		// 選択されたコマンドIDに応じて ContextMenuCommand を返します。未定義のコマンドIDの場合は None を返します。
		case FIT_TO_WINDOW_COMMAND:
			return ContextMenuCommand::FitToWindow;
		// ここで、FIT_TO_WINDOW_COMMAND の場合は ContextMenuCommand::FitToWindow を返します。
		case ORIGINAL_SIZE_COMMAND:
			return ContextMenuCommand::OriginalSize;
		case ROTATE_RIGHT_COMMAND:
			return ContextMenuCommand::RotateRight;
		case ROTATE_LEFT_COMMAND:
			return ContextMenuCommand::RotateLeft;
		// ここで、ORIGINAL_SIZE_COMMAND の場合は ContextMenuCommand::OriginalSize を返します。
		case TOGGLE_PLAYBACK_COMMAND:
			return ContextMenuCommand::TogglePlayback;
		case TOGGLE_SLIDESHOW_COMMAND:
			return ContextMenuCommand::ToggleSlideshow;
		// ここで、TOGGLE_PLAYBACK_COMMAND の場合は ContextMenuCommand::TogglePlayback を返します。
		case EXIT_COMMAND:
			return ContextMenuCommand::Exit;
		// ここで、EXIT_COMMAND の場合は ContextMenuCommand::Exit を返します。
		case SORT_BY_NAME_ASCENDING_COMMAND:
			return ContextMenuCommand::SortByNameAscending;
		case SORT_BY_NAME_DESCENDING_COMMAND:
			return ContextMenuCommand::SortByNameDescending;
		case SORT_BY_DATE_ASCENDING_COMMAND:
			return ContextMenuCommand::SortByDateAscending;
		case SORT_BY_DATE_DESCENDING_COMMAND:
			return ContextMenuCommand::SortByDateDescending;
		case SORT_RANDOM_COMMAND:
			return ContextMenuCommand::SortRandom;
		case PREVIOUS_FILE_COMMAND:
			return ContextMenuCommand::PreviousFile;
		case NEXT_FILE_COMMAND:
			return ContextMenuCommand::NextFile;
		default:
			return ContextMenuCommand::None;
	}
}
