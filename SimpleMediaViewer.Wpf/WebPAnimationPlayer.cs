using SixLabors.ImageSharp;
using SixLabors.ImageSharp.Formats.Webp;
using SixLabors.ImageSharp.PixelFormats;
using System.IO;
using System.Windows.Media.Imaging;
using System.Windows.Threading;

namespace SimpleMediaViewer.Wpf;

/// <summary>
/// WebPアニメーション再生クラス
/// </summary>
public class WebPAnimationPlayer : IDisposable
{
    private Image<Bgra32>? _image;
    private DispatcherTimer? _timer;
    private int _currentFrameIndex = 0;
    private bool _isPlaying = false;

    public event EventHandler<BitmapSource>? FrameChanged;
    public event EventHandler? AnimationCompleted;

    public bool IsPlaying => _isPlaying;
    public int FrameCount => _image?.Frames.Count ?? 0;

    /// <summary>
    /// WebPファイルを読み込む
    /// </summary>
    public async Task LoadAsync(string filePath)
    {
        Stop();
        Dispose();

        _image = await Image.LoadAsync<Bgra32>(filePath);
        _currentFrameIndex = 0;

        // タイマーの初期化
        _timer = new DispatcherTimer();
        _timer.Tick += Timer_Tick;
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

        var frame = _image.Frames[_currentFrameIndex];
        var bitmapSource = ConvertToBitmapSource(frame);

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
    }
}
