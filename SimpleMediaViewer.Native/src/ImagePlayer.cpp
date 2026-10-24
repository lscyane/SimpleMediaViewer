#define NOMINMAX
#include "ImagePlayer.h"

#include <SDL3_image/SDL_image.h>
#include <Windows.h>

#include <string>

/// <summary>画像パスをUTF-8化してSDL3_imageからGPUテクスチャとして読み込みます。</summary>
bool ImagePlayer::Load(SDL_Renderer* renderer, const std::wstring& path)
{
	Clear();

	const int requiredLength = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, nullptr, 0, nullptr, nullptr);
	if (requiredLength <= 1)
	{
		return false;
	}

	std::string utf8Path(static_cast<size_t>(requiredLength), '\0');
	if (WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, utf8Path.data(), requiredLength, nullptr, nullptr) == 0)
	{
		return false;
	}
	utf8Path.resize(static_cast<size_t>(requiredLength - 1));

	texture_ = IMG_LoadTexture(renderer, utf8Path.c_str());
	if (texture_ == nullptr)
	{
		return false;
	}

	float width = 0.0f;
	float height = 0.0f;
	if (!SDL_GetTextureSize(texture_, &width, &height) || width <= 0.0f || height <= 0.0f)
	{
		Clear();
		return false;
	}

	width_ = static_cast<int>(width);
	height_ = static_cast<int>(height);
	return true;
}

/// <summary>保持中のSDL画像テクスチャを破棄し、サイズ情報を初期化します。</summary>
void ImagePlayer::Clear()
{
	if (texture_ != nullptr)
	{
		SDL_DestroyTexture(texture_);
		texture_ = nullptr;
	}
	width_ = 0;
	height_ = 0;
}

/// <summary>画像テクスチャが読み込まれているかを返します。</summary>
bool ImagePlayer::IsLoaded() const noexcept
{
	return texture_ != nullptr;
}

/// <summary>画像のピクセル幅を返します。</summary>
int ImagePlayer::Width() const noexcept
{
	return width_;
}

/// <summary>画像のピクセル高さを返します。</summary>
int ImagePlayer::Height() const noexcept
{
	return height_;
}

/// <summary>指定された変換先矩形に画像を描画します。</summary>
void ImagePlayer::Render(SDL_Renderer* renderer, const SDL_FRect& destination) const
{
	if (texture_ != nullptr)
	{
		SDL_RenderTexture(renderer, texture_, nullptr, &destination);
	}
}
