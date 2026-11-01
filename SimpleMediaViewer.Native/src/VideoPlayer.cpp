#define NOMINMAX
#include "VideoPlayer.h"

#include <Windows.h>
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <iterator>
#include <limits>
#include <string>
#include "CONST.hpp"


/// <summary>
/// VLC初期化を遅延させ、指定動画をSDL描画スレッドと共有可能な出力形式で再生します。
/// </summary>
bool VideoPlayer::Play(SDL_Renderer* renderer, Uint32 frameEventType, Uint32 playbackEndedEventType, const std::wstring& path)
{
	Stop();
	renderer_ = renderer;
	frameEventType_ = frameEventType;
	playbackEndedEventType_ = playbackEndedEventType;

	if (!EnsureLibVlcInitialized())
	{
		return false;
	}

	const std::string utf8Path = [&path]()
	{
		const int length = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, nullptr, 0, nullptr, nullptr);
		if (length <= 1)
		{
			return std::string{};
		}
		std::string result(static_cast<size_t>(length), '\0');
		WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, result.data(), length, nullptr, nullptr);
		result.resize(static_cast<size_t>(length - 1));
		return result;
	}();
	if (utf8Path.empty())
	{
		return false;
	}

	libvlc_media_t* media = libvlc_media_new_path(instance_, utf8Path.c_str());
	if (media == nullptr)
	{
		return false;
	}

	mediaPlayer_ = libvlc_media_player_new_from_media(media);
	libvlc_media_release(media);
	if (mediaPlayer_ == nullptr)
	{
		return false;
	}
	libvlc_audio_set_volume(mediaPlayer_, VIDEO_INITIAL_VOLUME);
	playbackCallbackContext_ = std::make_unique<PlaybackCallbackContext>(
		PlaybackCallbackContext{ playbackEndedEventType_, playbackGeneration_ });
	eventManager_ = libvlc_media_player_event_manager(mediaPlayer_);
	if (eventManager_ == nullptr || libvlc_event_attach(
		eventManager_,
		libvlc_MediaPlayerEndReached,
		NotifyPlaybackEnded,
		playbackCallbackContext_.get()) != 0)
	{
		ReleaseMediaPlayer();
		return false;
	}

	{
		std::lock_guard lock(frameMutex_);
		isShuttingDown_ = false;
	}

	libvlc_video_set_callbacks(mediaPlayer_, LockVideoFrame, UnlockVideoFrame, DisplayVideoFrame, this);
	libvlc_video_set_format_callbacks(mediaPlayer_, SetupVideoFormat, CleanupVideoFormat);

	if (libvlc_media_player_play(mediaPlayer_) != 0)
	{
		ReleaseMediaPlayer();
		return false;
	}

	return true;
}


/// <summary>
/// 動画プレーヤーを止め、動画テクスチャを描画スレッドで解放します。
/// </summary>
void VideoPlayer::Stop()
{
	ReleaseMediaPlayer();
	playbackGeneration_ = playbackGeneration_ == std::numeric_limits<int>::max() ? 1 : playbackGeneration_ + 1;
	if (texture_ != nullptr)
	{
		SDL_DestroyTexture(texture_);
		texture_ = nullptr;
	}
	textureWidth_ = 0;
	textureHeight_ = 0;
}


/// <summary>
/// LibVLCインスタンスを含むすべての動画再生リソースを解放します。
/// </summary>
void VideoPlayer::Shutdown()
{
	Stop();
	if (instance_ != nullptr)
	{
		libvlc_release(instance_);
		instance_ = nullptr;
	}
	renderer_ = nullptr;
}


/// <summary>
/// 再生中の動画を一時停止または再開します。
/// </summary>
void VideoPlayer::TogglePause()
{
	if (mediaPlayer_ == nullptr)
	{
		return;
	}

	if (libvlc_media_player_is_playing(mediaPlayer_))
	{
		libvlc_media_player_pause(mediaPlayer_);
	}
	else
	{
		libvlc_media_player_play(mediaPlayer_);
	}
}


/// <summary>
/// シーク可能な動画を指定秒数だけ前後へシークします。
/// </summary>
void VideoPlayer::SeekBySeconds(int seconds)
{
	if (mediaPlayer_ == nullptr || !libvlc_media_player_is_seekable(mediaPlayer_))
	{
		return;
	}

	const libvlc_time_t currentTime = libvlc_media_player_get_time(mediaPlayer_);
	if (currentTime < 0)
	{
		return;
	}

	const libvlc_time_t offset = static_cast<libvlc_time_t>(seconds) * 1000;
	const libvlc_time_t duration = libvlc_media_player_get_length(mediaPlayer_);
	libvlc_time_t targetTime = std::max<libvlc_time_t>(0, currentTime + offset);
	if (duration >= 0)
	{
		targetTime = std::min(targetTime, duration);
	}
	libvlc_media_player_set_time(mediaPlayer_, targetTime);
}


