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
#include <random>
#include <utility>

#pragma comment(lib, "Shlwapi.lib")


/// <summary>
/// すべての対応拡張子を初期選択します。
/// </summary>
FileManager::FileManager()
	: selectedExtensions_(SUPPORTED_EXTENSIONS.begin(), SUPPORTED_EXTENSIONS.end())
{
}


/// <summary>
/// ファイル一覧の並び順を設定します。
/// </summary>
void FileManager::SetSortOrder(FileSortOrder sortOrder)
{
	if (sortOrder_ == sortOrder && sortOrder != FileSortOrder::Random)
	{
		return;
	}

	sortOrder_ = sortOrder;
	SortFiles();
}


/// <summary>
/// 現在のファイル一覧の並び順を返します。
/// </summary>
FileSortOrder FileManager::GetSortOrder() const noexcept
{
	return sortOrder_;
}


/// <summary>
/// 拡張子の選択状態を返します。
/// </summary>
bool FileManager::IsExtensionSelected(const std::wstring& extension) const
{
	return std::any_of(selectedExtensions_.begin(), selectedExtensions_.end(), [&extension](const std::wstring& selectedExtension)
	{
		return CompareStringOrdinal(selectedExtension.c_str(), -1, extension.c_str(), -1, TRUE) == CSTR_EQUAL;
	});
}


/// <summary>
/// 選択された拡張子一覧を一括で設定します。
/// </summary>
void FileManager::SetSelectedExtensions(const std::vector<std::wstring>& extensions)
{
	selectedExtensions_ = extensions;
	ApplyExtensionFilter();
}


