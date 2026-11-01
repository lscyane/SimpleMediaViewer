#define NOMINMAX
#include "ImagePlayer.h"

#include <SDL3_image/SDL_image.h>
#include <Windows.h>

#include <algorithm>
#include <array>
#include <cwctype>
#include <fstream>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>
#include "CONST.hpp"

namespace
{
	/// <summary>
	/// WindowsのパスをSDL用のUTF-8文字列へ変換します。
	/// </summary>
	std::string ToUtf8(const std::wstring& path)
	{
		const int requiredLength = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, nullptr, 0, nullptr, nullptr);
		if (requiredLength <= 1)
		{
			return {};
		}

		std::string utf8Path(static_cast<size_t>(requiredLength), '\0');
		if (WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, utf8Path.data(), requiredLength, nullptr, nullptr) == 0)
		{
			return {};
		}
		utf8Path.resize(static_cast<size_t>(requiredLength - 1));
		return utf8Path;
	}


	/// <summary>
	/// パスの拡張子がアニメーション対応形式かを返します。
	/// </summary>
	bool SupportsAnimationDecoder(const std::wstring& path)
	{
		std::wstring extension = std::filesystem::path(path).extension().wstring();
		std::transform(extension.begin(), extension.end(), extension.begin(), towlower);
		return extension == L".gif" || extension == L".webp" || extension == L".png" || extension == L".apng";
	}


	/// <summary>
	/// パスがPNG系のアニメーション形式としてデコードされるべきかを返します。
	/// </summary>
	bool IsApngPath(const std::wstring& path)
	{
		std::wstring extension = std::filesystem::path(path).extension().wstring();
		std::transform(extension.begin(), extension.end(), extension.begin(), towlower);
		return extension == L".png" || extension == L".apng";
	}


	/// <summary>
	/// PNGの4バイト整数をビッグエンディアンで読み取ります。
	/// </summary>
	Uint32 ReadBigEndian32(const Uint8* bytes)
	{
		return (static_cast<Uint32>(bytes[0]) << 24) |
			(static_cast<Uint32>(bytes[1]) << 16) |
			(static_cast<Uint32>(bytes[2]) << 8) |
			static_cast<Uint32>(bytes[3]);
	}


	/// <summary>
	/// PNGの4バイト整数をビッグエンディアンで書き込みます。
	/// </summary>
	void WriteBigEndian32(Uint8* bytes, Uint32 value)
	{
		bytes[0] = static_cast<Uint8>(value >> 24);
		bytes[1] = static_cast<Uint8>(value >> 16);
		bytes[2] = static_cast<Uint8>(value >> 8);
		bytes[3] = static_cast<Uint8>(value);
	}


	/// <summary>
	/// PNGチャンク用CRC-32テーブルを返します。
	/// </summary>
	const std::array<Uint32, 256>& GetPngCrcTable()
	{
		static const std::array<Uint32, 256> table = []
		{
			std::array<Uint32, 256> result{};
			for (size_t index = 0; index < result.size(); ++index)
			{
				Uint32 value = static_cast<Uint32>(index);
				for (int bit = 0; bit < 8; ++bit)
				{
					value = (value & 1u) != 0 ? 0xedb88320u ^ (value >> 1) : value >> 1;
				}
				result[index] = value;
			}
			return result;
		}();
		return table;
	}


	/// <summary>
	/// PNGチャンクの種類とデータからCRC-32を計算します。
	/// </summary>
	Uint32 CalculatePngCrc(const Uint8* type, const Uint8* data, size_t dataSize)
	{
		const std::array<Uint32, 256>& table = GetPngCrcTable();
		Uint32 crc = 0xffffffffu;
		for (size_t index = 0; index < 4; ++index)
		{
			crc = table[(crc ^ type[index]) & 0xffu] ^ (crc >> 8);
		}
		for (size_t index = 0; index < dataSize; ++index)
		{
			crc = table[(crc ^ data[index]) & 0xffu] ^ (crc >> 8);
		}
		return crc ^ 0xffffffffu;
	}


	/// <summary>
	/// 指定された種類とデータでCRC付きPNGチャンクを追加します。
	/// </summary>
	bool AppendPngChunk(std::vector<Uint8>& output, const Uint8* type, const Uint8* data, size_t dataSize)
	{
		if (dataSize > std::numeric_limits<Uint32>::max())
		{
			return false;
		}

		const size_t chunkStart = output.size();
		output.resize(chunkStart + 4);
		WriteBigEndian32(output.data() + chunkStart, static_cast<Uint32>(dataSize));
		output.insert(output.end(), type, type + 4);
		output.insert(output.end(), data, data + dataSize);
		const Uint32 crc = CalculatePngCrc(type, data, dataSize);
		const size_t crcOffset = output.size();
		output.resize(crcOffset + 4);
		WriteBigEndian32(output.data() + crcOffset, crc);
		return true;
	}


	/// <summary>
	/// 1フレームに分割されたfdATチャンクを結合し、必要な場合だけAPNGを再構成します。
	/// </summary>
	bool TryCoalesceApngFrameData(const std::wstring& path, std::vector<Uint8>& normalizedData)
	{
		std::ifstream scan{ std::filesystem::path(path), std::ios::binary };
		if (!scan)
		{
			return false;
		}

		static constexpr std::array<Uint8, 8> PNG_SIGNATURE{ 0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a };
		std::array<Uint8, PNG_SIGNATURE.size()> signature{};
		scan.read(reinterpret_cast<char*>(signature.data()), static_cast<std::streamsize>(signature.size()));
		if (!scan || signature != PNG_SIGNATURE)
		{
			return false;
		}

		const Uint8* const fdatType = reinterpret_cast<const Uint8*>("fdAT");
		const Uint8* const fctlType = reinterpret_cast<const Uint8*>("fcTL");
		const Uint8* const iendType = reinterpret_cast<const Uint8*>("IEND");
		size_t frameDataChunkCount = 0;
		bool hasFragmentedFrameData = false;
		bool hasEndChunk = false;
		while (scan)
		{
			std::array<Uint8, 8> chunkHeader{};
			scan.read(reinterpret_cast<char*>(chunkHeader.data()), static_cast<std::streamsize>(chunkHeader.size()));
			if (scan.gcount() != static_cast<std::streamsize>(chunkHeader.size()))
			{
				return false;
			}

			const Uint32 dataSize = ReadBigEndian32(chunkHeader.data());
			const Uint8* type = chunkHeader.data() + 4;
			if (std::equal(type, type + 4, fctlType))
			{
				hasFragmentedFrameData = hasFragmentedFrameData || frameDataChunkCount > 1;
				frameDataChunkCount = 0;
			}
			else if (std::equal(type, type + 4, fdatType))
			{
				if (dataSize < 4)
				{
					return false;
				}
				++frameDataChunkCount;
			}

			const bool isEndChunk = std::equal(type, type + 4, iendType);
			scan.seekg(static_cast<std::streamoff>(dataSize) + 4, std::ios::cur);
			if (!scan)
			{
				return false;
			}
			if (isEndChunk)
			{
				hasFragmentedFrameData = hasFragmentedFrameData || frameDataChunkCount > 1;
				hasEndChunk = true;
				break;
			}
		}
		if (!hasEndChunk || !hasFragmentedFrameData)
		{
			return false;
		}

		std::ifstream file{ std::filesystem::path(path), std::ios::binary | std::ios::ate };
		if (!file)
		{
			return false;
		}

		const std::streamoff fileSize = file.tellg();
		if (fileSize < 8 || static_cast<unsigned long long>(fileSize) > std::numeric_limits<size_t>::max() ||
			fileSize > std::numeric_limits<std::streamsize>::max())
		{
			return false;
		}

		std::vector<Uint8> source(static_cast<size_t>(fileSize));
		file.seekg(0, std::ios::beg);
		file.read(reinterpret_cast<char*>(source.data()), static_cast<std::streamsize>(source.size()));
		if (!file)
		{
			return false;
		}

		if (!std::equal(PNG_SIGNATURE.begin(), PNG_SIGNATURE.end(), source.begin()))
		{
			return false;
		}

		size_t offset = PNG_SIGNATURE.size();
		normalizedData.assign(PNG_SIGNATURE.begin(), PNG_SIGNATURE.end());
		normalizedData.reserve(source.size());
		std::vector<Uint8> frameData;
		Uint64 sequenceNumber = 0;
		bool hasFrameData = false;
		auto flushFrameData = [&]()
		{
			if (!hasFrameData || sequenceNumber > std::numeric_limits<Uint32>::max() ||
				frameData.size() > std::numeric_limits<Uint32>::max() - 4ull)
			{
				return !hasFrameData;
			}

			std::vector<Uint8> payload;
			payload.reserve(frameData.size() + 4);
			const size_t sequenceOffset = payload.size();
			payload.resize(sequenceOffset + 4);
			WriteBigEndian32(payload.data() + sequenceOffset, static_cast<Uint32>(sequenceNumber++));
			payload.insert(payload.end(), frameData.begin(), frameData.end());
			const bool appended = AppendPngChunk(normalizedData, fdatType, payload.data(), payload.size());
			frameData.clear();
			hasFrameData = false;
			return appended;
		};

		offset = PNG_SIGNATURE.size();
		while (offset <= source.size() && source.size() - offset >= 12)
		{
			const Uint32 dataSize = ReadBigEndian32(source.data() + offset);
			const Uint8* type = source.data() + offset + 4;
			const Uint8* payload = source.data() + offset + 8;
			if (std::equal(type, type + 4, fdatType))
			{
				frameData.insert(frameData.end(), payload + 4, payload + dataSize);
				hasFrameData = true;
				offset += static_cast<size_t>(dataSize) + 12;
				continue;
			}

			if (!flushFrameData())
			{
				normalizedData.clear();
				return false;
			}
			if (std::equal(type, type + 4, fctlType))
			{
				if (dataSize < 4 || sequenceNumber > std::numeric_limits<Uint32>::max())
				{
					normalizedData.clear();
					return false;
				}
				std::vector<Uint8> fctlData(payload, payload + dataSize);
				WriteBigEndian32(fctlData.data(), static_cast<Uint32>(sequenceNumber++));
				if (!AppendPngChunk(normalizedData, type, fctlData.data(), fctlData.size()))
				{
					normalizedData.clear();
					return false;
				}
			}
			else
			{
				const size_t chunkEnd = offset + static_cast<size_t>(dataSize) + 12;
				normalizedData.insert(normalizedData.end(), source.begin() + offset, source.begin() + chunkEnd);
			}

			const bool isEndChunk = std::equal(type, type + 4, iendType);
			offset += static_cast<size_t>(dataSize) + 12;
			if (isEndChunk)
			{
				break;
			}
		}

		if (!flushFrameData())
		{
			normalizedData.clear();
			return false;
		}
		return true;
	}


	/// <summary>
	/// 必要に応じてAPNG形式を明示してアニメーションデコーダーを作成します。
	/// </summary>
	IMG_AnimationDecoder* CreateAnimationDecoder(
		const std::string& utf8Path,
		const std::wstring& path,
		bool isApng,
		std::vector<Uint8>& animationData)
	{
		if (!isApng)
		{
			return IMG_CreateAnimationDecoder(utf8Path.c_str());
		}
		if (TryCoalesceApngFrameData(path, animationData))
		{
			SDL_IOStream* source = SDL_IOFromConstMem(animationData.data(), animationData.size());
			if (source == nullptr)
			{
				std::vector<Uint8>().swap(animationData);
				return nullptr;
			}

			IMG_AnimationDecoder* decoder = IMG_CreateAnimationDecoder_IO(source, true, "APNG");
			if (decoder == nullptr)
			{
				std::vector<Uint8>().swap(animationData);
			}
			return decoder;
		}

		const SDL_PropertiesID properties = SDL_CreateProperties();
		if (properties == 0)
		{
			return nullptr;
		}

		IMG_AnimationDecoder* decoder = nullptr;
		const bool hasFilename = SDL_SetStringProperty(
			properties,
			IMG_PROP_ANIMATION_DECODER_CREATE_FILENAME_STRING,
			utf8Path.c_str());
		const bool hasType = SDL_SetStringProperty(
			properties,
			IMG_PROP_ANIMATION_DECODER_CREATE_TYPE_STRING,
			"APNG");
		if (hasFilename && hasType)
		{
			decoder = IMG_CreateAnimationDecoderWithProperties(properties);
		}
		SDL_DestroyProperties(properties);
		return decoder;
	}
}