/// <summary>
/// 再生中の動画の音量を0～100の範囲で変更します。
/// </summary>
int VideoPlayer::AdjustVolume(int amount)
{
	if (mediaPlayer_ == nullptr)
	{
		return -1;	// 再生中の動画がない場合は音量を変更できない
	}

	const int currentVolume = libvlc_audio_get_volume(mediaPlayer_);
	if (currentVolume < 0)
	{
		return -1;	// 音量取得に失敗した場合は音量を変更できない
	}

	// 変更後の音量を0～100の範囲に制限して設定する
	const int targetVolume = std::clamp(currentVolume + amount, 0, 100);
	if (libvlc_audio_set_volume(mediaPlayer_, targetVolume) != 0)
	{
		return -1;
	}

	return targetVolume;
}


/// <summary>
/// 最新フレームをSDLテクスチャへ転送します。SDL操作は呼び出し元スレッド上だけで行います。
/// </summary>
bool VideoPlayer::UpdateTexture()
{
	FrameSlot* slot = nullptr;
	int frameIndex = -1;
	unsigned width = 0;
	unsigned height = 0;
	unsigned pitch = 0;
	std::uint64_t sequence = 0;

	{
		std::lock_guard lock(frameMutex_);
		if (latestFrameIndex_ < 0)
		{
			return false;
		}

		frameIndex = latestFrameIndex_;
		slot = &frameSlots_[static_cast<size_t>(frameIndex)];
		if (slot->sequence <= uploadedSequence_ || slot->pixels.empty())
		{
			return false;
		}

		slot->uploading = true;
		width = frameWidth_;
		height = frameHeight_;
		pitch = framePitch_;
		sequence = slot->sequence;
	}

	// テクスチャの作成と更新はSDLのメインスレッドで行います。
	if (texture_ == nullptr || textureWidth_ != static_cast<int>(width) || textureHeight_ != static_cast<int>(height))
	{
		if (texture_ != nullptr)
		{
			SDL_DestroyTexture(texture_);
		}
		texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
			static_cast<int>(width), static_cast<int>(height));
		textureWidth_ = texture_ != nullptr ? static_cast<int>(width) : 0;
		textureHeight_ = texture_ != nullptr ? static_cast<int>(height) : 0;
	}

	const bool updated = texture_ != nullptr && SDL_UpdateTexture(texture_, nullptr, slot->pixels.data(), static_cast<int>(pitch));

	{
		std::lock_guard lock(frameMutex_);
		slot->uploading = false;
		if (updated)
		{
			uploadedSequence_ = std::max(uploadedSequence_, sequence);
		}
	}
	frameCondition_.notify_all();
	return updated;
}


/// <summary>
/// 動画テクスチャのピクセル幅を返します。
/// </summary>
int VideoPlayer::Width() const noexcept
{
	return textureWidth_;
}


/// <summary>
/// 動画テクスチャのピクセル高さを返します。
/// </summary>
int VideoPlayer::Height() const noexcept
{
	return textureHeight_;
}


/// <summary>
/// 指定された変換先矩形に動画テクスチャを描画します。
/// </summary>
void VideoPlayer::Render(SDL_Renderer* renderer, const SDL_FRect& destination) const
{
	if (texture_ != nullptr)
	{
		SDL_RenderTexture(renderer, texture_, nullptr, &destination);
	}
}


/// <summary>
/// 動画プレーヤーが現在存在し、停止処理中ではないかを返します。
/// </summary>
bool VideoPlayer::IsActive() const noexcept
{
	return mediaPlayer_ != nullptr;
}


/// <summary>
/// 現在の再生世代を返します。
/// </summary>
int VideoPlayer::PlaybackGeneration() const noexcept
{
	return playbackGeneration_;
}


/// <summary>
/// 動画を開く要求までLibVLCインスタンスとプラグインパスの初期化を遅らせます。
/// </summary>
bool VideoPlayer::EnsureLibVlcInitialized()
{
	if (instance_ != nullptr)
	{
		return true;
	}

	const char* basePath = SDL_GetBasePath();
	if (basePath == nullptr)
	{
		return false;
	}

	const std::string pluginPath = std::string("--plugin-path=") + basePath + "plugins";

	const char* arguments[] = { "--no-video-title-show", pluginPath.c_str() };
	instance_ = libvlc_new(static_cast<int>(std::size(arguments)), arguments);
	return instance_ != nullptr;
}


