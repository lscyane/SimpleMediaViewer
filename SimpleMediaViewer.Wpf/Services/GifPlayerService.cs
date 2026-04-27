using System.Windows.Media.Imaging;

namespace SimpleMediaViewer.Wpf.Services;

/// <summary>
/// GIFアニメーション再生を管理するサービス
/// </summary>
public class GifPlayerService : IDisposable
{
    private readonly GifAnimationPlayer _player;

    public event EventHandler<BitmapSource>? FrameChanged;

    public bool IsPlaying => _player.IsPlaying;
    public int FrameCount => _player.FrameCount;

    public GifPlayerService()
    {
        _player = new GifAnimationPlayer();
        _player.FrameChanged += OnFrameChanged;
    }

    public async Task LoadAsync(string filePath)
    {
        await _player.LoadAsync(filePath);
    }

    public void Play(bool loop = true)
    {
        _player.Play(loop);
    }

    public void Resume()
    {
        _player.Resume();
    }

    public void Stop()
    {
        _player.Stop();
    }

    private void OnFrameChanged(object? sender, BitmapSource e)
    {
        FrameChanged?.Invoke(this, e);
    }

    public void Dispose()
    {
        _player?.Stop();
        _player?.Dispose();
    }
}
