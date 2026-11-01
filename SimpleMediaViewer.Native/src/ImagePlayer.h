#pragma once

#include <SDL3/SDL.h>

#include <string>
#include <vector>

struct IMG_AnimationDecoder;
struct avifDecoder;


/// <summary>
/// SDL3_imageで画像をGPUテクスチャへ読み込み、アニメーション再生と描画用サイズを管理します。
/// </summary>
class ImagePlayer final
{
public:
	/// <summary>対応画像を読み込み、前の画像テクスチャを置き換えます。</summary>
	bool Load(SDL_Renderer* renderer, const std::wstring& path);

	/// <summary>現在の画像テクスチャを破棄します。</summary>
	void Clear();

	/// <summary>表示期限を過ぎたアニメーション画像のフレームを進めます。</summary>
	bool AdvanceAnimation();

	/// <summary>アニメーション画像を一時停止または再開します。</summary>
	void TogglePause() noexcept;

	/// <summary>複数フレームの画像を読み込んでいるかを返します。</summary>
	bool IsAnimated() const noexcept;

	/// <summary>アニメーション画像を再生中かを返します。</summary>
	bool IsPlaying() const noexcept;

	/// <summary>次のフレーム切り替えまでの実時間をミリ秒で返します。</summary>
	int MillisecondsUntilNextFrame() const noexcept;

	/// <summary>画像テクスチャが読み込まれているかを返します。</summary>
	bool IsLoaded() const noexcept;

	/// <summary>画像のピクセル幅を返します。</summary>
	int Width() const noexcept;

	/// <summary>画像のピクセル高さを返します。</summary>
	int Height() const noexcept;

	/// <summary>指定された変換先矩形に画像を描画します。</summary>
	void Render(SDL_Renderer* renderer, const SDL_FRect& destination) const;

private:
	/// <summary>指定形式のアニメーションデコーダーからフレームを読み込みます。</summary>
	bool LoadAnimation(SDL_Renderer* renderer, const std::string& utf8Path, const std::wstring& path, bool isApng);
	/// <summary>libavifからアニメーションAVIFを読み込みます。</summary>
	bool LoadAvifAnimation(SDL_Renderer* renderer, const std::wstring& path);
	/// <summary>デコーダーから次に表示するフレームを先読みします。</summary>
	bool ReadNextFrame();
	/// <summary>libavifから次のAVIFフレームを先読みします。</summary>
	bool ReadNextAvifFrame();
	/// <summary>サーフェスから作成したテクスチャで現在のフレームを置き換えます。</summary>
	bool SetTextureFromSurface(SDL_Renderer* renderer, SDL_Surface* surface);

	SDL_Texture* texture_ = nullptr;
	SDL_Renderer* renderer_ = nullptr;
	IMG_AnimationDecoder* animationDecoder_ = nullptr;
	avifDecoder* avifDecoder_ = nullptr;
	std::vector<Uint8> animationData_;
	std::vector<Uint8> avifData_;
	SDL_Surface* pendingFrame_ = nullptr;
	int width_ = 0;
	int height_ = 0;
	int avifFrameCount_ = 0;
	int avifFramesDecoded_ = 0;
	Uint64 frameDuration_ = 0;
	Uint64 pendingFrameDuration_ = 0;
	Uint64 nextFrameTime_ = 0;
	Uint64 remainingFrameTime_ = 0;
	Sint64 loopCount_ = 0;
	Sint64 completedPlays_ = 0;
	bool isPlaying_ = false;
	bool isAnimated_ = false;
	bool isEnded_ = false;
	bool decoderComplete_ = false;
};
