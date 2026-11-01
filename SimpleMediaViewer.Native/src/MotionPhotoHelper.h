#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>

/// <summary>
/// Google Pixel Motion PhotoのJPEGメタデータを解析し、埋め込み動画を抽出します。
/// </summary>
class MotionPhotoHelper final
{
public:
	/// <summary>JPEGのXMPメタデータから埋め込み動画の先頭位置を取得します。</summary>
	static std::optional<std::uint64_t> FindEmbeddedVideoOffset(const std::filesystem::path& jpegPath);

	/// <summary>指定位置以降の埋め込み動画を一時MP4ファイルへ抽出します。</summary>
	static std::optional<std::filesystem::path> ExtractEmbeddedVideo(
		const std::filesystem::path& jpegPath,
		std::uint64_t videoOffset);
};