/// <summary>
/// デコーダーを停止してコールバック終了を待ち、メディアプレーヤーを解放します。
/// </summary>
void VideoPlayer::ReleaseMediaPlayer()
{
	if (mediaPlayer_ == nullptr)
	{
		return;
	}
	if (eventManager_ != nullptr && playbackCallbackContext_ != nullptr)
	{
		libvlc_event_detach(
			eventManager_,
			libvlc_MediaPlayerEndReached,
			NotifyPlaybackEnded,
			playbackCallbackContext_.get());
		eventManager_ = nullptr;
	}

	{
		std::lock_guard lock(frameMutex_);
		isShuttingDown_ = true;
	}
	frameCondition_.notify_all();

	libvlc_media_player_stop(mediaPlayer_);
	libvlc_media_player_release(mediaPlayer_);
	mediaPlayer_ = nullptr;
	playbackCallbackContext_.reset();

	{
		std::lock_guard lock(frameMutex_);
		for (FrameSlot& slot : frameSlots_)
		{
			slot.decoding = false;
			slot.uploading = false;
			slot.pixels.clear();
		}
		shutdownFrameSlot_.decoding = false;
		shutdownFrameSlot_.pixels.clear();
		latestFrameIndex_ = -1;
		frameWidth_ = 0;
		frameHeight_ = 0;
		framePitch_ = 0;
		uploadedSequence_ = 0;
		isShuttingDown_ = false;
	}
	frameCondition_.notify_all();
}


/// <summary>
/// LibVLCの終了通知を描画処理なしでSDLイベントキューへ追加します。
/// </summary>
void VideoPlayer::NotifyPlaybackEnded(const libvlc_event_t*, void* opaque)
{
	const auto* context = static_cast<const PlaybackCallbackContext*>(opaque);
	if (context == nullptr || context->eventType == 0 || context->eventType == static_cast<Uint32>(-1))
	{
		return;
	}

	SDL_Event event{};
	event.type = context->eventType;
	event.user.code = context->generation;
	SDL_PushEvent(&event);
}


/// <summary>
/// VLCのRV32フレームをSDLへ渡せるよう寸法と32バイト境界のピッチを設定します。
/// </summary>
unsigned VideoPlayer::SetupVideoFormat(void** opaque, char* chroma, unsigned* width, unsigned* height, unsigned* pitches, unsigned* lines)
{
	auto* player = static_cast<VideoPlayer*>(*opaque);
	if (player == nullptr || width == nullptr || height == nullptr || pitches == nullptr || lines == nullptr ||
		*width == 0 || *height == 0 || *width > MAX_VIDEO_DIMENSION || *height > MAX_VIDEO_DIMENSION)
	{
		return 0;
	}

	const std::uint64_t rowBytes = static_cast<std::uint64_t>(*width) * 4;
	const std::uint64_t alignedPitch = (rowBytes + VIDEO_ALIGNMENT - 1) & ~(static_cast<std::uint64_t>(VIDEO_ALIGNMENT) - 1);
	const std::uint64_t alignedLines = (static_cast<std::uint64_t>(*height) + VIDEO_ALIGNMENT - 1) & ~(static_cast<std::uint64_t>(VIDEO_ALIGNMENT) - 1);
	if (alignedPitch > std::numeric_limits<unsigned>::max() || alignedLines > std::numeric_limits<unsigned>::max() ||
		alignedPitch * alignedLines > std::numeric_limits<size_t>::max())
	{
		return 0;
	}

	std::unique_lock lock(player->frameMutex_);
	player->isConfiguring_ = true;
	player->frameCondition_.wait(lock, [player]()
	{
		return !player->shutdownFrameSlot_.decoding &&
			std::none_of(player->frameSlots_.begin(), player->frameSlots_.end(), [](const FrameSlot& slot)
		{
			return slot.decoding || slot.uploading;
		});
	});

	try
	{
		const size_t bufferSize = static_cast<size_t>(alignedPitch * alignedLines);
		for (FrameSlot& slot : player->frameSlots_)
		{
			slot.pixels.assign(bufferSize, 0);
			slot.decoding = false;
			slot.uploading = false;
			slot.sequence = 0;
		}
		player->shutdownFrameSlot_.pixels.assign(bufferSize, 0);
		player->shutdownFrameSlot_.decoding = false;
		player->shutdownFrameSlot_.uploading = false;
		player->shutdownFrameSlot_.sequence = 0;
	}
	catch (...)
	{
		player->isConfiguring_ = false;
		lock.unlock();
		player->frameCondition_.notify_all();
		return 0;
	}

	std::memcpy(chroma, "RV32", 4);
	pitches[0] = static_cast<unsigned>(alignedPitch);
	lines[0] = static_cast<unsigned>(alignedLines);
	player->frameWidth_ = *width;
	player->frameHeight_ = *height;
	player->framePitch_ = static_cast<unsigned>(alignedPitch);
	player->latestFrameIndex_ = -1;
	player->nextSequence_ = 0;
	player->uploadedSequence_ = 0;
	player->isConfiguring_ = false;
	lock.unlock();
	player->frameCondition_.notify_all();
	return static_cast<unsigned>(player->frameSlots_.size());
}


