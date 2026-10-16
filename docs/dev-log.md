# Geliştirme günlüğü

## 17.10.2026 — 0.7.0 native teslim doğrulaması

Üretim kaynak commit'i `b6766a0`; ayrı sürüm commit'i
`cf38c6323fbca434b72e0f82a227d2c44d621ed3`. Bütün derleme/paket sürümleri tek
`VERSION` girdisinden 0.7.0 alır. Bu bölüm güncel teslim kaydıdır; aşağıdaki
16 Ekim kayıtları aşama geçmişidir. Açıklamalı yerel `v0.7.0` etiketi paketle
aynı `cf38c63` kaynak commit'ini işaretler. Remote değiştirilmedi; push yapılmadı.

### Final derleme, paket ve UI kanıtı

- Yerel kapı, 00:02 +03, temiz `cf38c63`: native 5/5 CTest (0,01 sn), isteğe
  bağlı Qt referansı 9/9 (0,49 sn), exit 0. macOS'ta Windows adaptörleri ve WinUI
  açık SKIP; onların Windows sonuçları aşağıdadır.
- Windows temiz native build: 10/10 CTest, 1,72 sn, exit 0. WinUI tam Rebuild
  bütün kaynakları yeniden derledi. Kaynak arşivindeki 152 dosya derleme öncesi,
  paket öncesi ve paket sonrasında SHA-256 ile aynı commit'e eşleşti.
- 00:05 +03 final paket: 261 payload dosyası, 58.098.185 bayt; 999 statik DLL
  import çözümü. 6 kurulum × 263 SHA-256, 6 temiz PATH smoke, eşzamanlı kurulum,
  kilitli eski dosya/sentinel profil koruma, başarısız kurulum onarımı PASS.
  GUI otomatik açılışı bastırma opt-in senaryosu paketleme kapısında SKIP.
- Installer 21.709.312 bayt; SHA-256
  `5a3dfd3e1f5dcfd2d424d243980f830cbd5c8e4884836d10b1298319bd4495cc`.
  ZIP 22.532.217 bayt; SHA-256
  `29eb0546c3a2f66f5e2c6b4668dfc1797c9a8d3b1f24ca5c15a4a267a82b83c2`.
  Yerelde indirilen iki artifact hash'i, ZIP CRC ve 263 arşiv girdisi doğrulandı.
  Dosyalar: `build-check/artifacts/release-native-0.7.0/`.
- Final 0.7 paketinden WinUI 15/15 GUI kontrolü ve panelin 7 üretim senaryosu
  ile ayarlar süreç yaşam döngüsü PASS. Gerçek profil değişmedi, tüm test süreçleri
  normal kapandı. Panel hazır olma ölçümü 230,113 ms; runtime hata bayrağı false.
  Kanıt: `build-check/measurements/native-release-0.7.0/{settings-ui,panel-ui}/`.
- Gerçek final Windows görselleri incelendi ve `docs/screenshots/native-0.7-*.png`
  altında sürüm kontrollü saklandı. Önceki Qt ekranları tarihsel karşılaştırma
  için korundu. Yeni ekranlar sentetik test profilini gösterir.
- Gerçek görev çubuğu dar çekimi: 2560×48, DPI96, DISPLAY1 2560×1440;
  panel bölgesi `[136,1400–473,1432]` çekim anında boş. Odağın HWND'si ve mevcut
  profil hash'i değişmedi. Kanıt: `build-check/measurements/taskbar-before/`.
  Bu özel çekim otomatik onay denetiminde iki kez reddedildi; kullanıcıdan
  işlem bazlı onay alındıktan sonra tamamlandı ve geçici görev kaldırıldı.

### Üretim tüketim ölçümü

17 Ekim 00:08–00:19 +03, aynı zion-based oturumunda native önce, Qt sonra;
12 mantıksal işlemci, 5 saniye ısınma, 1 saniye örnekleme. Panel başına
300 saniye/298 örnek; yalnız başlatılan ana süreç ölçüldü, ilk açılış ve alt
süreç/GPU/toplam sistem tüketimi kapsam dışıdır. İki profil de bir
geri sayım ve üç dünya saati içerir; native önizleme 620 DIP, Qt benchmark
520 piksel genişliğindedir. Sonuç tek koşudur, eşzamanlı veya aynı piksel
yerleşiminde kontrollü genel performans garantisi değildir.

