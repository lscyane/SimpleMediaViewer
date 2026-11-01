#define NOMINMAX
#include "ImagePlayer.h"

#include <SDL3_image/SDL_image.h>
#include <Windows.h>
#include <avif/avif.h>

#pragma comment(lib, "avif.lib")

#include <algorithm>
#include <array>
#include <cmath>
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
	/// AVIFのRGBフレームをRGBA SDLサーフェスに変換します。
	/// </summary>
	SDL_Surface* CreateAvifSurface(const avifImage* image)
	{
		// AVIFの幅と高さがSDL_Surfaceの範囲内に収まるかを確認します。
		if (image == nullptr || image->width == 0 || image->height == 0
		 || image->width > static_cast<uint32_t>(std::numeric_limits<int>::max())
		 ||	image->height > static_cast<uint32_t>(std::numeric_limits<int>::max())
		) {
			return nullptr;
		}

		// SDL_SurfaceをRGBA32形式で作成します。
		SDL_Surface* surface = SDL_CreateSurface(static_cast<int>(image->width), static_cast<int>(image->height), SDL_PIXELFORMAT_RGBA32);
		if (surface == nullptr)
		{
			return nullptr;
		}

		// AVIFのYUV画像をRGBAに変換します。
		avifRGBImage rgbImage{};
		avifRGBImageSetDefaults(&rgbImage, image);
		rgbImage.format = AVIF_RGB_FORMAT_RGBA;
		rgbImage.depth = 8;
		rgbImage.pixels = static_cast<uint8_t*>(surface->pixels);
		rgbImage.rowBytes = static_cast<uint32_t>(surface->pitch);
		if (!SDL_LockSurface(surface))
		{
			SDL_DestroySurface(surface);
			return nullptr;
		}

		// YUVからRGBへの変換を行い、結果をSDL_Surfaceに書き込みます。
		const avifResult result = avifImageYUVToRGB(image, &rgbImage);
		SDL_UnlockSurface(surface);
		if (result != AVIF_RESULT_OK)
		{
			SDL_DestroySurface(surface);
			return nullptr;
		}
		return surface;
	}


	/// <summary>
	/// AVIFの現在フレーム表示時間をミリ秒で返します。
	/// </summary>
	Uint64 GetAvifFrameDuration(const avifDecoder* decoder)
	{
		const double milliseconds = decoder->imageTiming.duration * 1000.0;
		if (!std::isfinite(milliseconds) || milliseconds <= 1.0)
		{
			return 1;
		}
		return static_cast<Uint64>(std::min(
			milliseconds,
			static_cast<double>(std::numeric_limits<int>::max())));
	}


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
		return std::find(ANIMATION_DECODER_EXTENSIONS.begin(), ANIMATION_DECODER_EXTENSIONS.end(), extension) != ANIMATION_DECODER_EXTENSIONS.end();
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
	/// パスがAVIF形式かを返します。
	/// </summary>
	bool IsAvifPath(const std::wstring& path)
	{
		std::wstring extension = std::filesystem::path(path).extension().wstring();
		std::transform(extension.begin(), extension.end(), extension.begin(), towlower);
		return extension == L".avif";
	}


	/// <summary>
	/// PNGの4バイト整数をビッグエンディアンで読み取ります。
	/// </summary>
	Uint32 ReadBigEndian32(const Uint8* bytes)
	{
		return 
		   (static_cast<Uint32>(bytes[0]) << 24)
		 | (static_cast<Uint32>(bytes[1]) << 16)
		 | (static_cast<Uint32>(bytes[2]) << 8)
		 | (static_cast<Uint32>(bytes[3]));
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
		constexpr size_t tableSize = 256;
		static const std::array<Uint32, tableSize> table = []
		{
			std::array<Uint32, tableSize> result{};
			size_t index = 0;
			for (Uint32& tableEntry : result)
			{
				Uint32 value = static_cast<Uint32>(index);
				for (int bit = 0; bit < 8; ++bit)
				{
					value = (value & 1u) != 0 ? 0xedb88320u ^ (value >> 1) : value >> 1;
				}
				tableEntry = value;
				++index;
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
		// fdATチャンクが複数フレームに分割されているかを確認します。
		std::ifstream scan{ std::filesystem::path(path), std::ios::binary };
		if (!scan)
		{
			return false;
		}

		// PNGシグネチャを確認します。
		static constexpr std::array<Uint8, 8> PNG_SIGNATURE{ 0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a };
		std::array<Uint8, PNG_SIGNATURE.size()> signature{};
		scan.read(reinterpret_cast<char*>(signature.data()), static_cast<std::streamsize>(signature.size()));
		if (!scan || signature != PNG_SIGNATURE)
		{
			return false;
		}

		// PNGチャンクをスキャンして、fdATチャンクが複数フレームに分割されているかを確認します。
		const Uint8* const fdatType = reinterpret_cast<const Uint8*>("fdAT");
		const Uint8* const fctlType = reinterpret_cast<const Uint8*>("fcTL");
		const Uint8* const iendType = reinterpret_cast<const Uint8*>("IEND");
		size_t frameDataChunkCount = 0;
		bool hasFragmentedFrameData = false;
		bool hasEndChunk = false;
		while (scan)
		{
			// チャンクのヘッダー（データサイズと種類）を読み取ります。
			std::array<Uint8, 8> chunkHeader{};
			scan.read(reinterpret_cast<char*>(chunkHeader.data()), static_cast<std::streamsize>(chunkHeader.size()));
			if (scan.gcount() != static_cast<std::streamsize>(chunkHeader.size()))
			{
				return false;
			}

			// チャンクのデータサイズと種類を読み取ります。
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

			// IENDチャンクに到達したらループを終了します。
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

		// fdATチャンクを結合してAPNGを再構成します。
		std::ifstream file{ std::filesystem::path(path), std::ios::binary | std::ios::ate };
		if (!file)
		{
			return false;
		}

		// ファイルサイズを確認します。
		const std::streamoff fileSize = file.tellg();
		if (fileSize < 8 || static_cast<unsigned long long>(fileSize) > std::numeric_limits<size_t>::max() ||
			fileSize > std::numeric_limits<std::streamsize>::max())
		{
			return false;
		}

		//ファイル全体をメモリに読み込みます。
		std::vector<Uint8> source(static_cast<size_t>(fileSize));
		file.seekg(0, std::ios::beg);
		file.read(reinterpret_cast<char*>(source.data()), static_cast<std::streamsize>(source.size()));
		if (!file)
		{
			return false;
		}

		// PNGシグネチャを確認します。
		if (!std::equal(PNG_SIGNATURE.begin(), PNG_SIGNATURE.end(), source.begin()))
		{
			return false;
		}

		// fdATチャンクを結合してAPNGを再構成します。
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

		// fdATチャンクを結合してAPNGを再構成します。
		offset = PNG_SIGNATURE.size();
		while (offset <= source.size() && source.size() - offset >= 12)
		{
			// チャンクのデータサイズ、種類、ペイロードを読み取ります。
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

			// fdATチャンク以外のチャンクが出現した場合、残っているフレームデータをfdATチャンクとして書き込みます。
			if (!flushFrameData())
			{
				normalizedData.clear();
				return false;
			}
			if (std::equal(type, type + 4, fctlType))
			{
				// fcTLチャンクのシーケンス番号をfdATチャンクに書き込みます。
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
				// fdATチャンク以外のチャンクはそのままコピーします。
				const size_t chunkEnd = offset + static_cast<size_t>(dataSize) + 12;
				normalizedData.insert(normalizedData.end(), source.begin() + offset, source.begin() + chunkEnd);
			}

			// IENDチャンクに到達したらループを終了します。
			const bool isEndChunk = std::equal(type, type + 4, iendType);
			offset += static_cast<size_t>(dataSize) + 12;
			if (isEndChunk)
			{
				break;
			}
		}

		// 最後のフレームデータが残っている場合はfdATチャンクとして追加します。
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
	IMG_AnimationDecoder* CreateAnimationDecoder(const std::string& utf8Path, const std::wstring& path, bool isApng, std::vector<Uint8>& animationData)
	{
		// APNG形式を明示してアニメーションデコーダーを作成します。
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

		// APNG形式を明示してアニメーションデコーダーを作成します。
		const SDL_PropertiesID properties = SDL_CreateProperties();
		if (properties == 0)
		{
			return nullptr;
		}

		// SDL3_imageのアニメーションデコーダーを作成します。
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

	// パスをUTF-8に変換します。
	const std::string utf8Path = ToUtf8(path);
	if (utf8Path.empty())
	{
		return false;
	}

	// AVIF形式のアニメーションを読み込みます。
	if (IsAvifPath(path) && LoadAvifAnimation(renderer, path))
	{
		return true;
	}

	// アニメーション対応形式の画像を読み込みます。
	if (SupportsAnimationDecoder(path) && LoadAnimation(renderer, utf8Path, path, IsApngPath(path)))
	{
		return true;
	}

	// 静止画をGPUテクスチャとして読み込みます。
	texture_ = IMG_LoadTexture(renderer, utf8Path.c_str());
	if (texture_ == nullptr)
	{
		return false;
	}
	SDL_SetTextureScaleMode(texture_, USE_NEAREST_NEIGHBOR_IMAGE_SCALING ? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_LINEAR);

	// テクスチャの幅と高さを取得します。
	float width = 0.0f;
	float height = 0.0f;
	if (!SDL_GetTextureSize(texture_, &width, &height) || width <= 0.0f || height <= 0.0f)
	{
		Clear();
		return false;
	}

	// 幅と高さを整数に変換して保持します。
	width_ = static_cast<int>(width);
	height_ = static_cast<int>(height);
	return true;
}


/// <summary>
/// SDL3_imageのアニメーションデコーダーからフレームを読み込みます。
/// </summary>
bool ImagePlayer::LoadAnimation(SDL_Renderer* renderer, const std::string& utf8Path, const std::wstring& path, bool isApng)
{
	// SDL3_imageのアニメーションデコーダーを作成します。
	animationDecoder_ = CreateAnimationDecoder(utf8Path, path, isApng, animationData_);
	if (animationDecoder_ == nullptr)
	{
		std::vector<Uint8>().swap(animationData_);
		return false;
	}

	// アニメーションのフレーム数とループ回数を取得します。
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

	// 最初のフレームを読み込み、テクスチャとして設定します。
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

	// 最初のフレームの表示時間を設定し、次のフレームを先読みします。
	frameDuration_ = std::max<Uint64>(firstFrameDuration, 1);
	completedPlays_ = 1;
	SDL_DestroySurface(firstFrame);
	
	// 次のフレームを読み込みます。	
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

	// アニメーションの再生状態を初期化します。
	renderer_ = renderer;
	isAnimated_ = true;
	isPlaying_ = true;
	nextFrameTime_ = SDL_GetTicks() + frameDuration_;
	return true;
}


/// <summary>
/// libavifでアニメーションAVIFを読み込み、最初のフレームを表示します。
/// </summary>
bool ImagePlayer::LoadAvifAnimation(SDL_Renderer* renderer, const std::wstring& path)
{
	// AVIFのバイナリデータをファイルから読み込みます。
	std::ifstream file{ std::filesystem::path(path), std::ios::binary | std::ios::ate };
	if (!file)
	{
		return false;
	}

	// ファイルサイズを確認し、バッファに読み込みます。
	const std::streamoff fileSize = file.tellg();
	if (fileSize <= 0 || static_cast<unsigned long long>(fileSize) > std::numeric_limits<size_t>::max() ||
		fileSize > std::numeric_limits<std::streamsize>::max())
	{
		return false;
	}

	// AVIFデータをバッファに読み込みます。
	avifData_.resize(static_cast<size_t>(fileSize));
	file.seekg(0, std::ios::beg);
	file.read(reinterpret_cast<char*>(avifData_.data()), static_cast<std::streamsize>(avifData_.size()));
	if (!file)
	{
		std::vector<Uint8>().swap(avifData_);
		return false;
	}

	// libavifのデコーダーを作成し、AVIFデータを解析します。
	avifDecoder_ = avifDecoderCreate();
	if ((avifDecoder_ == nullptr)
	 || (avifDecoderSetIOMemory(avifDecoder_, avifData_.data(), avifData_.size()) != AVIF_RESULT_OK)
	 || (avifDecoderParse(avifDecoder_) != AVIF_RESULT_OK)
	) {
		if (avifDecoder_ != nullptr)
		{
			avifDecoderDestroy(avifDecoder_);
			avifDecoder_ = nullptr;
		}
		std::vector<Uint8>().swap(avifData_);
		return false;
	}

	// アニメーションフレームが1つしかない場合は、デコーダーを破棄して終了します。
	if (avifDecoder_->imageCount <= 1)
	{
		avifDecoderDestroy(avifDecoder_);
		avifDecoder_ = nullptr;
		std::vector<Uint8>().swap(avifData_);
		return false;
	}

	// アニメーションのフレーム数とループ回数を取得します。
	avifFrameCount_ = avifDecoder_->imageCount;
	if (HONOR_ANIMATION_LOOP_COUNT && avifDecoder_->repetitionCount >= 0)
	{
		loopCount_ = static_cast<Sint64>(avifDecoder_->repetitionCount) + 1;
	}
	else
	{
		loopCount_ = 0;
	}

	// 最初のフレームを読み込み、テクスチャとして設定します。
	if (avifDecoderNextImage(avifDecoder_) != AVIF_RESULT_OK)
	{
		avifDecoderDestroy(avifDecoder_);
		avifDecoder_ = nullptr;
		std::vector<Uint8>().swap(avifData_);
		return false;
	}

	// 最初のフレームをSDL_Surfaceに変換し、テクスチャとして設定します。
	SDL_Surface* firstFrame = CreateAvifSurface(avifDecoder_->image);
	if (firstFrame == nullptr || !SetTextureFromSurface(renderer, firstFrame))
	{
		if (firstFrame != nullptr)
		{
			SDL_DestroySurface(firstFrame);
		}
		avifDecoderDestroy(avifDecoder_);
		avifDecoder_ = nullptr;
		std::vector<Uint8>().swap(avifData_);
		return false;
	}
	SDL_DestroySurface(firstFrame);

	// 最初のフレームの表示時間を取得し、次のフレームを先読みします。
	avifFramesDecoded_ = 1;
	frameDuration_ = GetAvifFrameDuration(avifDecoder_);
	completedPlays_ = 1;
	if (!ReadNextAvifFrame())
	{
		avifDecoderDestroy(avifDecoder_);
		avifDecoder_ = nullptr;
		std::vector<Uint8>().swap(avifData_);
		return true;
	}

	// アニメーションの再生状態を初期化します。
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
	// SDL3_imageのアニメーションデコーダーから次のフレームを読み込みます。
	if (avifDecoder_ != nullptr)
	{
		return ReadNextAvifFrame();
	}

	// SDL3_imageのアニメーションデコーダーから次のフレームを読み込みます。
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
/// libavifから次のAVIFフレームと表示時間を読み込みます。
/// </summary>
bool ImagePlayer::ReadNextAvifFrame()
{
	// AVIFデコーダーが存在しないか、すでにすべてのフレームを読み込んでいる場合は終了します。
	if (avifDecoder_ == nullptr || avifFramesDecoded_ >= avifFrameCount_)
	{
		decoderComplete_ = true;
		return false;
	}

	// 次のAVIFフレームを読み込みます。
	if (avifDecoderNextImage(avifDecoder_) != AVIF_RESULT_OK)
	{
		SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "AVIF frame decoding failed: %s", avifDecoder_->diag.error);
		return false;
	}

	// 読み込んだAVIFフレームをSDL_Surfaceに変換します。
	pendingFrame_ = CreateAvifSurface(avifDecoder_->image);
	if (pendingFrame_ == nullptr)
	{
		SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "AVIF frame conversion failed.");
		return false;
	}
	++avifFramesDecoded_;
	pendingFrameDuration_ = GetAvifFrameDuration(avifDecoder_);
	return true;
}


/// <summary>
/// サーフェスから作成したテクスチャで現在のフレームを置き換えます。
/// </summary>
bool ImagePlayer::SetTextureFromSurface(SDL_Renderer* renderer, SDL_Surface* surface)
{
	// サーフェスが有効でない場合は失敗します。
	if (surface == nullptr || surface->w <= 0 || surface->h <= 0)
	{
		return false;
	}

	// サーフェスからテクスチャを作成します。
	SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
	if (texture == nullptr)
	{
		return false;
	}
	SDL_SetTextureScaleMode(texture, USE_NEAREST_NEIGHBOR_IMAGE_SCALING ? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_LINEAR);

	// 既存のテクスチャを破棄し、新しいテクスチャとサイズ情報を保持します。
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
	// アニメーションが再生中でない場合、または次のフレーム表示時間に達していない場合は終了します。
	if (!isPlaying_ || SDL_GetTicks() < nextFrameTime_)
	{
		return false;
	}

	// 現在のフレームを破棄し、次のフレームを読み込みます。
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

	// 次のフレームが読み込めなかった場合、デコーダーが完了しているか、ループ回数を超えているかを確認します。
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
	if (avifDecoder_ != nullptr)
	{
		if (avifDecoderReset(avifDecoder_) != AVIF_RESULT_OK)
		{
			isPlaying_ = false;
			return false;
		}
		avifFramesDecoded_ = 0;
		decoderComplete_ = false;
		if (!ReadNextAvifFrame() || pendingFrame_ == nullptr || !SetTextureFromSurface(renderer_, pendingFrame_))
		{
			if (pendingFrame_ != nullptr)
			{
				SDL_DestroySurface(pendingFrame_);
				pendingFrame_ = nullptr;
			}
			isPlaying_ = false;
			return false;
		}
		SDL_DestroySurface(pendingFrame_);
		pendingFrame_ = nullptr;
		++completedPlays_;
		frameDuration_ = std::max<Uint64>(pendingFrameDuration_, 1);
		nextFrameTime_ = now + frameDuration_;
		ReadNextAvifFrame();
		return true;
	}
	if (!IMG_ResetAnimationDecoder(animationDecoder_))
	{
		isPlaying_ = false;
		return false;
	}

	// 最初のフレームを再度読み込み、テクスチャとして設定します。
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
	// アニメーションデコーダーが存在しない場合、またはアニメーションが終了している場合は何もしません。
	if ((animationDecoder_ == nullptr && avifDecoder_ == nullptr) || isEnded_)
	{
		return;
	}

	// 再生中の場合は残りのフレーム表示時間を計算し、再生を一時停止します。
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
	if (avifDecoder_ != nullptr)
	{
		avifDecoderDestroy(avifDecoder_);
		avifDecoder_ = nullptr;
	}
	std::vector<Uint8>().swap(animationData_);
	std::vector<Uint8>().swap(avifData_);
	if (texture_ != nullptr)
	{
		SDL_DestroyTexture(texture_);
		texture_ = nullptr;
	}
	renderer_ = nullptr;
	width_ = 0;
	height_ = 0;
	avifFrameCount_ = 0;
	avifFramesDecoded_ = 0;
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
