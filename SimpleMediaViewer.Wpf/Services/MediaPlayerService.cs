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


    /// <summary>
    /// コンストラクタ
    /// </summary>
    public MediaPlayerService()
    {
        _libVLC = new LibVLC();
        _mediaPlayer = new MediaPlayer(_libVLC);
        _mediaPlayer.Volume = 0;
        _mediaPlayer.EndReached += OnEndReached;
    }


    /// <summary>
    /// 動画ファイルの再生
    /// </summary>
    /// <param name="filePath">再生する動画ファイルのパス</param>
    /// <param name="loop">ループ再生するかどうか</param>
    public void Play(string filePath, bool loop = true)
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
        if (loop)
        {
            _currentMedia.AddOption(":input-repeat=65535");
        }
        _mediaPlayer.Volume = 0;
        _mediaPlayer.Play(_currentMedia);
    }


    /// <summary>
    /// 動画ファイルの再生
    /// </summary>
    public void Play()
    {
        _mediaPlayer.Play();
    }


    /// <summary>
    /// 動画ファイルの再生停止
    /// </summary>
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


    /// <summary>
    /// 動画再生終了イベント
    /// </summary>
    /// <param name="sender"></param>
    /// <param name="e"></param>
    private void OnEndReached(object? sender, EventArgs e)
    {
        EndReached?.Invoke(this, e);
    }


    /// <summary>
    /// リソースの解放
    /// </summary>
    public void Dispose()
    {
        _mediaPlayer.Media = null;
        _currentMedia?.Dispose();
        _mediaPlayer?.Dispose();
        _libVLC?.Dispose();
    }
}
