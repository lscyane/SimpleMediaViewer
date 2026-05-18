using System.IO;
using System.Runtime.InteropServices;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;

namespace SimpleMediaViewer.Wpf;

/// <summary>
/// GIF/APNGアニメーション再生クラス（WPFビルトインデコーダー + フレームコンポジット対応）
/// </summary>
public class GifAnimationPlayer : IDisposable
{
    private DispatcherTimer? _timer;
    private int _currentFrameIndex = 0;
    private bool _isPlaying = false;
    private BitmapSource?[] _cachedFrames = Array.Empty<BitmapSource?>();
    private int[] _frameDelays = Array.Empty<int>();
    private bool _isFullyLoaded = false;
    private CancellationTokenSource? _loadCancellation;
    private Task? _currentLoadTask;
    private bool _waitingForNextFrame = false;
    private bool _loop = true;

    public event EventHandler<BitmapSource>? FrameChanged;
    public event EventHandler? AnimationCompleted;

    public bool IsPlaying => _isPlaying;
    public int FrameCount => _cachedFrames.Length;


    /// <summary>
    /// GIF/APNGファイルを読み込み。
    /// フレーム0のみ即時コンポジットして返り、残りはバックグラウンドで順次処理する。
    /// </summary>
    public async Task LoadAsync(string filePath)
    {
        _loadCancellation?.Cancel();
        if (_currentLoadTask != null)
        {
            try { await _currentLoadTask; }
            catch (OperationCanceledException) { }
        }

        Stop();
        ClearState();

        _loadCancellation = new CancellationTokenSource();
        var cancellationToken = _loadCancellation.Token;

        var isGif = filePath.EndsWith(".gif", StringComparison.OrdinalIgnoreCase);

        // フレーム0のコンポジットをバックグラウンドで実行して即返す
        // canvasPixels はバックグラウンドタスクへ引き継ぐためクロージャで共有
        byte[]? sharedCanvas = null;
        byte[]? sharedPrevCanvas = null;
        int sharedCanvasW = 0, sharedCanvasH = 0;

        // 非同期でフレーム0をコンポジット
        var (frames, delays) = await Task.Run(() =>
        {
            cancellationToken.ThrowIfCancellationRequested();

            var decoder = BitmapDecoder.Create(
                new Uri(filePath, UriKind.Absolute),
                BitmapCreateOptions.PreservePixelFormat,
                BitmapCacheOption.OnLoad);

            var (canvasW, canvasH) = GetCanvasSize(decoder);
            sharedCanvasW = canvasW;
            sharedCanvasH = canvasH;

            var frameCount = decoder.Frames.Count;
            var newFrames = new BitmapSource?[frameCount];
            var newDelays = new int[frameCount];

            for (int i = 0; i < frameCount; i++)
            {
                newDelays[i] = isGif ? GetGifFrameDelay(decoder.Frames[i]) : 100;
            }
            // フレーム0をコンポジット
            var canvas = new byte[canvasW * canvasH * 4];
            var prevCanvas = new byte[canvasW * canvasH * 4];
            newFrames[0] = ComposeFrame(decoder.Frames[0], canvas, prevCanvas, canvasW, canvasH);

            sharedCanvas = canvas;
            sharedPrevCanvas = prevCanvas;

            return (newFrames, newDelays);
        }, cancellationToken);

        cancellationToken.ThrowIfCancellationRequested();

        _cachedFrames = frames;
        _frameDelays = delays;
        _isFullyLoaded = frames.Length <= 1;
        _currentFrameIndex = 0;

        // フレーム1以降をバックグラウンドで順次コンポジット
        if (frames.Length > 1)
        {
            var canvas = sharedCanvas!;
            var prevCanvas = sharedPrevCanvas!;
            var canvasW = sharedCanvasW;
            var canvasH = sharedCanvasH;
            var frameCount = frames.Length;

            _currentLoadTask = Task.Run(() =>
            {
                try
                {
                    // このスレッド上で新たにデコーダーを作成することでスレッドアフィニティ違反を回避
                    var decoder = BitmapDecoder.Create(
                        new Uri(filePath, UriKind.Absolute),
                        BitmapCreateOptions.PreservePixelFormat,
                        BitmapCacheOption.OnLoad);

                    for (int i = 1; i < frameCount; i++)
                    {
                        cancellationToken.ThrowIfCancellationRequested();

                        _cachedFrames[i] = ComposeFrame(decoder.Frames[i], canvas, prevCanvas, canvasW, canvasH);

                        if (_waitingForNextFrame && _isPlaying && _currentFrameIndex == i)
                        {
                            _waitingForNextFrame = false;
                            System.Windows.Application.Current?.Dispatcher.BeginInvoke(() => AdvanceAndShow(_loop));
                        }
                    }

                    _isFullyLoaded = true;
                }
                catch (OperationCanceledException) { }
            }, cancellationToken);
        }
    }


