#pragma once

#include <SDL3/SDL.h>
#include <vlc/vlc.h>

#include <array>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>


/// <summary>
/// LibVLCの遅延初期化、動画フレーム受け渡し、SDLテクスチャ表示を担当します。
/// VLCコールバックではSDLの描画APIを呼ばず、描画スレッドがテクスチャを更新します。
/// </summary>
class VideoPlayer final
{
public:
	/// <summary>最初の動画再生要求時にLibVLCを初期化して動画を再生します。</summary>
	bool Play(SDL_Renderer* renderer, Uint32 frameEventType, Uint32 playbackEndedEventType, const std::wstring& path, bool loopPlayback);

	/// <summary>現在の動画再生を停止し、動画テクスチャを破棄します。</summary>
	void Stop();

	/// <summary>プレーヤーとLibVLCインスタンスを解放します。</summary>
	void Shutdown();

	/// <summary>再生中の動画を一時停止または再開します。</summary>
	void TogglePause();

	/// <summary>シーク可能な動画を指定秒数だけ前後へシークします。</summary>
	void SeekBySeconds(int seconds);

	/// <summary>再生中の動画の音量を指定量だけ変更します。</summary>
	int AdjustVolume(int amount);

	/// <summary>新しいVLCフレームがあればSDLテクスチャをメインスレッドで更新します。</summary>
	bool UpdateTexture();

	/// <summary>動画テクスチャのピクセル幅を返します。</summary>
	int Width() const noexcept;

	/// <summary>動画テクスチャのピクセル高さを返します。</summary>
	int Height() const noexcept;

	/// <summary>指定された変換先矩形に動画テクスチャを描画します。</summary>
	void Render(SDL_Renderer* renderer, const SDL_FRect& destination) const;

	/// <summary>動画が再生中かどうかを返します。</summary>
	bool IsActive() const noexcept;

	/// <summary>現在の再生世代を返します。</summary>
	int PlaybackGeneration() const noexcept;

private:
	struct PlaybackCallbackContext
	{
		Uint32 eventType = 0;
		int generation = 0;
	};

	struct FrameSlot
	{
		std::vector<Uint8> pixels;
		bool decoding = false;
		bool uploading = false;
		std::uint64_t sequence = 0;
	};

	/// <summary>LibVLCインスタンスを必要になった時点で一度だけ生成します。</summary>
	bool EnsureLibVlcInitialized();

	/// <summary>現在のLibVLCメディアプレーヤーを停止して解放します。</summary>
	void ReleaseMediaPlayer();

	/// <summary>LibVLCの映像形式をSDLへ渡すRV32/32バイト境界配置へ設定します。</summary>
	static unsigned SetupVideoFormat(void** opaque, char* chroma, unsigned* width, unsigned* height, unsigned* pitches, unsigned* lines);

	/// <summary>デコーダーが書き込める未使用の共有フレームバッファを取得します。</summary>
	static void* LockVideoFrame(void* opaque, void** planes);

	/// <summary>デコード完了を記録し、SDLテクスチャを更新できるようイベントで通知します。</summary>
	static void DisplayVideoFrame(void* opaque, void* picture);

	/// <summary>停止時にコールバック待機を解除してフレーム管理状態を初期化します。</summary>
	static void CleanupVideoFormat(void* opaque);

	/// <summary>SDLイベントをスレッドセーフに積み、メインループを新フレーム待ちから復帰させます。</summary>
	void NotifyFrameReady() const;
	/// <summary>LibVLCの再生終了をSDLイベントでメインループへ通知します。</summary>
	static void NotifyPlaybackEnded(const libvlc_event_t* event, void* opaque);

	/// <summary>デコード完了後のバッファを解放せず、表示コールバックまで予約状態を維持します。</summary>
	static void UnlockVideoFrame(void* opaque, void* picture, void* const* planes);

	// LibVLCインスタンスを保持します。
	libvlc_instance_t* instance_ = nullptr;
	// LibVLCのメディアプレーヤーを保持します。
	libvlc_media_player_t* mediaPlayer_ = nullptr;
	// LibVLCの再生終了を通知するイベントマネージャーを保持します。
	libvlc_event_manager_t* eventManager_ = nullptr;
	// LibVLCの再生終了を通知するコールバックコンテキストを保持します。
	std::unique_ptr<PlaybackCallbackContext> playbackCallbackContext_;
	// SDLイベントタイプを保持します。新しいフレームが描画可能になった際にメインループへ通知します。
	SDL_Texture* texture_ = nullptr;
	// VLCの再生終了を通知するSDLイベントタイプを保持します。
	Uint32 frameEventType_ = 0;
	// LibVLCの再生終了を通知するSDLイベントタイプを保持します。
	Uint32 playbackEndedEventType_ = 0;
	// 現在の再生世代を保持します。再生中の動画が停止されると、再生世代が増加します。
	int playbackGeneration_ = 0;
	// 動画テクスチャのピクセル幅を保持します。
	int textureWidth_ = 0;
	// 動画テクスチャのピクセル高さを保持します。
	int textureHeight_ = 0;

	// デコード済みスロットの状態を保護するミューテックス。SDL描画スレッドがフレームをアップロードする際に使用されます。
	std::mutex frameMutex_;
	// デコード済みスロットの状態を保護する条件変数。SDL描画スレッドがフレームをアップロードする際に使用されます。
	std::condition_variable frameCondition_;
	// デコード済みスロットを保持します。SDL描画スレッドがフレームをアップロードする際に使用されます。
	std::array<FrameSlot, 3> frameSlots_{};
	// シャットダウン時に使用する特別なフレームスロットを保持します。
	FrameSlot shutdownFrameSlot_{};
	// デコード済みスロットの中で、最新フレームとして公開されているスロットのインデックスを保持します。
	int latestFrameIndex_ = -1;
	// デコード済みスロットの中で、次に割り当てるフレームのシーケンス番号を保持します。
	std::uint64_t nextSequence_ = 0;
	// デコード済みスロットの中で、SDL描画スレッドが最後にアップロードしたフレームのシーケンス番号を保持します。
	std::uint64_t uploadedSequence_ = 0;
	// フレームのピクセル幅を保持します。VLCのSetupVideoFormatコールバックで使用されます。
	unsigned frameWidth_ = 0;
	// フレームのピクセル高さを保持します。VLCのSetupVideoFormatコールバックで使用されます。
	unsigned frameHeight_ = 0;
	// フレームのピッチを保持します。VLCのSetupVideoFormatコールバックで使用されます。
	unsigned framePitch_ = 0;
	// フレームスロットの設定中かどうかを保持します。VLCのSetupVideoFormatコールバックで使用されます。
	bool isConfiguring_ = false;
	// シャットダウン中かどうかを保持します。メディアプレーヤーの停止時にフレームスロットの解放を待機するために使用されます。
	bool isShuttingDown_ = false;
	// SDLレンダラーを保持します。描画操作を行う際に使用されます。
	SDL_Renderer* renderer_ = nullptr;
};
