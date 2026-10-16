# Yerel WinUI 3 ayarları — iç geliştirme notları

Bu ayrı C++/WinRT uygulaması, sürekli çalışan Win32 panelinden bağımsız açılır.
Pencere kapanınca `Application::Exit()` çağrılır; ayarlar uygulaması arka planda
beklemez. Ürün yürütülebilir dosyası C++'tır; C# veya .NET uygulaması içermez.

Gerçek WinUI `NavigationView`, `ListView`, `TextBox`, `ToggleSwitch`, `NumberBox`,
`TimePicker` ve `InfoBar` kontrolleri kullanılır. Widget ekleme, kaldırma, sıralama,
etiket, saat dilimi, tarih, süre, genişlik ve görünürlük alanları ortak
`native/core/Configuration.h` modeli üzerinden düzenlenir. Bilinmeyen widget türleri
ve JSON alanları korunur.

`Uygula`, ortak `SettingsStore` ile beklenen dosya sürümünü kontrol ederek atomik
kaydetme yapar. Başka bir işlem dosyayı değiştirdiyse çakışma gösterilir; değişiklikler
sessizce üzerine yazılmaz. `Geri al` diskteki dosyayı yeniden yükler. Bozuk veya
geçersiz dosya yüklenirse düzenleme ve kaydetme kapatılır; varsayılanlar bu dosyanın
üzerine yazılmaz. Panel aynı dosyayı izler. Kalıcı ayarlar için normal yol
`%LOCALAPPDATA%\w11widget\settings.json` konumudur.

Yerleşim gerçek `TaskbarLocator` gözlemine dayanır. Monitör, DPI ve görev çubuğu
koordinatları açık bölge onayının bağlamına kaydedilir. Yerleşim değiştiğinde veya
gözlem bu bağlamla uyuşmadığında onay düşer. Önizleme simgelerin boşluğunu algılamaz;
kullanıcı onayı olmadan paneli gösterme yetkisi üretmez. Kompakt önayarlar da onay
vermez. Tema ayarlar penceresinde anında önizlenir; panel teması ve opaklığı
`Uygula` ile kaydedilir.

## Derleme

Gereken araçlar: Visual Studio 2022 v143 C++ araçları, Windows SDK 10.0.26100.0,
MSBuild ve NuGet CLI. Build Tools kurulumunda
`Microsoft.VisualStudio.ComponentGroup.UWP.BuildTools` bileşeni de gerekir.
Yalnız `UWP.VC.BuildTools` veya `WindowsAppDevelopment.VC.BuildTools` seçmek,
WinUI projesinin kullandığı x64 Windows Store MSBuild hedeflerini getirmez.
Bu eksiklik, SDK diskte bulunsa bile `TargetPlatformVersion=7.0` ve `MSB8036`
ile görünebilir; SDK sürümünü komut satırında zorlayarak gizlenmemelidir.
Paketler `packages.config` içinde tam sürüme sabittir.
`1.8` bileşenleri, ilk prototipte VS2022 ile bilinen araç zincirini kullanmak için
seçilmiştir; en yeni üretim sürümü oldukları iddia edilmez.

Depo kökünden PowerShell:

```powershell
nuget.exe restore native\settings\packages.config `
  -PackagesDirectory native\settings\packages `
  -NonInteractive -Source https://api.nuget.org/v3/index.json

& 'C:\BuildTools2022\MSBuild\Current\Bin\MSBuild.exe' `
  native\settings\w11widget_native_settings.vcxproj `
  /t:Rebuild /m /p:Configuration=Release /p:Platform=x64 /v:minimal
```

