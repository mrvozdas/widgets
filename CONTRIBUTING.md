# w11widget'a katkı

Katkılar GitHub pull request'leri üzerinden kabul edilir. Bir değişiklik üzerinde
çalışmadan önce mevcut issue'ları kontrol edin; davranışı veya kullanıcı arayüzünü
önemli ölçüde değiştiren öneriler için önce bir issue açın.

## Geliştirme ortamı

- Windows 11 x64
- Visual Studio 2022 ve **Desktop development with C++** iş yükü
- CMake 3.20 veya üzeri ve Ninja
- Qt 6.12 MSVC 2022 64-bit; Core, Gui, Qml, Quick, Quick Controls 2 ve Widgets

`x64 Native Tools Command Prompt for VS 2022` açın. Qt farklı bir dizindeyse
önce aşağıdaki değişkeni ayarlayın:

```bat
set W11WIDGET_QT_PREFIX=C:\Qt\6.12.0\msvc2022_64
```

Projeyi yapılandırmak, derlemek ve testleri çalıştırmak için:

```bat
scripts\build-panel.cmd
```

Derlenen uygulamayı geliştirme ortamından başlatmak için:

```bat
scripts\run-panel.cmd --settings
```

Kurulum paketini oluşturup yerel paket testlerini çalıştırmak için:

```bat
scripts\build-installer.cmd
```

## Pull request kontrol listesi

- Değişikliği küçük ve tek amaçlı tutun.
- Yeni davranış için anlamlı test ekleyin veya mevcut testi güncelleyin.
- `scripts\build-panel.cmd` ile tüm testlerin geçtiğini doğrulayın.
- Kullanıcıya görünen davranış değiştiyse README ve ilgili `docs/` belgesini güncelleyin.
- Derleme çıktısı, EXE/DLL, IDE ayarı, günlük, kimlik bilgisi veya yerel ayar dosyası eklemeyin.
- Başka süreçleri veya Windows kabuğunu değiştiren kod eklemeyin. Yerel pencere
  işlemlerini yalnızca uygulamanın kendi pencereleriyle sınırlandırın.

Kaynak yapısı ve yerleşik widget sözleşmesi için
[`docs/widget-api.md`](docs/widget-api.md) belgesine bakın.
