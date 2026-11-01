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

	// SDL_Textureを保持します。
	SDL_Texture* texture_ = nullptr;
	// SDL3_imageのアニメーションデコーダーを保持します。
	SDL_Renderer* renderer_ = nullptr;
	// SDL3_imageのアニメーションデコーダーを保持します。
	IMG_AnimationDecoder* animationDecoder_ = nullptr;
	// AVIFデコーダーの状態を保持します。
	avifDecoder* avifDecoder_ = nullptr;
	// アニメーションデータとAVIFデータを保持します。
	std::vector<Uint8> animationData_;
	// AVIFデータを保持します。
	std::vector<Uint8> avifData_;
	// 次のフレームを先読みするためのサーフェスを保持します。
	SDL_Surface* pendingFrame_ = nullptr;
	// 画像のピクセル幅を保持します。
	int width_ = 0;
	// 画像のピクセル高さを保持します。
	int height_ = 0;
	// AVIFデコーダーでデコード済みのフレーム数を保持します。
	int avifFrameCount_ = 0;
	// AVIFデコーダーでデコード済みのフレーム数を保持します。
	int avifFramesDecoded_ = 0;
	// 現在のフレームの表示時間を保持します。
	Uint64 frameDuration_ = 0;
	// 次のフレームの表示時間を保持します。
	Uint64 pendingFrameDuration_ = 0;
	// 次のフレーム表示までの絶対時間を保持します。
	Uint64 nextFrameTime_ = 0;
	// 次のフレーム表示までの残り時間を保持します。
	Uint64 remainingFrameTime_ = 0;
	// アニメーションのループ回数を保持します。
	Sint64 loopCount_ = 0;
	// アニメーションの再生回数を保持します。
	Sint64 completedPlays_ = 0;
	// アニメーションが再生中かを示すフラグを保持します。
	bool isPlaying_ = false;
	// アニメーションが再生中かを示すフラグを保持します。
	bool isAnimated_ = false;
	// アニメーションが終了したかを示すフラグを保持します。
	bool isEnded_ = false;
	// デコーダーが完了したかを示すフラグを保持します。
	bool decoderComplete_ = false;
};