| Süreç | Tepe özel bellek | Tepe çalışma kümesi | CPU süresi | Tek çekirdek CPU |
| --- | ---: | ---: | ---: | ---: |
| Native 0.7 panel | 53,34 MiB | 44,09 MiB | 0,171875 sn | %0,057291 |
| Qt 0.6.2 referans | 167,27 MiB | 86,83 MiB | 0,546875 sn | %0,182291 |
| WinUI ayarlar, 60 sn/61 örnek | 88,59 MiB | 100,95 MiB | 0,109375 sn | %0,182288 |

Native ve WinUI süreli çalışma sonunda normal kapandı; Qt benchmark ölçüm
sonrası kendi PID'siyle sonlandırıldı. Qt TTL 330 sn, native TTL 308 sn
olduğundan beş saniyelik cleanup penceresi aynı değildir; bu sonuç Qt
kapanış hatası olarak yorumlanmaz. Qt'nin ilk özel bellek örneği 83,49 MiB,
son örneği 167,27 MiB; kısa prototip ölçümüyle tepe farkının nedeni bu koşuda
gözlenen büyümedir, kök nedeni bu teslim kapsamında teşhis edilmedi.
Gerçek profil hash'i aynı kaldı. Ham kanıt:
`build-check/measurements/native-release-0.7.0/visible/`.

Gizli durum ayrı 60 saniyelik koşuda sınandı: her süreçte 61/61 örnekte
gizlilik doğrulandı. Native son özel bellek 53,34 MiB (tepe 53,43), Qt 84,12
MiB (tepe 84,27); ikisinde de ölçülen CPU artışı 0,000 sn. Bu değer ölçüm
çözünürlüğünde sıfırdır. Native normal kapandı; Qt'nin yalnız test PID'si
aynı TTL/cleanup farkıyla sonlandırıldı. Profil değişmedi, geçici görev silindi.
Kanıt: `build-check/measurements/native-release-0.7.0/hidden/`.

### Gerçek kullanıcı kurulumu

00:25 +03 gerçek kullanıcı kurulumu exit 0: yeni dizin
`%LOCALAPPDATA%\w11widget\app\0.7.0-B00D4369-2DCE-41EF-A9E4-57E5C65BA61D`.
263 yeni dosyanın hash'i paketle eşleşti; önceki 0.6.2 kurulumunun 1.584
dosyası, profil ve başlangıç kayıtları değişmedi. Önceki dizin korunuyor.
Kurulum `--quiet --no-launch` ile yapıldı; setup log `ready/skipped-no-launch`.
Kurulum kanıtı: `build-check/measurements/native-release-0.7.0/installation.json`.

Kurulum öncesi byte doğrulanmış profil yedeği:
`%LOCALAPPDATA%\w11widget-dev\installation-native-0.7.0\rollback\settings.json`.
Eski dosyaların hash manifesti aynı dizinde `old-install-files.json`.

İlk iki gerçek profil UI denemesi test betiğinin managed UIA
`GetCurrentSelection` çağrısı ve çok erken `MainModule` sorgusu nedeniyle
başarısız oldu; PASS sayılmadı. Seçim
`SelectionPattern.Current.GetSelection()` ile, süreç kimliği pencere hazır
olduktan sonra tutulan handle üzerinden `QueryFullProcessImageNameW` ile
doğrulanıyor. Her iki denemede de profil aynı kaldı; sahipliği doğrulanan
test paneli normal Çıkış ile kapatıldı. Uygulama binary'leri değiştirilmedi.
Üçüncü denemede TimePicker erişilebilir adı içindeki U+200E yönlendirme
işaretleri saat karşılaştırmasını bozdu. Ekran 18:00 gösteriyordu; yalnız
yönlendirme işaretleri testte normalize edildi. Ölçülen erişilebilir adla
18:00 kabul / 19:00 ret kontrolü geçti. Bu deneme de PASS sayılmadı.

00:33 +03 `installed-ui-04`: altı işlev grubu PASS, exit 0. Gerçek üç
widget'ın kimlikleri, sırası, etiketleri ve tür bazlı ayarları birebir korundu;
geometri ve görünüm değerleri değişmedi. Onaylanan boş bölgenin bağlamı ayar
arayüzünden kaydedildi. Panel `[136,1400–473,1432]` konumunda; ayarlar normal
kapandı, panel kaldı. İlk widget UIA Invoke ile açıldı, popup görüntülendi ve
Escape ile kapandı. Beş ekran incelendi. Native runtime hazır süresi
231,889 ms, `profile_valid=true`, `runtime_failed=false`.
Kanıt: `build-check/measurements/native-release-0.7.0/installed-ui-04/`.

