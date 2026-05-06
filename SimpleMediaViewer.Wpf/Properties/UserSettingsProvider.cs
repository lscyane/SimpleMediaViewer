using System.Collections.Specialized;
using System.ComponentModel;
using System.Configuration;
using System.Diagnostics;
using System.Reflection;
using System.Xml;
using System.Xml.Linq;
using System.Xml.Serialization;
using System.Xml.XPath;

namespace SimpleMediaViewer.Wpf.Properties;


// 参考情報源 : https://so-zou.jp/software/tech/programming/c-sharp/deploying/application-settings.htm


// NOTE: Settings.Designerファイルを自動生成する際に消えてしまう処理を残すためのクラス
[global::System.Configuration.SettingsProviderAttribute(typeof(UserSettingsProvider))]
sealed partial class UserSettings : global::System.Configuration.ApplicationSettingsBase
{ 
}


[Obfuscation]
public class UserSettingsProvider : lscyane.Core.Properties.CustomSettingsProvider
{
    protected override string FileName => "user";
}
