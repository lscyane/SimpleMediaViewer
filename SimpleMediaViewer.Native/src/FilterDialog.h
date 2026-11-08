#pragma once

#include <Windows.h>

class FileManager;


/// <summary>
/// 拡張子フィルターを選択するWin32ダイアログです。
/// </summary>
class FilterDialog final
{
public:
	static bool Show(HWND owner, FileManager& fileManager);
};
