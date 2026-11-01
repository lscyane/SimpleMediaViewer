#pragma once

#include <SDL3/SDL.h>

#include <string>


/// <summary>
/// SDL3_imageで静止画をGPUテクスチャへ読み込み、描画用サイズを管理します。
/// </summary>
class ImagePlayer final
{
public:
	/// <summary>対応画像を読み込み、前の画像テクスチャを置き換えます。</summary>
	bool Load(SDL_Renderer* renderer, const std::wstring& path);

	/// <summary>現在の画像テクスチャを破棄します。</summary>
	void Clear();

	/// <summary>画像テクスチャが読み込まれているかを返します。</summary>
	bool IsLoaded() const noexcept;

	/// <summary>画像のピクセル幅を返します。</summary>
	int Width() const noexcept;

	/// <summary>画像のピクセル高さを返します。</summary>
	int Height() const noexcept;

	/// <summary>指定された変換先矩形に画像を描画します。</summary>
	void Render(SDL_Renderer* renderer, const SDL_FRect& destination) const;

private:
	SDL_Texture* texture_ = nullptr;
	int width_ = 0;
	int height_ = 0;
};
