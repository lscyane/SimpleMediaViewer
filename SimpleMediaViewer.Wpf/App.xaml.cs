using System.Configuration;
using System.Data;
using System.Windows;

namespace SimpleMediaViewer.Wpf
{
    /// <summary>
    /// Interaction logic for App.xaml
    /// </summary>
    public partial class App : Application
    {
        private void Application_Startup(object sender, StartupEventArgs e)
        {
            // コマンドライン引数から動画パス取得
            // 例: VideoPreview.exe sample.mp4
            var filePath = e.Args.FirstOrDefault();
        }
    }

}
