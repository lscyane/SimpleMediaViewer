#define NOMINMAX
#include "MotionPhotoHelper.h"

#include <Windows.h>
#include <objbase.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <chrono>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include "CONST.hpp"

#pragma comment(lib, "Ole32.lib")

namespace
{
	/// <summary>
	/// XMP属性から引用符で囲まれた符号なし整数を読み取ります。
	/// </summary>
	std::optional<std::uint64_t> ParseXmpAttribute(std::string_view text, std::string_view name)
	{
		std::size_t searchPosition = 0;
		while ((searchPosition = text.find(name, searchPosition)) != std::string_view::npos)
		{
			// 属性名の直後に等号があり、引用符で囲まれた数字が続くかを確認します。
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

			// 属性値の開始位置を見つけます。
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

			// 属性値の引用符を取得し、数字の開始位置を見つけます。
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

			// 文字列を符号なし整数に変換します。
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
				if (length.has_value()
				 && *length > 0
				 && (!padding.has_value() || *padding <= std::numeric_limits<std::uint64_t>::max() - *length)
				) {
					return *length + padding.value_or(0);
				}
			}
			videoMimePosition = xmp.find("video/mp4", videoMimePosition + 1);
		}
		return std::nullopt;
	}


	std::optional<std::filesystem::path> GetLocalAppDataPath()
	{
		const DWORD requiredLength = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
		if (requiredLength == 0)
		{
			return std::nullopt;
		}

		std::wstring localAppData(requiredLength, L'\0');
		const DWORD actualLength = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData.data(), requiredLength);
		if (actualLength == 0 || actualLength >= requiredLength)
		{
			return std::nullopt;
		}
		localAppData.resize(actualLength);
		return std::filesystem::path(localAppData);
	}


	void CleanupStaleRunDirectories(const std::filesystem::path& tempRoot) noexcept
	{
		try
		{
			const auto staleBefore = std::filesystem::file_time_type::clock::now() - std::chrono::hours(24);
			std::error_code error;
			std::filesystem::directory_iterator iterator(tempRoot, error);
			const std::filesystem::directory_iterator end;
			while (!error && iterator != end)
			{
				const std::filesystem::path runDirectory = iterator->path();
				const std::filesystem::file_status status = iterator->symlink_status(error);
				if (error)
				{
					error.clear();
					iterator.increment(error);
					continue;
				}
				if (status.type() == std::filesystem::file_type::directory)
				{
					const std::filesystem::file_time_type lastWriteTime = std::filesystem::last_write_time(runDirectory, error);
					if (!error && lastWriteTime < staleBefore)
					{
						const std::filesystem::path lockPath = runDirectory / L"owner.lock";
						HANDLE lockHandle = CreateFileW(
							lockPath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
							FILE_ATTRIBUTE_NORMAL, nullptr);
						const DWORD openError = lockHandle == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
						if (lockHandle != INVALID_HANDLE_VALUE)
						{
							CloseHandle(lockHandle);
						}
						if (lockHandle != INVALID_HANDLE_VALUE || openError == ERROR_FILE_NOT_FOUND || openError == ERROR_PATH_NOT_FOUND)
						{
							std::filesystem::remove_all(runDirectory, error);
						}
					}
				}
				error.clear();
				iterator.increment(error);
			}
		}
		catch (...)
		{
		}
	}
}


MotionPhotoTempStorage::MotionPhotoTempStorage(std::filesystem::path directory, std::intptr_t lockHandle) noexcept
	: directory_(std::move(directory)), lockHandle_(lockHandle)
{
}


MotionPhotoTempStorage::MotionPhotoTempStorage(MotionPhotoTempStorage&& other) noexcept
	: directory_(std::move(other.directory_)), lockHandle_(std::exchange(other.lockHandle_, -1))
{
}


MotionPhotoTempStorage& MotionPhotoTempStorage::operator=(MotionPhotoTempStorage&& other) noexcept
{
	if (this != &other)
	{
		Reset();
		directory_ = std::move(other.directory_);
		lockHandle_ = std::exchange(other.lockHandle_, -1);
	}
	return *this;
}


MotionPhotoTempStorage::~MotionPhotoTempStorage()
{
	Reset();
}


std::optional<MotionPhotoTempStorage> MotionPhotoTempStorage::Create()
{
	try
	{
		const std::optional<std::filesystem::path> localAppData = GetLocalAppDataPath();
		if (!localAppData.has_value())
		{
			return std::nullopt;
		}

		const std::filesystem::path tempRoot = *localAppData / L"TeaSoundProject" / L"SimpleMediaViewer" / L"Temp";
		std::error_code error;
		std::filesystem::create_directories(tempRoot, error);
		if (error)
		{
			return std::nullopt;
		}

		CleanupStaleRunDirectories(tempRoot);
		GUID runId{};
		if (CoCreateGuid(&runId) != S_OK)
		{
			return std::nullopt;
		}

		std::array<wchar_t, 39> runIdText{};
		if (StringFromGUID2(runId, runIdText.data(), static_cast<int>(runIdText.size())) == 0)
		{
			return std::nullopt;
		}

		const std::filesystem::path runDirectory = tempRoot / runIdText.data();
		if (!std::filesystem::create_directory(runDirectory, error) || error)
		{
			return std::nullopt;
		}

		const std::filesystem::path lockPath = runDirectory / L"owner.lock";
		HANDLE lockHandle = CreateFileW(
			lockPath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_NEW,
			FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_TEMPORARY, nullptr);
		if (lockHandle == INVALID_HANDLE_VALUE)
		{
			std::filesystem::remove_all(runDirectory, error);
			return std::nullopt;
		}
		return MotionPhotoTempStorage(runDirectory, reinterpret_cast<std::intptr_t>(lockHandle));
	}
	catch (...)
	{
		return std::nullopt;
	}
}


