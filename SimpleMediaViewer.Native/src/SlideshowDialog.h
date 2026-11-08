#pragma once

#include <Windows.h>

#include <optional>


/// <summary>
/// スライドショーの切替間隔を設定するダイアログです。
/// </summary>
class SlideshowDialog final
{
public:
	static std::optional<int> Show(HWND owner);
};