/// <summary>
/// デコーダーに未使用スロットを貸し出し、VLCが直接ピクセルを書き込めるようにします。
/// </summary>
void* VideoPlayer::LockVideoFrame(void* opaque, void** planes)
{
	auto* player = static_cast<VideoPlayer*>(opaque);
	if (player == nullptr || planes == nullptr)
	{
		return nullptr;
	}

	std::unique_lock lock(player->frameMutex_);
	player->frameCondition_.wait(lock, [player]()
	{
		if (player->isShuttingDown_)
		{
			return true;
		}
		if (player->isConfiguring_)
		{
			return false;
		}
		return std::any_of(player->frameSlots_.begin(), player->frameSlots_.end(), [player](const FrameSlot& slot)
		{
			const int index = static_cast<int>(&slot - player->frameSlots_.data());
			return !slot.decoding && !slot.uploading && index != player->latestFrameIndex_ && !slot.pixels.empty();
		});
	});

	if (player->isShuttingDown_)
	{
		player->frameCondition_.wait(lock, [player]()
		{
			return !player->shutdownFrameSlot_.decoding;
		});
		if (player->shutdownFrameSlot_.pixels.empty())
		{
			return nullptr;
		}

		player->shutdownFrameSlot_.decoding = true;
		planes[0] = player->shutdownFrameSlot_.pixels.data();
		return &player->shutdownFrameSlot_;
	}

	for (size_t index = 0; index < player->frameSlots_.size(); ++index)
	{
		FrameSlot& slot = player->frameSlots_[index];
		if (!slot.decoding && !slot.uploading && static_cast<int>(index) != player->latestFrameIndex_ && !slot.pixels.empty())
		{
			slot.decoding = true;
			planes[0] = slot.pixels.data();
			return &slot;
		}
	}

	return nullptr;
}


/// <summary>
/// 描画コールバックまでフレームスロットを占有したままにします。
/// </summary>
void VideoPlayer::UnlockVideoFrame(void*, void*, void* const*)
{
}


/// <summary>
/// デコード済みスロットを最新フレームとして公開し、SDLメインループへ通知します。
/// </summary>
void VideoPlayer::DisplayVideoFrame(void* opaque, void* picture)
{
	auto* player = static_cast<VideoPlayer*>(opaque);
	auto* slot = static_cast<FrameSlot*>(picture);
	if (player == nullptr || slot == nullptr)
	{
		return;
	}
	if (slot == &player->shutdownFrameSlot_)
	{
		{
			std::lock_guard lock(player->frameMutex_);
			slot->decoding = false;
		}
		player->frameCondition_.notify_all();
		return;
	}

	{
		std::lock_guard lock(player->frameMutex_);
		const auto slotIndex = slot - player->frameSlots_.data();
		if (slotIndex < 0 || slotIndex >= static_cast<ptrdiff_t>(player->frameSlots_.size()))
		{
			return;
		}
		slot->decoding = false;
		slot->sequence = ++player->nextSequence_;
		player->latestFrameIndex_ = static_cast<int>(slotIndex);
	}
	player->frameCondition_.notify_all();
	player->NotifyFrameReady();
}


/// <summary>
/// VLCが映像形式を破棄するときに、共有バッファを競合なく解放します。
/// </summary>
void VideoPlayer::CleanupVideoFormat(void* opaque)
{
	auto* player = static_cast<VideoPlayer*>(opaque);
	if (player == nullptr)
	{
		return;
	}

	std::unique_lock lock(player->frameMutex_);
	player->isConfiguring_ = true;
	player->frameCondition_.wait(lock, [player]()
	{
		return !player->shutdownFrameSlot_.decoding &&
			std::none_of(player->frameSlots_.begin(), player->frameSlots_.end(), [](const FrameSlot& slot)
		{
			return slot.decoding || slot.uploading;
		});
	});
	for (FrameSlot& slot : player->frameSlots_)
	{
		slot.pixels.clear();
	}
	player->latestFrameIndex_ = -1;
	player->frameWidth_ = 0;
	player->frameHeight_ = 0;
	player->framePitch_ = 0;
	player->isConfiguring_ = false;
	lock.unlock();
	player->frameCondition_.notify_all();
}


/// <summary>
/// VLCデコードスレッドからSDLイベントを積み、メインスレッドの描画待ちを解除します。
/// </summary>
void VideoPlayer::NotifyFrameReady() const
{
	if (frameEventType_ == 0 || frameEventType_ == static_cast<Uint32>(-1))
	{
		return;
	}

	SDL_Event event{};
	event.type = frameEventType_;
	SDL_PushEvent(&event);
}
