# Yerel WinUI 3 ayarlar prototipi — iç geliştirme notları

Bu ayrı C++/WinRT uygulaması, sürekli çalışan Win32 panelinden bağımsız açılır.
Pencere kapanınca `Application::Exit()` çağrılır; ayarlar uygulaması arka planda
beklemez. Ürün yürütülebilir dosyası C++'tır; C# veya .NET uygulaması içermez.

Prototipte gerçek WinUI `NavigationView`, `ListView`, `TextBox`, `ToggleSwitch`,
`NumberBox`, `TimePicker` ve `InfoBar` kontrolleri kullanılır. Widget ekleme,
kaldırma, sıralama, etiket düzenleme ve tema seçimi oturum içinde çalışır.
Yerleşim ve opaklık değerleri de oturum içinde tutulur. Her sayfada değişikliklerin
kaydedilmediği ve çalışan paneli değiştirmediği açıkça yazılır.

Kalıcı JSON, Qt ayarlarının göçü, panelle IPC ve gerçek monitör seçimi bu ölçüm
prototipinin kapsamına dahil değildir. Prototip mevcut kullanıcı ayarlarını,
otomatik başlatmayı veya görev çubuğu yerleşimini değiştirmez. Bu sınır, mimarinin
ilk açılış, açıkken bekleme ve kapandıktan sonra kaynak tüketimini izole etmek içindir.

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
  /m /p:Configuration=Release /p:Platform=x64 /v:minimal
```

Çıktı: `native/settings/out/x64/Release/w11widget_native_settings.exe`.
`packages`, `obj` ve `out` bu alt dizinde kalır ve git tarafından dışlanır.
Self-contained çıktıdaki DLL, PRI ve diğer çalışma zamanı dosyaları EXE ile
birlikte taşınmalıdır. Yalnız EXE'yi kopyalamak desteklenmez. Uygulama paket kaydı
veya Windows App Runtime yükleyicisi çalıştırmaz.
Bu self-contained seçeneği Windows App SDK içindir. Ham EXE ayrıca
`msvcp140.dll`, `vcruntime140.dll` ve `vcruntime140_1.dll` kullanır. Taşınabilir
prototip hazırlarken `scripts/package-native-prototype.ps1` ile yalnız Visual
Studio'nun `VC\Redist\MSVC\<sürüm>\x64\Microsoft.VC143.CRT` dizinindeki
dağıtılabilir CRT dosyaları eklenir; System32 veya derleyici dizininden DLL alınmaz.
Paketleyici x64 PE türünü, kopyaların SHA-256 değerlerini ve statik DLL
bağımlılıklarını kontrol eder; canlı çalışma testi yerine geçmez.

`$panelExecutable`, `$crtDirectory` ve `$dumpbin` doğrulanmış mutlak dosya/dizin
yollarıyken, yeni bir çıktı dizinine paketleme:

```powershell
.\scripts\package-native-prototype.ps1 `
  -PanelExecutable $panelExecutable `
  -SettingsDirectory .\native\settings\out\x64\Release `
  -CrtDirectory $crtDirectory -Dumpbin $dumpbin `
  -OutputDirectory .\build-native-package `
  -SourceRevision (git rev-parse HEAD)
```

Native panel prototipinin ayarlar düğmesi aynı dizindeki
`w11widget_native_settings.exe` dosyasını açar. Birleşik ölçüm için panel EXE'si
ayarların tam çıktı dizinine kopyalanabilir.

## Ölçüm ve doğrulama

Normal açılış:

```powershell
.\native\settings\out\x64\Release\w11widget_native_settings.exe
```

Kendiliğinden kapanan, GUI'si gerçekten açılan ölçüm:

```powershell
.\native\settings\out\x64\Release\w11widget_native_settings.exe --duration-seconds 30
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

Panelin tam ayar çıktısının yanında olduğu etkileşimli masaüstü oturumunda:

```powershell
powershell.exe -NoProfile -MTA -File .\native\panel\test_settings_lifecycle.ps1 `
  -PanelExecutable $panelExecutable `
  -OutputDirectory .\build-check\native-lifecycle `
  -ExerciseSettingsUi -CaptureWindows
```

UIA istemcisi MTA iş parçacığı kullanır ve Windows PowerShell'in .NET UIA
istemci sağlayıcılarını açıkça kaydeder. Bu kayıt eksikse standart Win32 düğmesi
`Pane` olarak görünebilir ve `InvokePattern` bulunmayabilir. Test bu hatayı
kontrol türünü gevşeterek geçmez; gerçek düğme ve etkileşim desenini doğrular.

Widget listesinde her satır ayrı `ListViewItem` nesnesidir. Eşit etiketleri yalın
string öğeleri olarak bağlamak, [WinUI issue #11226](https://github.com/microsoft/microsoft-ui-xaml/issues/11226)
ile belgelenen UIA kardeş döngüsünü tetikleyebilir. Aynı adlı iki UTC saati
geçerli girdidir; benzersizlik satır kimliğinde sağlanır, kullanıcı etiketi
değiştirilmez. Etiket düzenleme mevcut satırın içeriğini ve erişilebilir adını
günceller. Canlı test iki UTC satırını, 4→5→4 akışını ve işlem kapanışını doğrular.

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