00:34 +03 `installed-rollback`: dört kontrol grubu PASS. Testin kendi 0.7
paneli normal Çıkış ile kapandı; korunan 0.6.2 uygulamasının ayarları görünür
olarak açıldı ve kendi Çıkış düğmesiyle exit 0 kapandı. Profil baytları aynı
kaldı. Kurulu 0.7 yeniden açıldı, tam taskbar konumu doğrulandı ve çalışır
bırakıldı (PID 7068, başlangıç 00:34:10 +03). İki ekran incelendi.
Kanıt: `build-check/measurements/native-release-0.7.0/installed-rollback/`.

00:35 +03 kurulum sonrası görev çubuğu çekimi de incelendi: 2560×48,
DPI96; panel hava durumu, Başlat ve uygulama simgeleriyle örtüşmüyor.
Çekim öncesi/sonrası odak HWND'si, profil hash'i ve kurulu panel PID'si aynı.
Kanıt: `build-check/measurements/native-release-0.7.0/taskbar-after/`.

00:37 +03 tamamlanan 22 test görevi, komut/durum/tetikleyici guard'ları ve
22/22 SHA-256 doğrulanmış XML yedeğiyle kaldırıldı. Çekim ve gizli ölçüm
görevleri de kendi doğrulamalarından sonra kaldırıldı; süreçler durdurulmadı.
00:38 +03 son salt okunur kontrol PASS: doğru kurulumdan tek 0.7 paneli
(PID 7068), sıfır ayarlar süreci, sıfır geçici w11widget görevi; profil ve
kurulum öncesi yedek hash'leri beklenen değerlerle aynı.
Kanıt: `build-check/measurements/native-release-0.7.0/final-running-state.json`
ve `task-cleanup-{21,rollback}/`.

### Kapsam sınırları

Temiz ikinci Windows kurulumu, Narrator, fiziksel çoklu ekran/DPI değişimi,
gerçek uyku/dönüş ve Explorer yeniden başlatması bu teslimde çalıştırılmadı.
Geometri ve ilgili yaşam döngüsü hesaplarının başsız testleri, gerçek donanım
senaryolarının yerine sayılmaz. İncelenen görev çubuğu alanı bu oturumun
geometrisine aittir; simgelerin gelecekteki yer değişimini otomatik algılama
özelliği yoktur. Altı ek widget katalogda planlıdır; sağlayıcı/izin/yaşam
döngüsü tamamlanmadan etkinleştirilmez. İmzalama ve genel yayın yapılmadı.

## 16.10.2026 — Onaylanan native 0.7 üretim geçişi

Kullanıcı prototip ölçümlerinin ardından yerel mimariyle 0.7 üretimini, zion-based
kurulumunu ve testi onayladı. Bu bölüm üretim geçişinin geliştirme geçmişidir;
son teslim sonucu üstteki 17 Ekim kaydındadır. Kaynak tabanı `29c7df1` + üretim çalışma ağacı.

### Uygulanan üretim davranışı

- Standart C++ yapılandırma/şema göçü, bilinmeyen alan koruması, atomik ve yedekli
  kayıt, çakışan düzenleme koruması; OS ICU ile IANA/DST ve tek seferlik geri sayım.
- Native panelde dünya saati, geri sayım, monoton odak sayacı; UIA düğmeleri ve
  ekran içine yerleşen popup'lar. Gizleme ve uyku odak sayacını duraklatır.
- Taskbar alanı bağlam/DPI ile onaylanır; değişiklikte güvenli önizlemeye dönülür.
  Taşan widget'lar menüye alınır. Explorer değiştirilmez; bildirimler olay güdümlüdür.
- WinUI ayarlarında tür bazlı düzenleme, ekle/kaldır/sırala, görünürlük, yerleşim
  önizlemesi, hazır konumlar, tema, saydamlık, Uygula/Geri al ve açık hata durumları.
