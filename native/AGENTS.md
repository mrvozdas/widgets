# Native mimari sınırları

- `core/` yalnız C++ standart kütüphanesi ve yerel veri/yerelleştirme başlıklarını
  kullanır. Qt, WinRT, Win32 veya UI başlıkları burada yer almaz. Taşınabilir çekirdek
  testleri bu derleme sınırını korur.
- `platform/` Windows JSON, dosya sistemi ve OS ICU uyarlamalarını içerir. Panel
  Win32/Direct2D/DirectWrite, ayarlar ayrı ve isteğe bağlı WinUI sürecidir.
  Ayarlar, panelin sürekli çalışması için gerekli bir süreç olamaz.
- `WidgetCatalog` yalnız meta veridir. `Planned` girdiler ekleme arayüzünde etkin
  gösterilmez; sağlayıcı veya hazır ayar şeması sayılmaz. Yeni uygulanmış türün
  varsayılanları, doğrulaması, popup'ı ve yaşam döngüsü birlikte tamamlanır.

# Ayarlar ve yaşam döngüsü

- Şema 2, Qt profilini okuyabilmeli; bilinmeyen tür/ayar/ek alanlar korunmalıdır.
  Yükleme dosyayı değiştirmez. Kaydetme, yüklenen `SettingsRevision` ile yapılır;
  çakışma eski dosyanın üzerine yazma izni değildir. Önceki dosya yedeği korunur.
- JSON çağrısını yapan thread, COM/WinRT apartment'ını kendi yaşam süresi boyunca
  açık tutar. `SettingsStore` içinde her çağrıda `RoInitialize/RoUninitialize`
  yapılmaz: son MTA'nın kapanması C++/WinRT factory cache'ini geçersiz kılabilir.
- Saat/geri sayım duvar saatini, odak sayacı monoton süreyi kullanır. Odak yalnız
  çalışırken yenilenir; gizleme/uyku duraklatır, dönüş otomatik başlatmaz.
- `regionConfirmed` tek başına yerleşim izni değildir: monitör/taskbar fiziksel
  sınırları ve DPI, kayıtlı `RegionContext` ile eşleşmeli; güncel yerleşim hesabı
  geçmelidir. Yerleşim düzenlemesi onayı geçersiz kılar.

# Eşleşen sözleşmeler ve doğrulama

- Panel `ProductionPanel.cpp` ile WinUI `settings/App.xaml.cpp` içindeki
  `WM_COPYDATA` widget seçimi eşleşmelidir: `dwData=0x57313157`, NUL sonlandırılmış
  UTF-16, 1–64 ASCII kimlik karakteri. Mesaj yalnız var olan widget'ı seçer;
  dosya kaydetmez ve komut çalıştırmaz.
- `panel/ShellIntegration.cpp` tek örnek protokolü,
  `../src/platform/windows/ApplicationInstance.cpp` ile uyumludur: sınıf
  `w11widget.ApplicationInstance.v2`, `WM_APP+0x251`, kabul `0x573131`, aynı
  oturum/profil SHA-256 anahtarı. Değişiklikte iki uç ve
  `../tests/test_application_instance.cpp` birlikte incelenir.
- `panel/ProductionPanel.cpp` kontrol kimlikleri ile
  `panel/test_settings_lifecycle.ps1` eşleşir: ayarlar 100, taşanlar 101,
  widget başlangıcı 200; popup işlem/sıfırla/düzenle/kapat 302/303/304/305;
  göster/gizle/çıkış 401/402/403. UIA testi yalnız kendi PID/HWND'sini hedefler.
- Standart kapı `../scripts/check.sh`; Windows persistence testleri yalnız yeni
  geçici profilleri kullanır. GUI testleri ayrı komutlardır; açık izinli gerçek
  profil testi haricinde `--settings-file` ile yalıtılmış profil gerekir.
- Ortak çekirdek/platform kaynağı, native CMake ile WinUI `.vcxproj` tarafından
  derlenir. Yeni kaynak eklerken ikisinin kaynak listesi birlikte güncellenir.
  Ortak Windows kaynak klasörüne kopyalama, devam eden iki derlemeyle koordine edilir.

- Windows build kaynağı `%LOCALAPPDATA%` altında olabilir. MSBuild FileTracker
  bu dizindeki başlıkları bağımlılık takibinden dışlar; incremental WinUI derlemesi
  ortak struct değişiminde stale ABI ve heap corruption bırakabilir. Kanonik
  `build-native.ps1` bu nedenle WinUI için `/t:Rebuild` çalıştırır.
