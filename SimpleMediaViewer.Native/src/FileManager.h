#pragma once

#include <cstddef>
#include <future>
#include <optional>
#include <string>
#include <vector>


/// <summary>
/// 同じフォルダー内のメディア一覧と現在位置を管理します。
/// </summary>
class FileManager final
{
public:
	/// <summary> 指定ファイルのフォルダーにあるメディア一覧の更新を要求します。 </summary>
	void RefreshFolderFiles(const std::wstring& path);

	/// <summary> 現在のファイルパスを設定し、一覧内の位置を更新します。 </summary>
	void SetCurrentFilePath(const std::wstring& path);

	/// <summary> 完了した非同期スキャンの結果を一覧へ反映します。 </summary>
	void ProcessPendingScan();

	/// <summary> フォルダー一覧のスキャンが進行中かを返します。 </summary>
	bool IsScanPending() const noexcept;

	/// <summary> 一覧を循環し、指定方向にあるファイルパスを返します。 </summary>
	std::optional<std::wstring> GetRelativeFile(int direction) const;


private:
	struct FolderScanResult
	{
		std::wstring directory;
		std::vector<std::wstring> files;
	};

	/// <summary> フォルダー列挙と自然順ソートをバックグラウンドで開始します。 </summary>
	void StartFolderScan(const std::wstring& directory);

	/// <summary> 対応メディアを列挙し、自然順に並べた一覧を返します。 </summary>
	static std::vector<std::wstring> EnumerateFolderFiles(const std::wstring& directory);

	/// <summary>  現在のファイルパスに対応する一覧内の位置を更新します。 </summary>
	void UpdateCurrentFileIndex();

	// 現在のフォルダー
	std::wstring currentDirectory_;
	// 現在のファイルパスと一覧
	std::wstring currentFilePath_;
	// 現在のファイルパスに対応する一覧内の位置
	std::vector<std::wstring> fileList_;
	// 現在のファイルパスに対応する一覧内の位置
	size_t currentFileIndex_ = 0;
	// フォルダー列挙と自然順ソートをバックグラウンドで行うための非同期タスク
	std::future<FolderScanResult> folderScanFuture_;
	// 次のスキャンが保留されている場合のフォルダー
	std::optional<std::wstring> pendingFolderDirectory_;
};