- İsteğe bağlı ayarlar süreci; tekrar açma aynı süreçte ilgili widget'ı seçer.
- Üç çalışan widget, altı planlı katalog girdisi. Planlı sağlayıcılar hazır özellik
  gibi gösterilmez. Qt kaynakları referans/geri dönüş için yerinde korunur.
- Tek `VERSION` girdisi, Qt gerektirmeyen native derleme/paketleme, self-contained
  WinUI/CRT, kullanıcı başına sürümlü kurulum ve taşınabilir ZIP.

### Üretim sırasında bulunan hatalar

- COM apartment'ını her JSON çağrısında açıp kapatmak C++/WinRT factory cache'iyle
  ikinci kayıtta erişim ihlali doğuruyordu. Apartment artık çağıran thread'in ömrü
  boyunca açık; testler ardışık kayıt ve eşzamanlı yazmayı kapsıyor.
- Standart Win32 UIA, odak düğmeleri arasında geçerken popup'ı geçici olarak
  deaktive ediyordu; anında kapatma Sıfırla işlemini yutuyordu. Kapanma 150 ms
  tek atımlık odak kontrolüyle kararlaştırılıyor; tekrar aktivasyon iptal eder.
  `native-production-panel-04` reset/gizleme testi geçti; sonra test kodunun
  PowerShell 5.1 `File.Replace(...,$null)` boş-yol sorunu bulundu ve açık yedek
  yolu verildi. Başarısız koşular PASS sayılmadı.
