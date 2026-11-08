#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <utility>

/// <summary>
/// Motion Photo用の実行単位一時ディレクトリと所有ロックを管理します。
/// </summary>
class MotionPhotoTempStorage final
{
public:
	static std::optional<MotionPhotoTempStorage> Create();

	MotionPhotoTempStorage(const MotionPhotoTempStorage&) = delete;
	MotionPhotoTempStorage& operator=(const MotionPhotoTempStorage&) = delete;
	MotionPhotoTempStorage(MotionPhotoTempStorage&& other) noexcept;
	MotionPhotoTempStorage& operator=(MotionPhotoTempStorage&& other) noexcept;
	~MotionPhotoTempStorage();

	const std::filesystem::path& Directory() const noexcept;

private:
	MotionPhotoTempStorage(std::filesystem::path directory, std::intptr_t lockHandle) noexcept;
	void Reset() noexcept;

	std::filesystem::path directory_;
	std::intptr_t lockHandle_ = -1;
};

/// <summary>
/// Motion Photoから抽出した一時動画ファイルを管理します。
/// </summary>
class MotionPhotoTempFile final
{
public:
	MotionPhotoTempFile() = default;
	MotionPhotoTempFile(const MotionPhotoTempFile&) = delete;
	MotionPhotoTempFile& operator=(const MotionPhotoTempFile&) = delete;
	MotionPhotoTempFile(MotionPhotoTempFile&& other) noexcept;
	MotionPhotoTempFile& operator=(MotionPhotoTempFile&& other) noexcept;
	~MotionPhotoTempFile();

	const std::filesystem::path& Path() const noexcept;
	void Reset() noexcept;

private:
	explicit MotionPhotoTempFile(std::filesystem::path path) noexcept;
	friend class MotionPhotoHelper;

	std::filesystem::path path_;
};

/// <summary>
/// Google Pixel Motion PhotoのJPEGメタデータを解析し、埋め込み動画を抽出します。
/// </summary>
class MotionPhotoHelper final
{
public:
	/// <summary>JPEGのXMPメタデータから埋め込み動画の先頭位置を取得します。</summary>
	static std::optional<std::uint64_t> FindEmbeddedVideoOffset(const std::filesystem::path& jpegPath);

	/// <summary>指定位置以降の埋め込み動画を専用一時ディレクトリへ抽出します。</summary>
	static std::optional<MotionPhotoTempFile> ExtractEmbeddedVideo(const std::filesystem::path& jpegPath, std::uint64_t videoOffset, const std::filesystem::path& tempDirectory);
};
