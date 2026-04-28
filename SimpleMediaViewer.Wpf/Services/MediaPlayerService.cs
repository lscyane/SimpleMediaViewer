using LibVLCSharp.Shared;

namespace SimpleMediaViewer.Wpf.Services;

/// <summary>
/// LibVLCメディアプレーヤーを管理するサービス
/// </summary>
public class MediaPlayerService : IDisposable
{
    private readonly LibVLC _libVLC;
    private readonly MediaPlayer _mediaPlayer;
    private Media? _currentMedia;

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
        // 既存のメディアを停止・破棄
        if (_currentMedia != null)
        {
            // Stopの代わりにMediaを直接nullに設定
            _mediaPlayer.Media = null;
            _currentMedia.Dispose();
            _currentMedia = null;
        }

        _currentMedia = new Media(_libVLC, new Uri(filePath));
        _currentMedia.AddOption(":input-repeat=65535");
        _mediaPlayer.Volume = 0;
        _mediaPlayer.Play(_currentMedia);
    }

    public void Play()
    {
        _mediaPlayer.Play();
    }

    public void Stop()
    {
        // _mediaPlayer.Stop(); 
        // Stopの代わりにMediaをnullに設定することでハングアップを回避
        if (_mediaPlayer.Media != null)
        {
            _mediaPlayer.Media = null;
        }
        
        _currentMedia?.Dispose();
        _currentMedia = null;
    }

    private void OnEndReached(object? sender, EventArgs e)
    {
        EndReached?.Invoke(this, e);
    }

    public void Dispose()
    {
        _mediaPlayer.Media = null;
        _currentMedia?.Dispose();
        _mediaPlayer?.Dispose();
        _libVLC?.Dispose();
    }
}
