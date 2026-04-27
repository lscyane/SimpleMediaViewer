using SixLabors.ImageSharp;
using SixLabors.ImageSharp.Formats;
using SixLabors.ImageSharp.Formats.Gif;
using SixLabors.ImageSharp.Formats.Png;
using SixLabors.ImageSharp.PixelFormats;
using System.IO;
using System.Runtime.InteropServices;
using System.Windows.Media.Imaging;
using System.Windows.Threading;

namespace SimpleMediaViewer.Wpf;

/// <summary>
/// GIF/APNGアニメーション再生クラス
/// </summary>
public class GifAnimationPlayer : IDisposable
{
    private Image<Bgra32>? _image;
    private DispatcherTimer? _timer;
    private int _currentFrameIndex = 0;
    private bool _isPlaying = false;
    private BitmapSource?[] _cachedFrames = Array.Empty<BitmapSource>();
    private int[] _frameDelays = Array.Empty<int>();
    private bool _isFullyLoaded = false;
    private bool _isPng = false;

    public event EventHandler<BitmapSource>? FrameChanged;
    public event EventHandler? AnimationCompleted;

    public bool IsPlaying => _isPlaying;
    public int FrameCount => _image?.Frames.Count ?? 0;

    /// <summary>
    /// GIF/APNGファイルを読み込み
    /// </summary>
    public async Task LoadAsync(string filePath)
    {
        Stop();
        Dispose();

        var configuration = Configuration.Default.Clone();
        configuration.PreferContiguousImageBuffers = true;

        using var fileStream = new FileStream(
            filePath,
            FileMode.Open,
            FileAccess.Read,
            FileShare.Read,
            bufferSize: 80 * 1024,
            useAsync: true);

        var decoderOptions = new DecoderOptions { Configuration = configuration };
        _image = await Image.LoadAsync<Bgra32>(decoderOptions, fileStream);

        // フォーマットを判定
        _isPng = _image.Metadata.DecodedImageFormat is PngFormat;

        var frameCount = _image.Frames.Count;
        _cachedFrames = new BitmapSource[frameCount];
        _frameDelays = new int[frameCount];

        // 各フレームの遅延時間を取得
        if (_isPng)
        {
            var pngMetadata = _image.Metadata.GetPngMetadata();
            for (int i = 0; i < frameCount; i++)
            {
                var frame = _image.Frames[i];
                var frameMetadata = frame.Metadata.GetPngMetadata();

                // APNGのフレーム遅延は分子/分母で表現、デフォルトは100ms
                if (frameMetadata.FrameDelay != null)
                {
                    var delayNumerator = frameMetadata.FrameDelay.Numerator;
                    var delayDenominator = frameMetadata.FrameDelay.Denominator > 0 
                        ? frameMetadata.FrameDelay.Denominator 
                        : 100; // デフォルトは100分の1秒
                    _frameDelays[i] = (int)((delayNumerator * 1000.0) / delayDenominator);
                }
                else
                {
                    _frameDelays[i] = 100; // デフォルト100ms
                }

                if (_frameDelays[i] == 0)
                {
                    _frameDelays[i] = 100; // 0の場合は100msに設定
                }
            }
        }
        else
        {
            var gifMetadata = _image.Metadata.GetGifMetadata();
            for (int i = 0; i < frameCount; i++)
            {
                var frame = _image.Frames[i];
                var frameMetadata = frame.Metadata.GetGifMetadata();

                // GIFのフレーム遅延は1/100秒単位、デフォルトは100ms
                var delayInCentiseconds = frameMetadata.FrameDelay;
                _frameDelays[i] = delayInCentiseconds > 0 ? delayInCentiseconds * 10 : 100;
            }
        }

        // 全フレームを事前にキャッシュ
        await Task.Run(() =>
        {
            for (int i = 0; i < frameCount; i++)
            {
                _cachedFrames[i] = ConvertToBitmapSource(_image.Frames[i]);
            }
        });

        _isFullyLoaded = true;
        _currentFrameIndex = 0;

        // 最初のフレームを表示
        if (_cachedFrames.Length > 0 && _cachedFrames[0] != null)
        {
            FrameChanged?.Invoke(this, _cachedFrames[0]!);
        }
    }

    /// <summary>
    /// アニメーション再生開始
    /// </summary>
    public void Play(bool loop = true)
    {
        if (!_isFullyLoaded || _cachedFrames.Length == 0)
        {
            return;
        }

        if (_isPlaying)
        {
            return;
        }

        _isPlaying = true;
        _currentFrameIndex = 0;

        _timer = new DispatcherTimer(DispatcherPriority.Render);
        _timer.Tick += (s, e) => OnTimerTick(loop);
        _timer.Interval = TimeSpan.FromMilliseconds(_frameDelays[_currentFrameIndex]);
        _timer.Start();
    }

    /// <summary>
    /// 一時停止から再開
    /// </summary>
    public void Resume()
    {
        if (!_isFullyLoaded || _isPlaying)
        {
            return;
        }

        _isPlaying = true;
        _timer = new DispatcherTimer(DispatcherPriority.Render);
        _timer.Tick += (s, e) => OnTimerTick(true);
        _timer.Interval = TimeSpan.FromMilliseconds(_frameDelays[_currentFrameIndex]);
        _timer.Start();
    }

    /// <summary>
    /// 一時停止
    /// </summary>
    public void Pause()
    {
        _timer?.Stop();
        _isPlaying = false;
    }

    /// <summary>
    /// 停止
    /// </summary>
    public void Stop()
    {
        _timer?.Stop();
        _timer = null;
        _isPlaying = false;
        _currentFrameIndex = 0;
    }

    private void OnTimerTick(bool loop)
    {
        if (_cachedFrames[_currentFrameIndex] != null)
        {
            FrameChanged?.Invoke(this, _cachedFrames[_currentFrameIndex]!);
        }

        _currentFrameIndex++;

        if (_currentFrameIndex >= _cachedFrames.Length)
        {
            if (loop)
            {
                _currentFrameIndex = 0;
            }
            else
            {
                Stop();
                AnimationCompleted?.Invoke(this, EventArgs.Empty);
                return;
            }
        }

        // 次のフレームの遅延時間に更新
        if (_timer != null)
        {
            _timer.Interval = TimeSpan.FromMilliseconds(_frameDelays[_currentFrameIndex]);
        }
    }

    private BitmapSource ConvertToBitmapSource(ImageFrame<Bgra32> frame)
    {
        var width = frame.Width;
        var height = frame.Height;

        if (!frame.DangerousTryGetSinglePixelMemory(out var pixelMemory))
        {
            throw new InvalidOperationException("フレームのメモリ取得に失敗しました");
        }

        var byteArray = MemoryMarshal.AsBytes(pixelMemory.Span).ToArray();

        var bitmap = BitmapSource.Create(
            width,
            height,
            96,
            96,
            System.Windows.Media.PixelFormats.Bgra32,
            null,
            byteArray,
            width * 4);

        bitmap.Freeze();
        return bitmap;
    }

    public void Dispose()
    {
        Stop();
        _image?.Dispose();
        _image = null;
        _cachedFrames = Array.Empty<BitmapSource>();
        _frameDelays = Array.Empty<int>();
        _isFullyLoaded = false;
    }
}
