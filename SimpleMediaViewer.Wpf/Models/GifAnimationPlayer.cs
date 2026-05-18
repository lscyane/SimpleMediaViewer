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
    private CancellationTokenSource? _loadCancellation;
    private Task? _currentLoadTask;

    public event EventHandler<BitmapSource>? FrameChanged;
    public event EventHandler? AnimationCompleted;

    public bool IsPlaying => _isPlaying;
    public int FrameCount => _image?.Frames.Count ?? 0;


    /// <summary>
    /// GIF/APNGファイルを読み込み
    /// </summary>
    public async Task LoadAsync(string filePath)
    {
        // 前回の読み込みをキャンセルして完了を待つ
        _loadCancellation?.Cancel();
        if (_currentLoadTask != null)
        {
            await _currentLoadTask;
        }

        Stop();
        Dispose();

        _loadCancellation = new CancellationTokenSource();
        var cancellationToken = _loadCancellation.Token;

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
        var loadedImage = await Image.LoadAsync<Bgra32>(decoderOptions, fileStream, cancellationToken);

        // キャンセルチェック
        cancellationToken.ThrowIfCancellationRequested();

        // フォーマットを判定
        var isPng = loadedImage.Metadata.DecodedImageFormat is PngFormat;

        var frameCount = loadedImage.Frames.Count;
        var cachedFrames = new BitmapSource[frameCount];
        var frameDelays = new int[frameCount];

        // 各フレームの遅延時間を取得
        if (isPng)
        {
            // APNGの場合
            var pngMetadata = loadedImage.Metadata.GetPngMetadata();
            for (int i = 0; i < frameCount; i++)
            {
                var frame = loadedImage.Frames[i];
                var frameMetadata = frame.Metadata.GetPngMetadata();

                var delayNumerator = frameMetadata.FrameDelay.Numerator;
                var delayDenominator = (frameMetadata.FrameDelay.Denominator > 0) ? frameMetadata.FrameDelay.Denominator : 100;
                frameDelays[i] = (int)((delayNumerator * 1000.0) / delayDenominator);

                if (frameDelays[i] == 0)
                {
                    frameDelays[i] = 100;
                }
            }
        }
        else
        {
            // GIFの場合
            var gifMetadata = loadedImage.Metadata.GetGifMetadata();
            for (int i = 0; i < frameCount; i++)
            {
                var frame = loadedImage.Frames[i];
                var frameMetadata = frame.Metadata.GetGifMetadata();

                var delayInCentiseconds = frameMetadata.FrameDelay;
                frameDelays[i] = delayInCentiseconds > 0 ? delayInCentiseconds * 10 : 100;
            }
        }

        // 全フレームを事前にキャッシュ（ローカル変数を使用）
        _currentLoadTask = Task.Run(() =>
        {
            for (int i = 0; i < frameCount; i++)
            {
                cancellationToken.ThrowIfCancellationRequested();
                cachedFrames[i] = ConvertToBitmapSource(loadedImage.Frames[i]);
            }
        }, cancellationToken);

        await _currentLoadTask;

        // キャンセルチェック
        cancellationToken.ThrowIfCancellationRequested();

        // すべて成功したらフィールドに代入
        _image = loadedImage;
        _cachedFrames = cachedFrames;
        _frameDelays = frameDelays;
        _isFullyLoaded = true;
        _currentFrameIndex = 0;

        // 最初のフレームを表示
        if (_cachedFrames.Length > 0 && _cachedFrames[0] != null)
        {
            FrameChanged?.Invoke(this, _cachedFrames[0]!);
        }

        _currentLoadTask = null;
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
        var byteCount = width * height * 4;
        var byteArray = new byte[byteCount];

        frame.ProcessPixelRows(accessor =>
        {
            var destSpan = byteArray.AsSpan();
            for (int y = 0; y < accessor.Height; y++)
            {
                var rowSpan = accessor.GetRowSpan(y);
                var rowBytes = MemoryMarshal.AsBytes(rowSpan);
                rowBytes.CopyTo(destSpan.Slice(y * width * 4, width * 4));
            }
        });

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

    /// <summary>
    /// リソースを解放
    /// </summary>
    public void Dispose()
    {
        Stop();
        _loadCancellation?.Cancel();
        _loadCancellation?.Dispose();
        _loadCancellation = null;
        _image?.Dispose();
        _image = null;
        _cachedFrames = Array.Empty<BitmapSource>();
        _frameDelays = Array.Empty<int>();
        _isFullyLoaded = false;
    }
}