Doğrulama ve paketleme öncesinde tam `Rebuild` gerekir;
`scripts/build-native.ps1` bunu uygular. MSBuild
[FileTracker](https://github.com/dotnet/msbuild/blob/main/src/Utilities/TrackedDependencies/FileTracker.cs#L202-L228),
`%LOCALAPPDATA%` altındaki başlıkları bağımlılık izlemesinden dışlar. Kaynak bu
dizindeyse artımlı derleme, ortak yapı değişikliklerinden sonra eski nesneleri
bırakıp ABI uyumsuzluğu oluşturabilir. Tam derleme bu karışımı önler.

Çıktı: `native/settings/out/x64/Release/w11widget_native_settings.exe`.
`packages`, `obj` ve `out` bu alt dizinde kalır ve git tarafından dışlanır.
Self-contained çıktıdaki DLL, PRI ve diğer çalışma zamanı dosyaları EXE ile
birlikte taşınmalıdır. Yalnız EXE'yi kopyalamak desteklenmez. Uygulama paket kaydı
veya Windows App Runtime yükleyicisi çalıştırmaz.
Bu self-contained seçeneği Windows App SDK içindir. Ham EXE ayrıca
`msvcp140.dll`, `vcruntime140.dll` ve `vcruntime140_1.dll` kullanır. Taşınabilir
paket hazırlarken `scripts/package-native.ps1` ile yalnız Visual
Studio'nun `VC\Redist\MSVC\<sürüm>\x64\Microsoft.VC143.CRT` dizinindeki
dağıtılabilir CRT dosyaları eklenir; System32 veya derleyici dizininden DLL alınmaz.
Paketleyici x64 PE türünü, kopyaların SHA-256 değerlerini ve statik DLL
bağımlılıklarını kontrol eder; canlı çalışma testi yerine geçmez.

Doğrulanmış panel EXE’si ve ayar çıktı diziniyle, yeni bir çıktı dizinine paketleme:

```powershell
.\scripts\package-native.ps1 `
  -PanelExecutable $panelExecutable `
  -SettingsDirectory .\native\settings\out\x64\Release `
  -OutputDirectory .\build-native-package `
  -SourceRevision (git rev-parse HEAD) -VerifyInstaller
```

Başlık, EXE `ProductVersion` bilgisi ve derlenen manifest depo kökündeki `VERSION`
dosyasından gelir. `app.manifest` kaynak şablondur; `obj` altındaki sürümlü kopya
MSBuild tarafından üretilir. Paketleyici kaynak sürümü ile gerçek EXE sürümünü
karşılaştırır. Panel aynı dizindeki `w11widget_native_settings.exe` dosyasını açar
ve kullanılan ayar dosyasını `--settings-file` ile aktarır. `--widget-id` doğrudan
ilgili widget’ın düzenleyicisini seçer. Zaten açık pencere için
`native/panel/ProductionPanel.cpp` ve `App.xaml.cpp`, `WM_COPYDATA` etiketi
`0x57313157` ile aynı protokolü kullanır: en fazla 64 ASCII kimlik karakteri ve
sonunda NUL bulunan UTF-16 yük. Alıcı yalnız mevcut widget’ı seçer; bu mesaj
ayar kaydetmez, dosya açmaz ve komut çalıştırmaz. Bu eşleşme iki tarafta birlikte
korunmalıdır.

## Ölçüm ve doğrulama

Normal açılış:

```powershell
.\native\settings\out\x64\Release\w11widget_native_settings.exe
```

Kendiliğinden kapanan, GUI'si gerçekten açılan ölçüm:

```powershell
.\native\settings\out\x64\Release\w11widget_native_settings.exe `
  --settings-file "C:\Temp\w11widget-test\settings.json" --duration-seconds 30
```

`--duration-seconds` 1–3600 arası tam sayı kabul eder. Süre verilmezse zamanlayıcı
oluşturulmaz. Ölçüm, işlem başlangıcından görünür pencereye kadar süreyi, özel
belleği/çalışma kümesini, CPU zamanını ve kapanıştan sonra ayarlar PID'sinin
kalmadığını ayrı ayrı kaydetmelidir. Panel açık kalırken ayarlar penceresini
kapatmak, on-demand tasarımın temel sınamasıdır.

UI doğrulaması; üç sayfa arasında geçiş, widget ekleme/sıralama/kaldırma, açık/koyu
tema, Tab ile gezinme, Narrator etiketleri, yüksek karşıtlık ve farklı ekran
ölçeklerinde kesilme kontrolünü kapsar. Native Windows derlemesi ve canlı GUI
ölçümü olmadan uygulamanın çalıştığı kabul edilmez.

İzole üretim UI testi, yeni çıktı dizini içinde kendi fixture dosyasını üretir;
gerçek kullanıcı profilinin değişmediğini de doğrular. Etkileşimli masaüstünde:

```powershell
powershell.exe -NoProfile -MTA -File .\native\settings\test_settings_ui.ps1 `
  -SettingsExecutable .\native\settings\out\x64\Release\w11widget_native_settings.exe `
  -OutputDirectory .\build-check\native-settings-ui -CaptureWindows
```

Test; eşit UTC satırlarını, typed alanları, etkinlik/görünürlük, sıralama,
tema/önayar, atomik kaydetme, geri alma, hatalı saat dilimi, stale dosya çakışması,
yeniden açılış, `--widget-id`, açık pencerede doğrulanan widget seçimi, bilinmeyen
alan korunumu ve bozuk dosyanın üzerine
yazılmamasını sınar. Görev çubuğunun boşluğunu kullanıcı adına onaylamaz.
Panel ve ayarların birlikte sınanması için `native/panel/test_settings_lifecycle.ps1`
yalnız açık `-Production` seçeneğiyle çalıştırılır; bu mod da izole profil üretir.
Seçeneksiz varsayılan akış eski prototip içindir ve üretim ayarlarıyla kullanılmaz.

UIA istemcisi MTA iş parçacığı kullanır ve Windows PowerShell'in .NET UIA
istemci sağlayıcılarını açıkça kaydeder. Bu kayıt eksikse standart Win32 düğmesi
`Pane` olarak görünebilir ve `InvokePattern` bulunmayabilir. Test bu hatayı
kontrol türünü gevşeterek geçmez; gerçek düğme ve etkileşim desenini doğrular.

Widget listesinde her satır ayrı `ListViewItem` nesnesidir. Eşit etiketleri yalın
string öğeleri olarak bağlamak, [WinUI issue #11226](https://github.com/microsoft/microsoft-ui-xaml/issues/11226)
ile belgelenen UIA kardeş döngüsünü tetikleyebilir. Aynı adlı iki UTC saati
geçerli girdidir; benzersizlik satır kimliğinde sağlanır, kullanıcı etiketi
değiştirilmez. Etiket düzenleme mevcut satırın içeriğini ve erişilebilir adını
günceller. UI testi aynı etiketli iki satırı ağaçtan gerçekten okur; sorunu
kontrolü atlayarak veya etiketleri değiştirerek gizlemez.

## Sabit bağımlılıklar

| Paket | Sürüm |
| --- | --- |
| Microsoft.Windows.CppWinRT | 2.0.250303.1 |
| Microsoft.WindowsAppSDK.WinUI | 1.8.260803003 |
| Microsoft.WindowsAppSDK.Foundation | 1.8.260803002 |
| Microsoft.WindowsAppSDK.Base | 1.8.251216001 |
| Microsoft.WindowsAppSDK.InteractiveExperiences | 1.8.260708001 |
| Microsoft.Windows.SDK.BuildTools | 10.0.26100.4654 |
| Microsoft.Windows.SDK.BuildTools.MSIX | 1.7.20250829.1 |
| Microsoft.Web.WebView2 | 1.0.3179.45 |

WebView2 SDK, WinUI paketinin geçişli derleme bağımlılığıdır; bu uygulama WebView2
kontrolü oluşturmaz veya bir tarayıcı çalıştırmaz. Uygulamaya AI/ML paketlerini
getiren üst WindowsAppSDK meta paketi yerine yalnız UI bileşenleri kullanılır.

Proje düzeni için [Microsoft C++/WinRT uygulama modeli](https://learn.microsoft.com/en-us/windows/apps/develop/cpp-winrt/author-apis)
ve [resmi WinUI C++ örneği](https://github.com/microsoft/WindowsAppSDK-Samples/tree/main/Samples/Windowing/cpp/cpp-winui/SampleApp)
incelenmiştir. Sürüm ve bağımlılıklar NuGet'in gerçek `flatcontainer` paket
bildirimlerinden doğrulanır; web arama özetleri derleme kanıtı sayılmaz.
