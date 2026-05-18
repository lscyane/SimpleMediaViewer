using SixLabors.ImageSharp;
using SixLabors.ImageSharp.Formats;
using SixLabors.ImageSharp.Formats.Webp;
using SixLabors.ImageSharp.PixelFormats;
using System.IO;
using System.Windows.Media.Imaging;
using System.Windows.Threading;

namespace SimpleMediaViewer.Wpf;

/// <summary>
/// WebPアニメーション再生クラス（高速化版）
/// </summary>
public class WebPAnimationPlayer : IDisposable
{
    private Image<Bgra32>? _image;
    private DispatcherTimer? _timer;
    private int _currentFrameIndex = 0;
    private bool _isPlaying = false;
    private BitmapSource?[] _cachedFrames = Array.Empty<BitmapSource>();
    private bool _isFullyLoaded = false;

    public event EventHandler<BitmapSource>? FrameChanged;
    public event EventHandler? AnimationCompleted;

    public bool IsPlaying => _isPlaying;
    public int FrameCount => _image?.Frames.Count ?? 0;
    public BitmapSource? FirstFrame => _cachedFrames.Length > 0 ? _cachedFrames[0] : null;

    /// <summary>
    /// WebPファイルを高速読み込み
    /// </summary>
    public async Task LoadAsync(string filePath)
    {
        Stop();
        Dispose();

        // デコードとフレーム0変換をバックグラウンドで実行（CPUバウンドのためUIスレッドをブロックしない）
        var (loadedImage, cachedFrames) = await Task.Run(() =>
        {
            var configuration = Configuration.Default.Clone();
            configuration.PreferContiguousImageBuffers = true;

            using var fileStream = new FileStream(
                filePath,
                FileMode.Open,
                FileAccess.Read,
                FileShare.Read,
                bufferSize: 80 * 1024);

            var decoderOptions = new DecoderOptions { Configuration = configuration };
            var image = Image.Load<Bgra32>(decoderOptions, fileStream);

            var frames = new BitmapSource?[image.Frames.Count];

            // フレーム0を即時変換
            if (image.Frames.Count > 0)
                frames[0] = ConvertToBitmapSource(image.Frames[0]);

            return (image, frames);
        });

        _image = loadedImage;
        _cachedFrames = cachedFrames;
        _currentFrameIndex = 0;
        _isFullyLoaded = false;

        // タイマーの初期化
        _timer = new DispatcherTimer();
        _timer.Tick += Timer_Tick;

        // バックグラウンドで残りのフレームをキャッシュ
        if (_image.Frames.Count > 1)
        {
            _ = Task.Run(() => CacheAllFramesAsync());
        }
        else
        {
            _isFullyLoaded = true;
        }
    }

    /// <summary>
    /// 全フレームを事前キャッシュ（バックグラウンド処理）
    /// </summary>
    private async Task CacheAllFramesAsync()
    {
        await Task.Yield(); // UIスレッドに制御を戻す

        if (_image == null) return;

        // 2フレーム目以降を順次キャッシュ
        for (int i = 1; i < _image.Frames.Count; i++)
        {
            if (_image == null) break; // 途中でDisposeされた場合

            var frame = _image.Frames[i];
            var bitmapSource = ConvertToBitmapSource(frame);
            _cachedFrames[i] = bitmapSource;

            // 定期的にUIスレッドに制御を返す
            if (i % 10 == 0)
            {
                await Task.Delay(1);
            }
        }

        _isFullyLoaded = true;
    }

    /// <summary>
    /// アニメーション再生を開始
    /// </summary>
    public void Play(bool loop = true)
    {
        if (_image == null || _timer == null)
            return;

        _isPlaying = true;
        _currentFrameIndex = 0;
        Loop = loop;

        ShowCurrentFrame();
        UpdateTimerInterval();
        _timer.Start();
    }

    /// <summary>
    /// アニメーション再生を停止
    /// </summary>
    public void Stop()
    {
        _isPlaying = false;
        _timer?.Stop();
        _currentFrameIndex = 0;
    }

    /// <summary>
    /// アニメーション再生を一時停止
    /// </summary>
    public void Pause()
    {
        _isPlaying = false;
        _timer?.Stop();
    }

    /// <summary>
    /// アニメーション再生を再開
    /// </summary>
    public void Resume()
    {
        if (_image == null || _timer == null)
            return;

        _isPlaying = true;
        _timer.Start();
    }

    public bool Loop { get; set; } = true;

    private void Timer_Tick(object? sender, EventArgs e)
    {
        if (_image == null || !_isPlaying)
            return;

        _currentFrameIndex++;

        if (_currentFrameIndex >= _image.Frames.Count)
        {
            if (Loop)
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

        ShowCurrentFrame();
        UpdateTimerInterval();
    }

    private void ShowCurrentFrame()
    {
        if (_image == null || _currentFrameIndex >= _image.Frames.Count)
            return;

        // キャッシュがあればそれを使用、なければその場でデコード
        var bitmapSource = _cachedFrames[_currentFrameIndex];
        if (bitmapSource == null)
        {
            var frame = _image.Frames[_currentFrameIndex];
            bitmapSource = ConvertToBitmapSource(frame);
            _cachedFrames[_currentFrameIndex] = bitmapSource;
        }

        FrameChanged?.Invoke(this, bitmapSource);
    }

    private void UpdateTimerInterval()
    {
        if (_image == null || _timer == null || _currentFrameIndex >= _image.Frames.Count)
            return;

        var frame = _image.Frames[_currentFrameIndex];
        var metadata = frame.Metadata.GetWebpMetadata();

        // フレーム遅延時間を取得（ミリ秒単位）
        var delay = metadata.FrameDelay > 0 ? (int)metadata.FrameDelay : 100;
        _timer.Interval = TimeSpan.FromMilliseconds(delay);
    }

    private BitmapSource ConvertToBitmapSource(ImageFrame<Bgra32> frame)
    {
        var width = frame.Width;
        var height = frame.Height;
        var pixels = new byte[width * height * 4];

        frame.CopyPixelDataTo(pixels);

        var bitmap = BitmapSource.Create(
            width,
            height,
            96,
            96,
            System.Windows.Media.PixelFormats.Bgra32,
            null,
            pixels,
            width * 4);

        bitmap.Freeze();
        return bitmap;
    }

    public void Dispose()
    {
        _timer?.Stop();
        _timer = null;
        _image?.Dispose();
        _image = null;
        _cachedFrames = Array.Empty<BitmapSource>();
        _isFullyLoaded = false;
    }
}