    /// <summary>
    /// アニメーション再生開始
    /// </summary>
    public void Play(bool loop = true)
    {
        if (_cachedFrames.Length == 0 || _cachedFrames[0] == null)
            return;

        if (_isPlaying)
            return;

        _loop = loop;
        _isPlaying = true;
        _waitingForNextFrame = false;
        _currentFrameIndex = 0;

        FrameChanged?.Invoke(this, _cachedFrames[0]!);

        if (_cachedFrames.Length == 1)
        {
            if (!loop)
            {
                _isPlaying = false;
                AnimationCompleted?.Invoke(this, EventArgs.Empty);
            }
            return;
        }

        _timer = new DispatcherTimer(DispatcherPriority.Render);
        _timer.Tick += (s, e) => { _timer!.Stop(); AdvanceAndShow(_loop); };
        _timer.Interval = TimeSpan.FromMilliseconds(_frameDelays[0]);
        _timer.Start();
    }


    /// <summary>
    /// 一時停止から再開
    /// </summary>
    public void Resume()
    {
        if (_isPlaying || _cachedFrames.Length == 0)
            return;

        _isPlaying = true;
        _waitingForNextFrame = false;

        _timer = new DispatcherTimer(DispatcherPriority.Render);
        _timer.Tick += (s, e) => { _timer!.Stop(); AdvanceAndShow(true); };
        _timer.Interval = TimeSpan.FromMilliseconds(_frameDelays[_currentFrameIndex]);
        _timer.Start();
    }


    /// <summary>
    /// 停止
    /// </summary>
    public void Stop()
    {
        _timer?.Stop();
        _timer = null;
        _isPlaying = false;
        _waitingForNextFrame = false;
        _currentFrameIndex = 0;
    }


    /// <summary>
    /// 一時停止
    /// </summary>
    public void Pause()
    {
        _timer?.Stop();
        _timer = null;
        _isPlaying = false;
        _waitingForNextFrame = false;
    }


    /// <summary>
    /// 次フレームへ進んで表示する。キャッシュ未完了なら完了まで待機する。
    /// </summary>
    private void AdvanceAndShow(bool loop)
    {
        if (!_isPlaying) return;

        var nextIndex = _currentFrameIndex + 1;

        if (nextIndex >= _cachedFrames.Length)
        {
            if (loop)
                nextIndex = 0;
            else
            {
                Stop();
                AnimationCompleted?.Invoke(this, EventArgs.Empty);
                return;
            }
        }

        if (_cachedFrames[nextIndex] == null)
        {
            _waitingForNextFrame = true;
            return;
        }

        _currentFrameIndex = nextIndex;
        FrameChanged?.Invoke(this, _cachedFrames[_currentFrameIndex]!);

        _timer = new DispatcherTimer(DispatcherPriority.Render);
        _timer.Tick += (s, e) => { _timer!.Stop(); AdvanceAndShow(loop); };
        _timer.Interval = TimeSpan.FromMilliseconds(_frameDelays[_currentFrameIndex]);
        _timer.Start();
    }


    // ─────────────────────────────────────────────────────────────
    #region GIFコンポジット処理
    // ─────────────────────────────────────────────────────────────

