using LibVLCSharp.Shared;

namespace SimpleMediaViewer.Wpf.Services;

/// <summary>
/// LibVLCメディアプレーヤーを管理するサービス
/// </summary>
public class MediaPlayerService : IDisposable
{
    private readonly LibVLC _libVLC;
    private readonly MediaPlayer _mediaPlayer;

    public MediaPlayer Player => _mediaPlayer;

    public event EventHandler? EndReached;

    public MediaPlayerService()
    {
        _libVLC = new LibVLC();
        _mediaPlayer = new MediaPlayer(_libVLC);
        _mediaPlayer.Volume = 0;
        _mediaPlayer.EndReached += OnEndReached;
    }

    public void Play(string filePath)
    {
        var media = new Media(_libVLC, new Uri(filePath));
        media.AddOption(":input-repeat=65535");
        _mediaPlayer.Volume = 0;
        _mediaPlayer.Play(media);
    }

    public void Play()
    {
        _mediaPlayer.Play();
    }

    public void Stop()
    {
        _mediaPlayer.Stop();
    }

    private void OnEndReached(object? sender, EventArgs e)
    {
        EndReached?.Invoke(this, e);
    }

    public void Dispose()
    {
        _mediaPlayer?.Stop();
        _mediaPlayer?.Dispose();
        _libVLC?.Dispose();
    }
}
