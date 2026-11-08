#pragma once

#include <SDL3/SDL.h>

class FileManager;


/// <summary>
/// コンテキストメニューで使用するコマンドの種類を表す列挙型です。
/// </summary>
enum class ContextMenuCommand
{
	None,
	FitToWindow,
	OriginalSize,
	TogglePlayback,
	SortByNameAscending,
	SortByNameDescending,
	SortByDateAscending,
	SortByDateDescending,
	SortRandom,
	PreviousFile,
	NextFile,
	Exit
};


/// <summary>
/// 指定したウィンドウにコンテキストメニューを表示し、選択されたコマンドを返します。
/// </summary>
class ContextMenu final
{
public:
	static ContextMenuCommand Show(SDL_Window* window, FileManager& fileManager);
};
