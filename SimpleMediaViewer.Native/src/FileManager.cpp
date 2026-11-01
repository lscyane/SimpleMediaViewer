#define NOMINMAX
#include "FileManager.h"
#include "CONST.hpp"

#include <Windows.h>
#include <shlwapi.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cwctype>
#include <filesystem>
#include <utility>

#pragma comment(lib, "Shlwapi.lib")


/// <summary>
/// 指定ファイルのフォルダーにあるメディア一覧の更新を要求します。
/// </summary>
void FileManager::RefreshFolderFiles(const std::wstring& path)
{
	std::error_code error;
	const std::filesystem::path absolutePath = std::filesystem::absolute(path, error).lexically_normal();
	const std::filesystem::path sourcePath = error ? std::filesystem::path(path) : absolutePath;
	const std::wstring directory = sourcePath.parent_path().wstring();
	if (directory == currentDirectory_)
	{
		return;
	}

	currentDirectory_ = directory;
	fileList_.clear();
	currentFileIndex_ = 0;
	if (folderScanFuture_.valid())
	{
		pendingFolderDirectory_ = directory;
		return;
	}

	StartFolderScan(directory);
}


/// <summary>
/// 現在のファイルパスを設定し、一覧内の位置を更新します。
/// </summary>
void FileManager::SetCurrentFilePath(const std::wstring& path)
{
	currentFilePath_ = path;
	UpdateCurrentFileIndex();
}


/// <summary>
/// 完了した非同期スキャンの結果を一覧へ反映します。
/// </summary>
void FileManager::ProcessPendingScan()
{
	if (!folderScanFuture_.valid() || folderScanFuture_.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
	{
		return;
	}

	FolderScanResult result = folderScanFuture_.get();
	if (result.directory == currentDirectory_)
	{
		fileList_ = std::move(result.files);
		UpdateCurrentFileIndex();
	}

	if (pendingFolderDirectory_.has_value())
	{
		std::wstring directory = std::move(*pendingFolderDirectory_);
		pendingFolderDirectory_.reset();
		if (directory != result.directory)
		{
			StartFolderScan(directory);
		}
	}
}


/// <summary>
/// フォルダー一覧のスキャンが進行中かを返します。
/// </summary>
bool FileManager::IsScanPending() const noexcept
{
	return folderScanFuture_.valid();
}


/// <summary>
/// 一覧を循環し、指定方向にあるファイルパスを返します。
/// </summary>
std::optional<std::wstring> FileManager::GetRelativeFile(int direction) const
{
	if (fileList_.empty())
	{
		return std::nullopt;
	}

	const int count = static_cast<int>(fileList_.size());
	const int nextIndex = (static_cast<int>(currentFileIndex_) + direction + count) % count;
	return fileList_[static_cast<size_t>(nextIndex)];
}


/// <summary>
/// フォルダー列挙と自然順ソートをバックグラウンドで開始します。
/// </summary>
void FileManager::StartFolderScan(const std::wstring& directory)
{
	folderScanFuture_ = std::async(std::launch::async, [directory]
	{
		return FolderScanResult{ directory, EnumerateFolderFiles(directory) };
	});
}


/// <summary>
/// 対応メディアを列挙し、自然順に並べた一覧を返します。
/// </summary>
std::vector<std::wstring> FileManager::EnumerateFolderFiles(const std::wstring& directory)
{
	std::vector<std::wstring> files;
	std::error_code error;
	for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(directory, error))
	{
		if (error)
		{
			break;
		}
		if (!entry.is_regular_file(error))
		{
			error.clear();
			continue;
		}

		std::wstring extension = entry.path().extension().wstring();
		std::transform(extension.begin(), extension.end(), extension.begin(), towlower);
		if (std::find(SUPPORTED_EXTENSIONS.begin(), SUPPORTED_EXTENSIONS.end(), extension) != SUPPORTED_EXTENSIONS.end())
		{
			files.push_back(entry.path().lexically_normal().wstring());
		}
		error.clear();
	}

	std::sort(files.begin(), files.end(), [](const std::wstring& left, const std::wstring& right)
	{
		const std::wstring leftName = std::filesystem::path(left).filename().wstring();
		const std::wstring rightName = std::filesystem::path(right).filename().wstring();
		return StrCmpLogicalW(leftName.c_str(), rightName.c_str()) < 0;
	});
	return files;
}


/// <summary>
/// 現在のファイルパスに対応する一覧内の位置を更新します。
/// </summary>
void FileManager::UpdateCurrentFileIndex()
{
	currentFileIndex_ = 0;
	for (size_t index = 0; index < fileList_.size(); ++index)
	{
		if (CompareStringOrdinal(fileList_[index].c_str(), -1, currentFilePath_.c_str(), -1, TRUE) == CSTR_EQUAL)
		{
			currentFileIndex_ = index;
			break;
		}
	}
}
