using CommunityToolkit.Mvvm.DependencyInjection;
using LibVLCSharp.Shared;
using lscyane.Wpf.Controls;
using Microsoft.Extensions.DependencyInjection;
using System.Configuration;
using System.Data;
using System.Globalization;
using System.Windows;

namespace SimpleMediaViewer.Wpf
{
    /// <summary>
    /// Interaction logic for App.xaml
    /// </summary>
    public partial class App : Application
    {

        /// <summary>
        /// サービスプロバイダ
        /// </summary>
        public IServiceProvider Services { get; }


        /// <summary>
        /// コンストラクタ
        /// </summary>
        public App()
        {
            // Exception イベント (未処理例外を最後に処理)
            App.Current.DispatcherUnhandledException += Current_DispatcherUnhandledException;

            // 文字コードプロバイダの登録 (Shift_JIS 等を使用可能にする)
            System.Text.Encoding.RegisterProvider(System.Text.CodePagesEncodingProvider.Instance);

            Services = ConfigureServices();
            Ioc.Default.ConfigureServices(Services);
        }


        /// <summary>
        /// サービスをここで登録する
        /// </summary>
        /// <returns></returns>
        private static ServiceProvider ConfigureServices()
        {
            var services = new ServiceCollection();

            // ViewModel
            services.AddSingleton<IMainWindowViewModel, MainWindowViewModel>();

            return services.BuildServiceProvider();
        }


        protected override void OnStartup(StartupEventArgs e)
        {
            base.OnStartup(e);

            // LibVLCSharpの初期化
            Core.Initialize();
        }


        /// <summary>
        /// スタートアップイベント (アプリケーション開始時に実行される)
        /// </summary>
        /// <param name="sender"></param>
        /// <param name="e"></param>
        private async void Application_Startup(object sender, StartupEventArgs e)
        {
            // NOTE: ロケールの指定(全ての他のスレッドが開始される前に呼ぶ必要がある)
            CultureInfo.DefaultThreadCurrentCulture = CultureInfo.InvariantCulture;

            // 言語を設定
            // TODO // Core.Properties.ResourcesService.Current.ChangeCulture(Core.Properties.UserSettings.Default.LanguageID);

            // ToolTipが表示されるまでの時間のグローバル設定を変更
            System.Windows.Controls.ToolTipService.InitialShowDelayProperty.OverrideMetadata(
                typeof(DependencyObject),
                new FrameworkPropertyMetadata(100));

            // ToolTip表示継続時間のグローバル設定を変更
            System.Windows.Controls.ToolTipService.ShowDurationProperty.OverrideMetadata(
                typeof(DependencyObject),
                new FrameworkPropertyMetadata(int.MaxValue));

            // user.configのパスを取得
            Configuration appSettings = ConfigurationManager.OpenExeConfiguration(System.Configuration.ConfigurationUserLevel.PerUserRoamingAndLocal);
            // TODO // Models.DebugLog.Instance.WriteLine(Models.DebugLog.EType.Info, "user.config path: " + appSettings.FilePath);

            // 関連付けをしたファイルに大して、中アイコン以上の表示時にサムネイルを表示するためのレジストリ登録
            FileAssociationHelper.RegisterAppIdIfNeeded();

            // コマンドライン引数からファイルパス取得
            var filePath = e.Args.FirstOrDefault(x => System.IO.File.Exists(x));

            // MainWindowが作成された後に初期ファイルを読み込む
            MainWindow ??= new MainWindow();
            if (MainWindow is MainWindow mainWindow && !string.IsNullOrEmpty(filePath))
            {
                // ウィンドウが完全にロードされた後に実行
                mainWindow.Loaded += async (s, args) =>
                {
                    await mainWindow.LoadInitialFileAsync(filePath);
                };
            }
            MainWindow.Show();
        }


        /// <summary>
        /// アプリケーション終了時（<see cref="Application.Exit"/>）に実行されるイベントハンドラ
        /// </summary>
        /// <param name="sender"></param>
        /// <param name="e"></param>
        private void Application_Exit(object sender, ExitEventArgs e)
        {
            SimpleMediaViewer.Wpf.Properties.EnvSettings.Default.Version = System.Reflection.Assembly.GetExecutingAssembly().GetName().Version?.ToString();
            SimpleMediaViewer.Wpf.Properties.EnvSettings.Default.Save();
        }


        /// <summary>
        /// Exception イベント (未処理例外を最後に処理)
        /// </summary>
        /// <param name="sender"></param>
        /// <param name="e"></param>
        private static void Current_DispatcherUnhandledException(object sender, System.Windows.Threading.DispatcherUnhandledExceptionEventArgs e)
        {
            // TODO // Models.DebugLog.Instance.WriteLine(Models.DebugLog.EType.Error, "Unhandled Exception catched\r\nProgram Termination", e.ToString());
            OwneredMessageBox.Show(e.Exception.ToString(), "Unhandled Exception catched\r\nProgram Termination");
        }
    }
}