- Ortak yapılandırma başlığı değiştiğinde WinUI incremental derlemesi eski
  `SettingsStore.obj` dosyasını bıraktı; UI06 heap corruption ile kapandı.
  MSBuild FileTracker, `%LOCALAPPDATA%` altındaki kaynak başlıklarını bağımlılık
  takibinden dışlıyor; 2040 başlıklı tlog içinde ortak Configuration.h yoktu.
  Tam `Rebuild` bütün C++ birimlerini yeniledi. Kanonik Windows derlemesi artık
  WinUI için `Rebuild` kullanır; kaynak eşleme sonrası stale ABI engellenir.
  [Microsoft FileTracker kaynağı](https://github.com/dotnet/msbuild/blob/main/src/Utilities/TrackedDependencies/FileTracker.cs#L202-L228)
- Windows PowerShell 5.1 ZIP üretiminde Compression assembly açık yükleniyor;
  yollar `/` ile standartlaştırılıyor ve tüm arşiv dosyaları hash ile doğrulanıyor.

### Ölçülmüş ara doğrulama

- 23:53 +03 macOS taşınabilir çekirdek: 5/5 CTest, 1,05 sn, exit 0. Windows/UI
  testleri bu komutun macOS koşusunda açıkça SKIP. Qt referansı ayrı opt-in kapıda
  daha önce 9/9 geçti; son üretim derlemesinin yerine sayılmadı.
- 23:53 +03 zion-based MSVC: 10/10 CTest, 0,74 sn, exit 0; yapılandırma, odak,
  popup yerleşimi, taskbar geometrisi, shell tek örnek, kayıt, IANA/DST ve smoke.
- `native-production-settings-05`: 13 GUI kontrolü geçti; aynı adlı iki UTC,
  tipli kayıt, sıralama/görünürlük, tema/yerleşim, bilinmeyen alanlar, Geri al,
  geçersiz IANA, stale düzenleme ve bozuk profil. Gerçek profil hash'i aynı.
- `candidate-native-02`: 0.6.2 doğrulama adayı; 261 runtime dosyası / 58.094.063
  bayt, 999 statik import çözümü. 6 kurulum × 263 dosya hash'i, 6 temiz PATH
  smoke, eşzamanlı kurulum/kilitli eski dosya/profil koruma/onarım ve ZIP hash
  doğrulaması geçti. Bu aday 0.7 olarak teslim edilmez.

- `native-production-settings-07`: temiz WinUI Rebuild sonrasında 15/15 GUI
  kontrolü, task exit 0, normal profil değişmedi ve üç süreç normal kapandı.
  Bilinmeyen region_context alanı, ilk görünümde yerleşim onayı ve mevcut ayarlar
  penceresine kimlikle widget seçimi ayrıca geçti.

- `native-production-panel-07` (23:59 +03): 7 üretim GUI senaryosu ve ayarlar
  yaşam döngüsü PASS, 7,14 sn, task exit 0. Saat/geri sayım popup'ları, odak
  başlat/duraklat/sıfırla, gizlemede duraklama, atomik ayar bildirimi, tek örnek,
  açık ayarlarda widget seçimi, dış odakta kapanma, aynı ayarlar PID'sini tekrar
  kullanma ve kapat/açta yeni PID doğrulandı. Cleanup temiz; gerçek profil aynı.
  Görseller `build-check/measurements/native-production-panel-07/` ve
  `native-production-settings-07/settings-{widgets,layout,appearance}.png`.
  Üç ayar sayfası ve odak popup'ı görüntülenerek incelendi.

### Teslim doğrulamasına geçerken kalanlar (tarihsel)

0.7 sürüm commit'i, final kaynak-parite kontrolü ve
paketi, üretim tüketim ölçümü, gerçek kullanıcı kurulumu/görsel kontrol ve geri
 dönüş kanıtı henüz tamamlanmadı. Temiz Windows, fiziksel çoklu DPI/ekran,
Narrator ve gerçek uyku/Explorer yeniden başlatma bu oturumda henüz sınanmadı.

## 16.10.2026 — 0.7 hazırlığı

Kaynak başlangıcı: `ebde1d8`, Qt sürümü 0.6.2. Uygulama ve prototip değişiklikleri
yerel `00df7ef` commit'inde kayıtlıdır; native ölçüm prototipi sürümü 0.0.1'dir.
Gerçek depo `widgets/src` içindedir; üst dizin bir git deposu değildir.
Remote değiştirilmedi, push veya sürüm etiketi oluşturulmadı.

### Tamamlanan yerel temel

- Qt ayarlarında Widget'lar / Yerleşim / Görünüm gezinmesi, katalog, konum önizlemesi.
- Ortak ayrı-pencere flyout, tek seferlik geri sayım tarihi, odak sayacı.
- Geçersiz widget düzenlemesi için işlem öncesi doğrulama/geri alma; 256 KiB ayar sınırı.
- View metinleri için `locales/tr.json`; teknik çekirdek hata metinlerinin tamamı
  henüz yerelleştirilmedi.
- Canonical `./scripts/check.sh`: macOS Qt 6.11.2 ile 9/9 test PASS; Windows
  entegrasyonu bu yerel testin kapsamına girmez. Son tam yerel ölçüm 22:58:59 +03,
  temel commit `ebde1d8` + sonra `00df7ef` olarak kaydedilen çalışma ağacı;
  Qt CTest 0,46 saniye, exit 0. Aynı kapıya eklenen yerel çekirdek sözleşmesi
  1/1 CTest (15 kontrol), exit 0.
- Installer testi varsayılan olarak gerçek kullanıcı uygulamasını başlatmaz;
  canlı fixture senaryosu ayrı opt-in gerektirir.

### Görsel kanıtlar

- Önce: `docs/screenshots/panel-controls.png`, `docs/screenshots/countdown-settings.png`.
- Kaynak commit'ten yeniden üretilen önce: `build-check/ui-before/widget-settings-before.png`.
- Sonra: `build-check/ui-after/widget-settings-after.png`, `panel-after.png`,
  `worldClockFlyout-after.png`, `countdownFlyout-after.png`, `focusTimerFlyout-after.png`.
- Bu yeni görseller gerçek Qt bileşenlerinden macOS/offscreen ile alınmıştır;
  Windows masaüstü doğrulaması olarak sayılmaz. Normal ve taşan içerik için popup
  sınırı, kaydırma ve ana pencere gizlenince kapanma ayrıca GUI modunda sınandı.

### Mimari kararı ve Windows hazırlığı

Kullanıcı C++/Win32 + Direct2D panel ve yalnız gerektiğinde çalışan C++/WinUI 3
ayarlar prototipini, zion-based üzerinde karşılaştırmalı ölçümü onayladı.
Mevcut Qt geliştirmeleri karşılaştırma/geri dönüş tabanıdır. Plan:
[`0.7-mimari.md`](0.7-mimari.md).

zion-based kimliği SSH ile doğrulandı: `zion-based\\oem`, etkin console oturumu.
MSVC/SDK kurulumu `C:\BuildTools2022` altında tamamlandı: Visual Studio Build Tools
2022 17.14.42, kurulum exit 0, restart gerekmiyor. Qt 6.8.3 MSVC referansı ayrı
`%LOCALAPPDATA%\w11widget-dev\Qt` klasöründe; sistem PATH'i değiştirilmedi.

### Windows derlemesi ve ölçümler

- Qt/MSVC: 15/15 CTest PASS, 7,44 saniye. Dağıtım dizininde `--smoke-test`
  exit 0; 1.288 dosya, 83.741.878 bayt. Paket boyutu henüz optimize edilmedi.
  Kaynak arşivi SHA-256: `401c83bb53729a09befc90852489a9f1938854ca7fd9fba0bfe55ab7ea9ef910`.
- Yerel Win32 panel: 2/2 CTest PASS; son derlemede 0,18 saniye, exit 0;
  çekirdek sözleşmesi ve işletim sisteminin ICU saat dilimi/DST dönüşümü.
- 16.10.2026 22:09–22:11 +03, zion-based console oturum 1, 5 saniye ısınma
  ardından her süreç için 60 saniye / 61 örnek. Temel kaynak `ebde1d8` + çalışma
  ağacı; ölçülen EXE hash'leri JSON kanıtlarında tutulur.

| Ölçüm (ilk 60 saniyelik koşu) | Qt referansı | Yerel panel prototipi |
|---|---:|---:|
| Tepe özel bellek | 85,30 MiB | 54,85 MiB |
| Tepe çalışma kümesi | 85,48 MiB | 49,03 MiB |
| Süreç CPU zamanı | 0,046875 sn | 0,015625 sn |
| Tek mantıksal çekirdeğe göre CPU | %0,0781 | %0,0260 |
| Tepe handle / thread | 885 / 37 | 371 / 17 |

Kanıtlar: `build-check/measurements/panel-comparison-01/qt-panel.json`,
`native-panel.json`, `session-status.json`; gerçek Windows görselleri aynı
dizindeki `qt-panel.png` ve `native-panel.png`. Kullanıcı ayar hash'i değişmedi.
Geçici zamanlanmış görev koşu sonunda kaldırıldı. Qt ölçüm süreci kapanışta
yalnız kendi PID'si hedeflenerek sonlandırıldı; native panel normal kapandı.

Bu sonuç işlevsel eşdeğerlik iddiası değildir: Qt gerçek widget host/popup
altyapısını taşır, yerel örnek yalnız dört göstergeyi çizer. Qt penceresi
520×32, yerel pencere başlık dahil 636×71 pikseldir. GPU belleği ve ayar süreci
bu ölçüme dahil değildir. CPU farkı yalnız üçe karşı bir Windows zaman dilimidir;
buradan “üç kat hızlı” sonucu çıkarılmaz.

Ters sıralı ikinci koşu 22:19–22:30 +03 arasında tamamlandı: önce native, sonra
Qt; 5 saniye ısınmadan sonra her biri 300 saniye ve 298 örnek. EXE hash'leri ilk
koşuyla aynı. `build-check/measurements/panel-comparison-02/` altında JSON ve
Windows ekran görüntüleri bulunur; kullanıcı ayar hash'i yine değişmedi.

| Ölçüm (300 saniye) | Qt referansı | Yerel panel prototipi |
|---|---:|---:|
| Tepe özel bellek | 84,59 MiB | 54,11 MiB |
| Tepe çalışma kümesi | 84,82 MiB | 45,46 MiB |
| Süreç CPU zamanı | 0,15625 sn | 0,09375 sn |
| Tek mantıksal çekirdeğe göre CPU | %0,05208 | %0,03125 |
| Tepe handle / thread | 888 / 37 | 370 / 16 |

Prototipte yaklaşık %36 daha düşük tepe özel bellek ölçüldü. Qt tarafında gerçek
widget host, kalıcılık ve popup altyapısı varken native tarafında yalnız dört
gösterge vardır; bu oran tamamlanmış 0.7 için taahhüt değildir. Makine özel bir
performans laboratuvarı olarak ayrılmadı; başka derleme/kurulum işleri de vardı.
CPU süreleri Windows zaman çözünürlüğüne yakın; kesin hız karşılaştırması yapılmaz.

Gizli panel koşusu 22:31–22:33 +03, 60 saniye / 61 örnek:

| Ölçüm (gizli) | Qt referansı | Yerel panel prototipi |
|---|---:|---:|
| Tepe özel bellek | 84,80 MiB | 53,98 MiB |
| Süreç CPU zamanı | 0,046875 sn | 0 sn ölçüldü |
| Gizlilik doğrulaması | 61/61 örnek | 61/61 örnek |

Kanıt: `build-check/measurements/panel-hidden-01/`. Qt, benchmark başladıktan
1 saniye sonra gerçek `PanelWindow::hide()` yaşam döngüsünü kullanır; native
pencereler 5 saniye ısınma sonunda gizlenir. Başlangıç geçmişleri farklıdır.
Gizli süreçler ölçüm sonunda yalnız testin tuttuğu kendi PID'leriyle sonlandırıldı.
Qt gizli ölçüm EXE hash'i JSON'da ayrı tutulur; görünür ölçüm binary'si üzerine yazılmadı.

### WinUI ayarlar ve yaşam döngüsü

WinUI 3 C++/WinRT Release x64 derlemesi tamamlandı. Eksik Windows Store x64
MSBuild hedefleri, resmi `Microsoft.VisualStudio.ComponentGroup.UWP.BuildTools`
bileşeni eklenerek giderildi; SDK sürümü zorlanarak hata gizlenmedi. C++/WinRT
uygulama giriş noktası XAML derleyicisinden üretilir; üretilen dosyalar elle değişmedi.

22:35–22:36 +03 bağımsız ayarlar ölçümü: 5 saniye ısınma, 60 saniye / 61 örnek;
tepe özel bellek **86,86 MiB**, çalışma kümesi **101,79 MiB**, CPU **0,046875 sn**.
Normal pencere kapatma ile süreç sonlandı, zorla kapatma gerekmedi.
Kanıtlar: `build-check/measurements/native-settings-01/settings.json` ve
`settings.png`. Bu ölçümdeki İngilizce toggle durum metni sonraki derlemede
Türkçe Açık/Kapalı olarak düzeltildi; ölçüm dosyası eski binary hash'ini korur.

UI Automation sırasında iki ayrı sorun bulundu ve giderildi:

- Windows PowerShell istemcisi standart Win32 düğmesini `Pane` olarak görüyordu.
  Framework ile eşleşen Microsoft istemci sağlayıcısı açık yüklenip kaydedildi;
  gerçek `Button` ve `Invoke` beklentileri gevşetilmedi.
- İkinci UTC widget'ından sonra `FindAll` çağrısı `0x80131505` zaman aşımıyla
  sonuçlanıyor, WinUI süreci yüksek CPU ile yanıt vermiyordu. Bu, eşit string
  öğelerde [WinUI #11226](https://github.com/microsoft/microsoft-ui-xaml/issues/11226)
  davranışıyla örtüştü. Her satıra ayrı `ListViewItem` kimliği verildi; aynı
  kullanıcı etiketleri korunuyor. Düzeltilen senaryo iki UTC satırını gerçekten
  okuyarak geçti. Hatalı koşuların yalnız kendi süreçleri PID, tam yol, üst süreç
  ve UTC başlama zamanı doğrulanarak temizlendi.

Hata kanıtları `native-lifecycle-03` ve `native-lifecycle-04` altında korunur;
bunlar PASS sayılmaz. Test artık ilk hatayı, cleanup hatalarını, son 64 aşamayı
ve widget sayısı geçişlerini ayrı kaydeder.

Son başarılı Windows koşusu: **23:02 +03**, `00df7ef` kaynaklarına ait izole
`prototype-native-03` paketinden `native-lifecycle-07`; görev exit **0**.
4→5→4 widget, aynı adlı iki UTC, etiket düzenleme, sayfa değişiminde etiketin
korunması, yukarı/aşağı sıralama, üç sayfa ve ayrı ayarlar süreci doğrulandı.
Tekrar açma aynı PID'yi kullandı; kapatma PID'yi bitirdi, panel hayatta kaldı;
yeniden açma yeni PID üretti. İki kapatma da exit 0, kalan test süreci yok;
kullanıcı ayar hash'i değişmedi.

Gerçek Windows görselleri:
`build-check/measurements/native-lifecycle-07/settings-widgets.png`,
`settings-layout.png`, `settings-appearance.png`, `panel-after-settings-close.png`.
Üç sayfa görsel olarak incelendi; Türkçe Açık/Kapalı metinleri ve sistem temasıyla
eşleşen doğal başlık çubuğu doğrulandı. Yüksek DPI, Narrator, yüksek karşıtlık ve
tam klavye dolaşımı henüz bu prototip testinin kapsamına girmiyor.

### İzole native paket

Paket dizini: zion-based üzerinde
`%LOCALAPPDATA%\w11widget-dev\prototype-native-03`.
Başlatıcı: `w11widget_native_panel.exe`; yanındaki WinUI uygulamasını Ayarlar
düğmesi açar. Paket otomatik başlatma veya gerçek kullanıcı kurulumunu değiştirmez.

- Derleme girdilerinin 21 dosyalık SHA-256 manifesti `00df7ef` ile birebir eşleşti.
- 251 payload dosyası, **57.392.638 bayt / 54,73 MiB**; debug PDB dışarıda.
- 959 statik DLL import kaydı çözümlendi; Microsoft CRT, Visual Studio'nun
  `VC/Redist/MSVC/14.44.35112/x64/Microsoft.VC143.CRT` dizininden app-local eklendi.
- `package-manifest.json` ve `SHA256SUMS` pakette. İlk paketleme denemesinde
  Windows PowerShell 5.1 dictionary/Measure-Object uyumsuzluğu bulundu; dosya
  kayıtları `PSCustomObject` yapıldı ve yeni dizine paketleme geçti.
- Kaynak arşivi: `build-check/artifacts/w11widget-prototype-source-00df7ef.zip`,
  SHA-256 `6f381ad0585349a778b3ea3b69e78d10865aeee2bde1cc24a423b5ddf98e2657`.

Son paket ayar ölçümü 23:04–23:05 +03: 60,002 saniye / 61 örnek, tepe özel
bellek **86,98 MiB**, çalışma kümesi **98,43 MiB**, CPU **0,046875 sn**.
Normal kapatma ile PID sonlandı; zorla kapatma yok. Kanıt:
`build-check/measurements/native-settings-02/settings.json`.
`loaded-crt.json`, üç MSVC CRT DLL'sinin gerçekten paket içinden yüklendiğini ve
ayar uygulamasının alt süreç oluşturmadığını kaydeder. Bu, temiz Windows kurulumu
testinin yerine geçmez; zion-based'deki izole paket testidir.

Teslim arşivi:
`build-check/artifacts/w11widget-native-prototype-00df7ef-x64-portable.zip`
(22.228.442 bayt). SHA-256:
`3757be445f9f61ff812e260995730e24185da15b3c14e4f6250a5f93e0a8e026`.
Arşiv CRC denetimi ve manifestteki **252 SHA-256 girdisi** yerelde doğrulandı;
ZIP yolları standart `/` ayıracı kullanır. Windows'un yerleşik `tar.exe -a -c`
komutu, staging dizinindeki tüm girdilerle kullanıldı. İlk .NET Framework ZIP'inin
ters bölü içeren yolları nedeniyle taşınabilir arşiv ayrıca üretildi.

Geçici test görevleri kaldırıldı; prototip süreçleri kalmadı. Test dosyaları ve
derleme araçları yeniden üretim için ayrı geliştirme dizininde tutuluyor.

### Değerlendirme

Yerel panel + gerektiğinde açılan WinUI ayarlar yönü, sürekli çalışan panelin
bellek maliyeti açısından umut veriyor. Ölçülen yaklaşık %36 farkı tam ürün
kazancı saymadan, 0.7'nin işlevleri taşındıktan sonra aynı senaryolarla yeniden
ölçmek gerekir. Ayarlar yalnız açıkken yaklaşık 87 MiB ek özel bellek kullanıyor;
kapandığında bu süreç tamamen kalkıyor. Qt sürümü işlev ve geri dönüş referansı
olarak korunuyor.

### Prototip tamamlandığındaki kalanlar (tarihsel)

- 0.7 üretim işlevleri: native görev çubuğu yerleşimi, kalıcı ayarlar/Qt göçü,
  panel-ayarlar bildirimi, gerçek widget popup'ları ve erişilebilir panel eylemleri.
- %100/%150/%200 DPI, tam klavye/Narrator, Explorer yeniden oluşumu ve uyku/dönüş.
- Temiz Windows üzerinde runtime doğrulaması; 0.7 yükleyicisi, kurulum ve geri dönüş.
- Uygulama 0.7 olarak henüz yayınlanmadı veya kurulmadı. Windows prototip kaynakları
  ürün eşdeğerliği iddiası taşımaz.
