using System.IO;
using System.Text;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Documents;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Navigation;
using System.Windows.Shapes;

namespace SimpleMediaViewer.Wpf
{
    /// <summary>
    /// Interaction logic for MainWindow.xaml
    /// </summary>
    public partial class MainWindow : Window
    {
        public MainWindow()
        {
            this.DataContext = new Wpf.MainWindowViewModel();
            InitializeComponent();
            /*
            // 起動時に画像読み込み（例）
            var bmp = new BitmapImage();
            bmp.BeginInit();
            bmp.UriSource = new Uri("sample.jpg", UriKind.Relative);
            bmp.CacheOption = BitmapCacheOption.OnLoad; // 即時ロード
            bmp.EndInit();
            bmp.Freeze(); // 描画スレッド最適化

            ImageView.Source = bmp;
            */
        }

        // D&D中のカーソル制御
        private void Window_DragOver(object sender, DragEventArgs e)
        {
            // ファイルのみ受け付ける
            e.Effects = e.Data.GetDataPresent(DataFormats.FileDrop)
                ? DragDropEffects.Copy
                : DragDropEffects.None;

            e.Handled = true;
        }

        // ドロップ時処理
        private void Window_Drop(object sender, DragEventArgs e)
        {
            if (!e.Data.GetDataPresent(DataFormats.FileDrop))
                return;

            // 最初の1ファイルのみ使用
            var path = ((string[])e.Data.GetData(DataFormats.FileDrop)!)
                       .FirstOrDefault();

            if (path == null || !File.Exists(path))
                return;

            // 既存再生停止
            VideoView.Stop();
            VideoView.Source = null;

            // 動画として設定（画像も MediaElement で表示可能）
            VideoView.Source = new Uri(path, UriKind.Absolute);
            VideoView.Play();
        }

        // ウィンドウいっぱいに表示（アスペクト比維持）
        private void Fit_Click(object sender, RoutedEventArgs e)
        {
            ImageView.Stretch = System.Windows.Media.Stretch.Uniform;
            VideoView.Stretch = System.Windows.Media.Stretch.Uniform;
        }

        // 等倍表示（1:1ピクセル）
        private void Original_Click(object sender, RoutedEventArgs e)
        {
            ImageView.Stretch = System.Windows.Media.Stretch.None;
            VideoView.Stretch = System.Windows.Media.Stretch.None;
        }

        // 再生
        private void Play_Click(object sender, RoutedEventArgs e)
        {
            VideoView.Play();
        }

        // 停止
        private void Stop_Click(object sender, RoutedEventArgs e)
        {
            VideoView.Stop();
        }


        // 再生終了時に自動ループ
        private void VideoView_MediaEnded(object sender, RoutedEventArgs e)
        {
            VideoView.Position = TimeSpan.Zero; // 先頭に戻す
            VideoView.Play();                   // 再生再開
        }
    }
}