/// <summary>
/// 画像パスをUTF-8化し、アニメーション画像または静止画をGPUテクスチャとして読み込みます。
/// </summary>
bool ImagePlayer::Load(SDL_Renderer* renderer, const std::wstring& path)
{
	Clear();

	const std::string utf8Path = ToUtf8(path);
	if (utf8Path.empty())
	{
		return false;
	}

	if (SupportsAnimationDecoder(path) && LoadAnimation(renderer, utf8Path, path, IsApngPath(path)))
	{
		return true;
	}

	texture_ = IMG_LoadTexture(renderer, utf8Path.c_str());
	if (texture_ == nullptr)
	{
		return false;
	}
	SDL_SetTextureScaleMode(
		texture_,
		USE_NEAREST_NEIGHBOR_IMAGE_SCALING ? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_LINEAR);

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


/// <summary>
/// SDL3_imageのアニメーションデコーダーからフレームを読み込みます。
/// </summary>
bool ImagePlayer::LoadAnimation(SDL_Renderer* renderer, const std::string& utf8Path, const std::wstring& path, bool isApng)
{
	animationDecoder_ = CreateAnimationDecoder(utf8Path, path, isApng, animationData_);
	if (animationDecoder_ == nullptr)
	{
		std::vector<Uint8>().swap(animationData_);
		return false;
	}

	const SDL_PropertiesID properties = IMG_GetAnimationDecoderProperties(animationDecoder_);
	const Sint64 expectedFrameCount = properties != 0
		? SDL_GetNumberProperty(properties, IMG_PROP_METADATA_FRAME_COUNT_NUMBER, 0)
		: 0;
	loopCount_ = HONOR_ANIMATION_LOOP_COUNT && properties != 0
		? SDL_GetNumberProperty(properties, IMG_PROP_METADATA_LOOP_COUNT_NUMBER, 0)
		: 0;
	if (loopCount_ < 0)
	{
		loopCount_ = 0;
	}

	SDL_Surface* firstFrame = nullptr;
	Uint64 firstFrameDuration = 0;
	if (!IMG_GetAnimationDecoderFrame(animationDecoder_, &firstFrame, &firstFrameDuration) || firstFrame == nullptr ||
		!SetTextureFromSurface(renderer, firstFrame))
	{
		if (firstFrame != nullptr)
		{
			SDL_DestroySurface(firstFrame);
		}
		IMG_CloseAnimationDecoder(animationDecoder_);
		animationDecoder_ = nullptr;
		std::vector<Uint8>().swap(animationData_);
		return false;
	}

	frameDuration_ = std::max<Uint64>(firstFrameDuration, 1);
	completedPlays_ = 1;
	SDL_DestroySurface(firstFrame);

	if (!ReadNextFrame())
	{
		if (decoderComplete_ && expectedFrameCount > 1)
		{
			SDL_LogError(
				SDL_LOG_CATEGORY_APPLICATION,
				"Animation decoder ended after the first frame but reports %lld frames.",
				static_cast<long long>(expectedFrameCount));
		}
		IMG_CloseAnimationDecoder(animationDecoder_);
		animationDecoder_ = nullptr;
		std::vector<Uint8>().swap(animationData_);
		return true;
	}

	renderer_ = renderer;
	isAnimated_ = true;
	isPlaying_ = true;
	nextFrameTime_ = SDL_GetTicks() + frameDuration_;
	return true;
}


/// <summary>
/// デコーダーから次の表示フレームを先読みします。
/// </summary>
bool ImagePlayer::ReadNextFrame()
{
	if (!IMG_GetAnimationDecoderFrame(animationDecoder_, &pendingFrame_, &pendingFrameDuration_))
	{
		const IMG_AnimationDecoderStatus status = IMG_GetAnimationDecoderStatus(animationDecoder_);
		decoderComplete_ = status == IMG_DECODER_STATUS_COMPLETE;
		if (!decoderComplete_)
		{
			SDL_LogError(
				SDL_LOG_CATEGORY_APPLICATION,
				"Animation frame decoding failed (status %d): %s",
				static_cast<int>(status),
				SDL_GetError());
		}
		return false;
	}
	return pendingFrame_ != nullptr;
}


/// <summary>
/// サーフェスから作成したテクスチャで現在のフレームを置き換えます。
/// </summary>
bool ImagePlayer::SetTextureFromSurface(SDL_Renderer* renderer, SDL_Surface* surface)
{
	if (surface == nullptr || surface->w <= 0 || surface->h <= 0)
	{
		return false;
	}

	SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
	if (texture == nullptr)
	{
		return false;
	}
	SDL_SetTextureScaleMode(
		texture,
		USE_NEAREST_NEIGHBOR_IMAGE_SCALING ? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_LINEAR);

	if (texture_ != nullptr)
	{
		SDL_DestroyTexture(texture_);
	}
	texture_ = texture;
	width_ = surface->w;
	height_ = surface->h;
	return true;
}


/// <summary>
/// 表示期限を過ぎたアニメーションのフレームを進めます。
/// </summary>
bool ImagePlayer::AdvanceAnimation()
{
	if (!isPlaying_ || SDL_GetTicks() < nextFrameTime_)
	{
		return false;
	}

	const Uint64 now = SDL_GetTicks();
	if (pendingFrame_ != nullptr)
	{
		if (!SetTextureFromSurface(renderer_, pendingFrame_))
		{
			SDL_DestroySurface(pendingFrame_);
			pendingFrame_ = nullptr;
			isPlaying_ = false;
			return false;
		}
		SDL_DestroySurface(pendingFrame_);
		pendingFrame_ = nullptr;
		frameDuration_ = std::max<Uint64>(pendingFrameDuration_, 1);
		nextFrameTime_ = now + frameDuration_;
		if (ReadNextFrame())
		{
			return true;
		}
		return true;
	}

	if (!decoderComplete_)
	{
		isPlaying_ = false;
		return false;
	}
	if (loopCount_ > 0 && completedPlays_ >= loopCount_)
	{
		isPlaying_ = false;
		isEnded_ = true;
		return false;
	}
	if (!IMG_ResetAnimationDecoder(animationDecoder_))
	{
		isPlaying_ = false;
		return false;
	}

	SDL_Surface* firstFrame = nullptr;
	Uint64 firstFrameDuration = 0;
	if (!IMG_GetAnimationDecoderFrame(animationDecoder_, &firstFrame, &firstFrameDuration) || firstFrame == nullptr)
	{
		isPlaying_ = false;
		return false;
	}
	if (!SetTextureFromSurface(renderer_, firstFrame))
	{
		SDL_DestroySurface(firstFrame);
		isPlaying_ = false;
		return false;
	}
	SDL_DestroySurface(firstFrame);
	++completedPlays_;
	frameDuration_ = std::max<Uint64>(firstFrameDuration, 1);
	nextFrameTime_ = now + frameDuration_;
	decoderComplete_ = false;
	ReadNextFrame();
	return true;
}


/// <summary>
/// アニメーション画像の再生状態を切り替えます。
/// </summary>
void ImagePlayer::TogglePause() noexcept
{
	if (animationDecoder_ == nullptr || isEnded_)
	{
		return;
	}

	if (isPlaying_)
	{
		const Uint64 now = SDL_GetTicks();
		remainingFrameTime_ = nextFrameTime_ > now ? nextFrameTime_ - now : 0;
		isPlaying_ = false;
	}
	else
	{
		nextFrameTime_ = SDL_GetTicks() + remainingFrameTime_;
		isPlaying_ = true;
	}
}


/// <summary>
/// 複数フレームのアニメーションを読み込んでいるかを返します。
/// </summary>
bool ImagePlayer::IsAnimated() const noexcept
{
	return isAnimated_;
}


/// <summary>
/// アニメーション画像を再生中かを返します。
/// </summary>
bool ImagePlayer::IsPlaying() const noexcept
{
	return isPlaying_;
}


/// <summary>
/// 次のフレーム切り替えまでの実時間をミリ秒で返します。
/// </summary>
int ImagePlayer::MillisecondsUntilNextFrame() const noexcept
{
	if (!isPlaying_)
	{
		return -1;
	}

	const Uint64 now = SDL_GetTicks();
	if (nextFrameTime_ <= now)
	{
		return 0;
	}
	return static_cast<int>(std::min<Uint64>(nextFrameTime_ - now, static_cast<Uint64>(std::numeric_limits<int>::max())));
}


/// <summary>
/// 保持中のSDL画像テクスチャを破棄し、サイズ情報を初期化します。
/// </summary>
void ImagePlayer::Clear()
{
	if (pendingFrame_ != nullptr)
	{
		SDL_DestroySurface(pendingFrame_);
		pendingFrame_ = nullptr;
	}
	if (animationDecoder_ != nullptr)
	{
		IMG_CloseAnimationDecoder(animationDecoder_);
		animationDecoder_ = nullptr;
	}
	std::vector<Uint8>().swap(animationData_);
	if (texture_ != nullptr)
	{
		SDL_DestroyTexture(texture_);
		texture_ = nullptr;
	}
	renderer_ = nullptr;
	width_ = 0;
	height_ = 0;
	frameDuration_ = 0;
	pendingFrameDuration_ = 0;
	nextFrameTime_ = 0;
	remainingFrameTime_ = 0;
	loopCount_ = 0;
	completedPlays_ = 0;
	isPlaying_ = false;
	isAnimated_ = false;
	isEnded_ = false;
	decoderComplete_ = false;
}


/// <summary>
/// 画像テクスチャが読み込まれているかを返します。
/// </summary>
bool ImagePlayer::IsLoaded() const noexcept
{
	return texture_ != nullptr;
}


/// <summary>
/// 画像のピクセル幅を返します。
/// </summary>
int ImagePlayer::Width() const noexcept
{
	return width_;
}


/// <summary>
/// 画像のピクセル高さを返します。
/// </summary>
int ImagePlayer::Height() const noexcept
{
	return height_;
}


/// <summary>
/// 指定された変換先矩形に画像を描画します。
/// </summary>
void ImagePlayer::Render(SDL_Renderer* renderer, const SDL_FRect& destination) const
{
	if (texture_ != nullptr)
	{
		SDL_RenderTexture(renderer, texture_, nullptr, &destination);
	}
}