/// <summary>
/// 指定ファイルのフォルダーにあるメディア一覧の更新を要求します。
/// </summary>
void FileManager::RefreshFolderFiles(const std::wstring& path)
{
	// フォルダーが変わっていなければ何もしません。
	std::error_code error;
	const std::filesystem::path absolutePath = std::filesystem::absolute(path, error).lexically_normal();
	const std::filesystem::path sourcePath = error ? std::filesystem::path(path) : absolutePath;
	const std::wstring directory = sourcePath.parent_path().wstring();
	if (directory == currentDirectory_)
	{
		return;
	}

	// フォルダーが変わった場合は一覧をクリアし、非同期スキャンを開始します。
	currentDirectory_ = directory;
	allFileList_.clear();
	fileList_.clear();
	currentFileIndex_ = fileList_.size();
	if (folderScanFuture_.valid())
	{
		pendingFolderDirectory_ = directory;
		return;
	}

	// フォルダー列挙と自然順ソートをバックグラウンドで開始します。
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
/// 使用できなくなったファイルをスキャン結果と表示対象の一覧から削除します。
/// </summary>
void FileManager::RemoveFile(const std::wstring& path)
{
	const auto matchesPath = [&path](const std::wstring& candidate)
	{
		return CompareStringOrdinal(candidate.c_str(), -1, path.c_str(), -1, TRUE) == CSTR_EQUAL;
	};
	const auto removeMatchingPaths = [&matchesPath](std::vector<std::wstring>& files)
	{
		files.erase(std::remove_if(files.begin(), files.end(), matchesPath), files.end());
	};
	removeMatchingPaths(allFileList_);
	removeMatchingPaths(fileList_);
	UpdateCurrentFileIndex();
}


/// <summary>
/// 完了した非同期スキャンの結果を一覧へ反映します。
/// </summary>
void FileManager::ProcessPendingScan()
{
	// スキャンが完了していなければ何もしません。
	if (!folderScanFuture_.valid() || folderScanFuture_.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
	{
		return;
	}

	// スキャン結果を取得し、現在のフォルダーと一致する場合のみ一覧を更新します。
	FolderScanResult result;
	bool scanSucceeded = false;
	try
	{
		result = folderScanFuture_.get();
		scanSucceeded = true;
	}
	catch (...)
	{
		allFileList_.clear();
		fileList_.clear();
		currentFileIndex_ = fileList_.size();
	}
	if (scanSucceeded && result.directory == currentDirectory_)
	{
		allFileList_ = std::move(result.files);
		ApplyExtensionFilter();
	}

	// 次のスキャンが保留されている場合は、保留されたフォルダーで再度スキャンを開始します。
	if (pendingFolderDirectory_.has_value())
	{
		std::wstring directory = std::move(*pendingFolderDirectory_);
		pendingFolderDirectory_.reset();
		if (!scanSucceeded || directory != result.directory)
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

	if (currentFileIndex_ >= fileList_.size())
	{
		return direction < 0 ? fileList_.back() : fileList_.front();
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
	try
	{
		folderScanFuture_ = std::async(std::launch::async, [directory]
		{
			try
			{
				return FolderScanResult{ directory, EnumerateFolderFiles(directory) };
			}
			catch (...)
			{
				return FolderScanResult{ directory, {} };
			}
		});
	}
	catch (...)
	{
		folderScanFuture_ = {};
	}
}


/// <summary>
/// 対応メディアを列挙し、自然順に並べた一覧を返します。
/// </summary>
std::vector<std::wstring> FileManager::EnumerateFolderFiles(const std::wstring& directory)
{
	std::vector<std::wstring> files;
	std::error_code error;
	std::filesystem::directory_iterator iterator(directory, error);
	const std::filesystem::directory_iterator end;
	while (!error && iterator != end)
	{
		const std::filesystem::directory_entry& entry = *iterator;
		if (!entry.is_regular_file(error))
		{
			error.clear();
		}
		else
		{
			// 対応する拡張子のファイルのみを一覧に追加します。
			std::wstring extension = entry.path().extension().wstring();
			std::transform(extension.begin(), extension.end(), extension.begin(), towlower);
			if (std::find(SUPPORTED_EXTENSIONS.begin(), SUPPORTED_EXTENSIONS.end(), extension) != SUPPORTED_EXTENSIONS.end())
			{
				files.push_back(entry.path().lexically_normal().wstring());
			}
		}
		iterator.increment(error);
	}

	// 自然順ソートを行います。
	std::sort(files.begin(), files.end(), [](const std::wstring& left, const std::wstring& right)
	{
		const std::wstring leftName = std::filesystem::path(left).filename().wstring();
		const std::wstring rightName = std::filesystem::path(right).filename().wstring();
		return StrCmpLogicalW(leftName.c_str(), rightName.c_str()) < 0;
	});
	return files;
}


/// <summary>
/// 現在の並び順でファイル一覧を並べ替えます。
/// </summary>
void FileManager::SortFiles()
{
	if (sortOrder_ == FileSortOrder::Random)
	{
		static std::mt19937 randomEngine(std::random_device{}());
		std::shuffle(fileList_.begin(), fileList_.end(), randomEngine);
	}
	else if (sortOrder_ == FileSortOrder::NameAscending || sortOrder_ == FileSortOrder::NameDescending)
	{
		const bool ascending = sortOrder_ == FileSortOrder::NameAscending;
		std::sort(fileList_.begin(), fileList_.end(), [ascending](const std::wstring& left, const std::wstring& right)
		{
			const std::wstring leftName = std::filesystem::path(left).filename().wstring();
			const std::wstring rightName = std::filesystem::path(right).filename().wstring();
			const int comparison = StrCmpLogicalW(leftName.c_str(), rightName.c_str());
			return ascending ? comparison < 0 : comparison > 0;
		});
	}
	else
	{
		using FileWithTimestamp = std::pair<std::wstring, std::filesystem::file_time_type>;
		std::vector<FileWithTimestamp> filesWithTimestamps;
		filesWithTimestamps.reserve(fileList_.size());
		for (const std::wstring& file : fileList_)
		{
			std::error_code error;
			const std::filesystem::file_time_type timestamp = std::filesystem::last_write_time(file, error);
			filesWithTimestamps.emplace_back(file, error ? std::filesystem::file_time_type::min() : timestamp);
		}

		const bool ascending = sortOrder_ == FileSortOrder::DateAscending;
		std::stable_sort(filesWithTimestamps.begin(), filesWithTimestamps.end(), [ascending](const FileWithTimestamp& left, const FileWithTimestamp& right)
		{
			return ascending ? left.second < right.second : left.second > right.second;
		});
		for (size_t index = 0; index < filesWithTimestamps.size(); ++index)
		{
			fileList_[index] = std::move(filesWithTimestamps[index].first);
		}
	}

	UpdateCurrentFileIndex();
}


/// <summary>
/// 選択された拡張子でスキャン済み一覧を更新します。
/// </summary>
void FileManager::ApplyExtensionFilter()
{
	fileList_.clear();
	for (const std::wstring& file : allFileList_)
	{
		std::wstring extension = std::filesystem::path(file).extension().wstring();
		std::transform(extension.begin(), extension.end(), extension.begin(), towlower);
		if (IsExtensionSelected(extension))
		{
			fileList_.push_back(file);
		}
	}
	SortFiles();
}


/// <summary>
/// 現在のファイルパスに対応する一覧内の位置を更新します。
/// </summary>
void FileManager::UpdateCurrentFileIndex()
{
	currentFileIndex_ = fileList_.size();
	for (size_t index = 0; index < fileList_.size(); ++index)
	{
		if (CompareStringOrdinal(fileList_[index].c_str(), -1, currentFilePath_.c_str(), -1, TRUE) == CSTR_EQUAL)
		{
			currentFileIndex_ = index;
			break;
		}
	}
}
