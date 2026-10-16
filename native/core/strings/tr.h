#pragma once
namespace w11widget::native::configText {
inline constexpr auto countdown = L"Geri sayım";
inline constexpr auto focus = L"Odak";
inline constexpr auto worldClock = L"Dünya saati";
inline constexpr auto focusTimer = L"Odak sayacı";
inline constexpr auto calendar = L"Takvim";
inline constexpr auto systemStatus = L"Sistem durumu";
inline constexpr auto power = L"Pil / güç";
inline constexpr auto network = L"Ağ";
inline constexpr auto weather = L"Hava durumu";
inline constexpr auto accountCalendar = L"Hesaplı takvim";
inline constexpr auto invalidField = L"Geçersiz veya eksik alan: ";
inline constexpr auto invalidWidget = L"Widget kimliği geçersiz, yinelenmiş veya genişliği sınır dışında.";
inline constexpr auto widgetLimit = L"En fazla 16 widget kullanılabilir.";
inline constexpr auto invalidSettings = L"Widget ayarları geçersiz veya toplam 64 KiB sınırını aşıyor.";
inline constexpr auto invalidZone = L"Geçersiz IANA saat dilimi: ";
inline constexpr auto groupConflict = L"time_zone ve eski time_zones birlikte kullanılamaz.";
inline constexpr auto invalidJson = L"Ayar dosyası geçerli bir JSON nesnesi değil.";
inline constexpr auto fileLimit = L"Ayar dosyası 256 KiB sınırını aşıyor.";
inline constexpr auto readError = L"Ayar dosyası okunamadı.";
inline constexpr auto saveError = L"Ayar dosyası güvenli biçimde kaydedilemedi.";
inline constexpr auto backupRecovery = L"Kayıt tamamlanamadı. Önceki ayarlar bu yedekte korundu: ";
inline constexpr auto conflict = L"Ayar dosyası başka bir işlem tarafından değiştirildi. Yeniden yükleyip tekrar deneyin.";
inline constexpr auto locked = L"Başka bir ayar kaydı sürüyor. Tekrar deneyin.";
inline constexpr auto invalidPath = L"Ayar dosyası yolu mutlak ve geçerli olmalı.";
inline constexpr auto timeError = L"Saat dilimi veya hedef tarih hesaplanamadı.";
}