const std::filesystem::path& MotionPhotoTempStorage::Directory() const noexcept
{
	return directory_;
}


void MotionPhotoTempStorage::Reset() noexcept
{
	if (lockHandle_ != -1)
	{
		CloseHandle(reinterpret_cast<HANDLE>(lockHandle_));
		lockHandle_ = -1;
	}
	if (!directory_.empty())
	{
		std::error_code error;
		std::filesystem::remove_all(directory_, error);
		directory_.clear();
	}
}


MotionPhotoTempFile::MotionPhotoTempFile(std::filesystem::path path) noexcept
	: path_(std::move(path))
{
}


MotionPhotoTempFile::MotionPhotoTempFile(MotionPhotoTempFile&& other) noexcept
	: path_(std::move(other.path_))
{
	other.path_.clear();
}


MotionPhotoTempFile& MotionPhotoTempFile::operator=(MotionPhotoTempFile&& other) noexcept
{
	if (this != &other)
	{
		Reset();
		path_ = std::move(other.path_);
		other.path_.clear();
	}
	return *this;
}


MotionPhotoTempFile::~MotionPhotoTempFile()
{
	Reset();
}


const std::filesystem::path& MotionPhotoTempFile::Path() const noexcept
{
	return path_;
}


void MotionPhotoTempFile::Reset() noexcept
{
	if (!path_.empty())
	{
		std::error_code error;
		std::filesystem::remove(path_, error);
		if (!error)
		{
			path_.clear();
		}
	}
}


/// <summary>
/// JPEGのXMPメタデータから埋め込み動画の先頭位置を取得します。
/// </summary>
std::optional<std::uint64_t> MotionPhotoHelper::FindEmbeddedVideoOffset(const std::filesystem::path& jpegPath)
{
	// JPEGファイルのサイズを取得します。
	std::error_code fileError;
	const std::uintmax_t fileSize = std::filesystem::file_size(jpegPath, fileError);
	if (fileError || fileSize == 0)
	{
		return std::nullopt;
	}

	// JPEGのヘッダーを読み取り、XMPメタデータを検索します。
	const std::size_t headerSize = static_cast<std::size_t>(std::min<std::uintmax_t>(fileSize, XMP_HEADER_LIMIT));
	std::ifstream input(jpegPath, std::ios::binary);
	if (!input)
	{
		return std::nullopt;
	}

	// JPEGのヘッダーを読み取り、XMPメタデータを検索します。
	std::string header(headerSize, '\0');
	input.read(header.data(), static_cast<std::streamsize>(header.size()));
	if (input.gcount() != static_cast<std::streamsize>(header.size()))
	{
		return std::nullopt;
	}

	// 従来形式またはContainer形式のXMPから埋め込み動画サイズを取得します。
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
std::optional<MotionPhotoTempFile> MotionPhotoHelper::ExtractEmbeddedVideo(
	const std::filesystem::path& jpegPath,
	std::uint64_t videoOffset,
	const std::filesystem::path& tempDirectory)
{
	// JPEGファイルのサイズを取得し、指定位置が有効かを確認します。
	std::error_code fileError;
	const std::uintmax_t fileSize = std::filesystem::file_size(jpegPath, fileError);
	if (fileError || videoOffset >= fileSize || videoOffset > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max()))
	{
		return std::nullopt;
	}

	if (tempDirectory.empty())
	{
		return std::nullopt;
	}

	GUID fileId{};
	if (CoCreateGuid(&fileId) != S_OK)
	{
		return std::nullopt;
	}
	std::array<wchar_t, 39> fileIdText{};
	if (StringFromGUID2(fileId, fileIdText.data(), static_cast<int>(fileIdText.size())) == 0)
	{
		return std::nullopt;
	}
	const std::filesystem::path mp4Path = tempDirectory / (std::wstring(fileIdText.data()) + L".mp4");

	// 一時MP4ファイルを作成し、指定位置以降の埋め込み動画をコピーします。
	std::ifstream input(jpegPath, std::ios::binary);
	std::ofstream output(mp4Path, std::ios::binary | std::ios::out);
	if (!input || !output)
	{
		input.close();
		output.close();
		std::filesystem::remove(mp4Path, fileError);
		return std::nullopt;
	}

	// 指定位置以降の埋め込み動画を一時MP4ファイルへコピーします。
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
	return MotionPhotoTempFile(mp4Path);
}