    /// <summary>
    /// フレームをキャンバスにコンポジットして BitmapSource を返す。
    /// canvas はインプレースで更新される（次フレームのために状態を引き継ぐ）。
    /// </summary>
    private static BitmapSource ComposeFrame(BitmapFrame frame, byte[] canvas, byte[] prevCanvas, int canvasW, int canvasH)
    {
        var (left, top) = GetFrameOffset(frame);
        var disposal = GetDisposalMethod(frame);

        // Disposal method 3: この描画前に canvas を保存
        if (disposal == 3)
        {
            Buffer.BlockCopy(canvas, 0, prevCanvas, 0, canvas.Length);
        }

        // フレームピクセルを Bgra32 で取得
        BitmapSource src = frame.Format != PixelFormats.Bgra32
            ? new FormatConvertedBitmap(frame, PixelFormats.Bgra32, null, 0)
            : frame;

        var frameW = src.PixelWidth;
        var frameH = src.PixelHeight;
        var framePixels = new byte[frameW * frameH * 4];
        src.CopyPixels(framePixels, frameW * 4, 0);

        // キャンバスにフレームを描画（src-over アルファブレンド）
        for (int y = 0; y < frameH; y++)
        {
            int cy = top + y;
            if (cy < 0 || cy >= canvasH) continue;
            for (int x = 0; x < frameW; x++)
            {
                int cx = left + x;
                if (cx < 0 || cx >= canvasW) continue;

                int si = (y * frameW + x) * 4;
                int di = (cy * canvasW + cx) * 4;
                byte sa = framePixels[si + 3];

                if (sa == 0) continue;

                if (sa == 255)
                {
                    canvas[di]     = framePixels[si];
                    canvas[di + 1] = framePixels[si + 1];
                    canvas[di + 2] = framePixels[si + 2];
                    canvas[di + 3] = 255;
                }
                else
                {
                    float a = sa / 255f;
                    float ia = 1f - a;
                    canvas[di]     = (byte)(framePixels[si]     * a + canvas[di]     * ia);
                    canvas[di + 1] = (byte)(framePixels[si + 1] * a + canvas[di + 1] * ia);
                    canvas[di + 2] = (byte)(framePixels[si + 2] * a + canvas[di + 2] * ia);
                    canvas[di + 3] = (byte)(sa + canvas[di + 3] * ia);
                }
            }
        }

        // 現在のキャンバスをスナップショットとして保存
        var snapshot = canvas.ToArray();
        var bmp = BitmapSource.Create(canvasW, canvasH, 96, 96, PixelFormats.Bgra32, null, snapshot, canvasW * 4);
        bmp.Freeze();

        // Disposal method を適用（次フレームのためにキャンバスを変更）
        switch (disposal)
        {
            case 2: // Restore to background (透明にクリア)
                for (int y = 0; y < frameH; y++)
                {
                    int cy = top + y;
                    if (cy < 0 || cy >= canvasH) continue;
                    for (int x = 0; x < frameW; x++)
                    {
                        int cx = left + x;
                        if (cx < 0 || cx >= canvasW) continue;
                        int di = (cy * canvasW + cx) * 4;
                        canvas[di] = canvas[di + 1] = canvas[di + 2] = canvas[di + 3] = 0;
                    }
                }
                break;
            case 3: // Restore to previous
                Buffer.BlockCopy(prevCanvas, 0, canvas, 0, canvas.Length);
                break;
            // 0, 1: Do not dispose（そのまま）
        }

        return bmp;
    }
    #endregion


    // ─────────────────────────────────────────────────────────────
    #region メタデータユーティリティ
    // ─────────────────────────────────────────────────────────────

    private static (int width, int height) GetCanvasSize(BitmapDecoder decoder)
    {
        int w = 0, h = 0;
        try
        {
            if (decoder.Metadata is BitmapMetadata meta)
            {
                w = ToInt(meta.GetQuery("/logscrdesc/Width"));
                h = ToInt(meta.GetQuery("/logscrdesc/Height"));
            }
        }
        catch { }

        if (w <= 0) w = decoder.Frames[0].PixelWidth;
        if (h <= 0) h = decoder.Frames[0].PixelHeight;
        return (w, h);
    }

    private static (int left, int top) GetFrameOffset(BitmapFrame frame)
    {
        int left = 0, top = 0;
        try
        {
            if (frame.Metadata is BitmapMetadata meta)
            {
                left = ToInt(meta.GetQuery("/imgdesc/Left"));
                top  = ToInt(meta.GetQuery("/imgdesc/Top"));
            }
        }
        catch { }
        return (left, top);
    }

    private static int GetDisposalMethod(BitmapFrame frame)
    {
        try
        {
            if (frame.Metadata is BitmapMetadata meta)
                return ToInt(meta.GetQuery("/grctlext/Disposal"));
        }
        catch { }
        return 0;
    }

    private static int GetGifFrameDelay(BitmapFrame frame)
    {
        try
        {
            if (frame.Metadata is BitmapMetadata meta)
            {
                var v = meta.GetQuery("/grctlext/Delay");
                int cs = ToInt(v);
                if (cs > 0) return cs * 10;
            }
        }
        catch { }
        return 100;
    }

    private static int ToInt(object? value) => value switch
    {
        ushort v => v,
        short v  => v,
        uint v   => (int)v,
        int v    => v,
        byte v   => v,
        _        => 0,
    };

    private void ClearState()
    {
        _cachedFrames = Array.Empty<BitmapSource?>();
        _frameDelays = Array.Empty<int>();
        _isFullyLoaded = false;
        _waitingForNextFrame = false;
    }

    public void Dispose()
    {
        _loadCancellation?.Cancel();
        Stop();
        _loadCancellation?.Dispose();
        _loadCancellation = null;
        _currentLoadTask = null;
        ClearState();
    }
    #endregion
}
