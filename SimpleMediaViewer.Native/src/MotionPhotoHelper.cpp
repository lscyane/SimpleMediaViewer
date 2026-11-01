#define NOMINMAX
#include "MotionPhotoHelper.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>

namespace
{
	constexpr std::size_t XMP_HEADER_LIMIT = 131072;
	constexpr std::size_t COPY_BUFFER_SIZE = 64 * 1024;


	/// <summary>
	/// XMP属性から引用符で囲まれた符号なし整数を読み取ります。
	/// </summary>
	std::optional<std::uint64_t> ParseXmpAttribute(std::string_view text, std::string_view name)
	{
		std::size_t searchPosition = 0;
		while ((searchPosition = text.find(name, searchPosition)) != std::string_view::npos)
		{
			std::size_t position = searchPosition + name.size();
			while (position < text.size() && std::isspace(static_cast<unsigned char>(text[position])))
			{
				++position;
			}
			if (position >= text.size() || text[position] != '=')
			{
				searchPosition += name.size();
				continue;
			}

			++position;
			while (position < text.size() && std::isspace(static_cast<unsigned char>(text[position])))
			{
				++position;
			}
			if (position >= text.size() || (text[position] != '\'' && text[position] != '"'))
			{
				searchPosition += name.size();
				continue;
			}

			const char quote = text[position++];
			const std::size_t valueStart = position;
			while (position < text.size() && text[position] >= '0' && text[position] <= '9')
			{
				++position;
			}
			if (position == valueStart || position >= text.size() || text[position] != quote)
			{
				searchPosition += name.size();
				continue;
			}

			std::uint64_t value = 0;
			const char* const begin = text.data() + valueStart;
			const char* const end = text.data() + position;
			if (std::from_chars(begin, end, value).ec == std::errc{})
			{
				return value;
			}
			searchPosition += name.size();
		}
		return std::nullopt;
	}


	/// <summary>
	/// 従来形式またはContainer形式のXMPから埋め込み動画サイズを取得します。
	/// </summary>
	std::optional<std::uint64_t> FindVideoSize(std::string_view xmp)
	{
		for (const std::string_view attribute : {
			std::string_view("Camera:MotionPhotoVideoFileSize"),
			std::string_view("GCamera:MicroVideoLength") })
		{
			const std::optional<std::uint64_t> size = ParseXmpAttribute(xmp, attribute);
			if (size.has_value() && *size > 0)
			{
				return size;
			}
		}

		std::size_t videoMimePosition = xmp.find("video/mp4");
		while (videoMimePosition != std::string_view::npos)
		{
			const std::size_t itemStart = xmp.rfind("<Container:Item", videoMimePosition);
			const std::size_t itemEnd = xmp.find("/>", videoMimePosition);
			if (itemStart != std::string_view::npos && itemEnd != std::string_view::npos)
			{
				const std::string_view item = xmp.substr(itemStart, itemEnd + 2 - itemStart);
				const std::optional<std::uint64_t> length = ParseXmpAttribute(item, "Item:Length");
				const std::optional<std::uint64_t> padding = ParseXmpAttribute(item, "Item:Padding");
				if (length.has_value() && *length > 0 &&
					(!padding.has_value() || *padding <= std::numeric_limits<std::uint64_t>::max() - *length))
				{
					return *length + padding.value_or(0);
				}
			}
			videoMimePosition = xmp.find("video/mp4", videoMimePosition + 1);
		}
		return std::nullopt;
	}
}


/// <summary>
/// JPEGのXMPメタデータから埋め込み動画の先頭位置を取得します。
/// </summary>
std::optional<std::uint64_t> MotionPhotoHelper::FindEmbeddedVideoOffset(const std::filesystem::path& jpegPath)
{
	std::error_code fileError;
	const std::uintmax_t fileSize = std::filesystem::file_size(jpegPath, fileError);
	if (fileError || fileSize == 0)
	{
		return std::nullopt;
	}

	const std::size_t headerSize = static_cast<std::size_t>(std::min<std::uintmax_t>(fileSize, XMP_HEADER_LIMIT));
	std::ifstream input(jpegPath, std::ios::binary);
	if (!input)
	{
		return std::nullopt;
	}

	std::string header(headerSize, '\0');
	input.read(header.data(), static_cast<std::streamsize>(header.size()));
	if (input.gcount() != static_cast<std::streamsize>(header.size()))
	{
		return std::nullopt;
	}

	const std::optional<std::uint64_t> videoSize = FindVideoSize(header);
	if (!videoSize.has_value() || *videoSize == 0 || *videoSize >= fileSize)
	{
		return std::nullopt;
	}
	return static_cast<std::uint64_t>(fileSize - *videoSize);
}


/// <summary>
/// 指定位置以降の埋め込み動画を一時MP4ファイルへ抽出します。
/// </summary>
std::optional<std::filesystem::path> MotionPhotoHelper::ExtractEmbeddedVideo(
	const std::filesystem::path& jpegPath,
	std::uint64_t videoOffset)
{
	std::error_code fileError;
	const std::uintmax_t fileSize = std::filesystem::file_size(jpegPath, fileError);
	if (fileError || videoOffset >= fileSize || videoOffset > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max()))
	{
		return std::nullopt;
	}

	std::array<wchar_t, MAX_PATH> tempFileName{};
	const DWORD tempPathLength = GetTempPathW(static_cast<DWORD>(tempFileName.size()), tempFileName.data());
	if (tempPathLength == 0 || tempPathLength >= tempFileName.size())
	{
		return std::nullopt;
	}
	std::array<wchar_t, MAX_PATH> tempFilePath{};
	if (GetTempFileNameW(tempFileName.data(), L"smv", 0, tempFilePath.data()) == 0)
	{
		return std::nullopt;
	}

	const std::filesystem::path temporaryPath(tempFilePath.data());
	std::filesystem::path mp4Path = temporaryPath;
	mp4Path.replace_extension(L".mp4");
	std::filesystem::rename(temporaryPath, mp4Path, fileError);
	if (fileError)
	{
		std::filesystem::remove(temporaryPath, fileError);
		return std::nullopt;
	}

	std::ifstream input(jpegPath, std::ios::binary);
	std::ofstream output(mp4Path, std::ios::binary | std::ios::trunc);
	if (!input || !output)
	{
		input.close();
		output.close();
		std::filesystem::remove(mp4Path, fileError);
		return std::nullopt;
	}

	input.seekg(static_cast<std::streamoff>(videoOffset), std::ios::beg);
	std::array<char, COPY_BUFFER_SIZE> buffer{};
	while (input && output)
	{
		input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
		const std::streamsize bytesRead = input.gcount();
		if (bytesRead > 0)
		{
			output.write(buffer.data(), bytesRead);
		}
	}
	const bool succeeded = input.eof() && output.good();
	input.close();
	output.close();
	if (!succeeded || !output)
	{
		std::filesystem::remove(mp4Path, fileError);
		return std::nullopt;
	}
	return mp4Path;
}
