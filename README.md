# ESP32-S3 Offline Chat

Chat room lewat WiFi lokal di ESP32-S3. ESP32 membuat jaringan WiFi sendiri, jadi tidak butuh internet atau router. HP atau laptop yang tersambung membuka halaman chat di browser, lalu bisa saling kirim pesan dan file.

Ini proyek hobi dan masih sederhana.

## Yang bisa dilakukan

- Kirim pesan antar perangkat secara langsung.
- Setiap orang harus mengisi nama sebelum masuk. Nama tidak boleh kembar selama orangnya masih online.
- Kirim file sampai 5 MB. File disimpan di flash ESP32.
- Orang yang baru masuk langsung melihat 20 pesan terakhir.
- Halaman chat terbuka sendiri setelah tersambung ke WiFi (captive portal).

## Cara kerja

ESP32 menjadi access point dengan alamat `192.168.4.1`. Halaman webnya tertanam di kode, jadi tidak ada file lain yang perlu diunggah. Pesan lewat WebSocket, sedangkan unggah dan unduh file lewat HTTP biasa. Nama pengirim dipasang oleh ESP32, bukan oleh browser, jadi tidak bisa dipalsukan lewat pesan.

## Kebutuhan

- Board ESP32-S3. Saya pakai modul N16R8, tapi PSRAM tidak dipakai oleh kodenya.
- Arduino IDE 2.x
- Board package **esp32** dari Espressif
- Library **ESPAsyncWebServer** dan **AsyncTCP** dari organisasi [ESP32Async](https://github.com/ESP32Async)

Versi yang saya pakai saat mengompilasi: esp32 core 3.3.12, ESP Async WebServer 3.12.1, Async TCP 3.5.0. Pakai library dari ESP32Async. Fork lama seperti `lacamera/ESPAsyncWebServer` sudah diarsipkan.

## Pasang dan upload

1. Clone repository ini.
   ```
   git clone https://github.com/xx1ndr/esp32s3-offline-chat-room.git
   ```
2. Pasang board package esp32 lewat Boards Manager.
3. Pasang ESPAsyncWebServer dan AsyncTCP (Library Manager, atau ZIP dari GitHub ESP32Async).
4. Buka `esp32s3_offline_chat/esp32s3_offline_chat.ino`.
5. Atur menu Tools. Contoh untuk N16R8:

   | Menu | Pilihan |
   |---|---|
   | Board | ESP32S3 Dev Module |
   | USB CDC On Boot | Enabled, kalau kabel dicolok ke port berlabel USB |
   | Flash Size | 16MB (128Mb) |
   | PSRAM | OPI PSRAM (varian R8), QSPI PSRAM (R2), atau Disabled |
   | Partition Scheme | 16M Flash (3MB APP/9.9MB FATFS) |

   Partition Scheme harus yang punya FATFS, karena file kiriman disimpan di sana. Kalau flash board kamu 8 MB, pilih skema 8MB yang juga punya FATFS.
6. Klik Upload. Kalau macet di "Connecting...", tahan tombol BOOT sampai upload mulai.
7. Buka Serial Monitor di 115200 baud. Kalau berhasil, muncul `AP IP: 192.168.4.1`.

Ada juga `platformio.ini` kalau kamu lebih suka PlatformIO. Konfigurasi itu belum diuji.

## Cara pakai

1. Sambungkan HP atau laptop ke WiFi `ChatRoom-ESP32`.
2. Halaman chat biasanya terbuka sendiri. Kalau tidak, buka `http://192.168.4.1` (pakai `http`, bukan `https`).
3. Isi nama, lalu masuk.
4. Ketik pesan dan kirim. Tombol klip kertas di sebelah kolom pesan untuk mengirim file.
5. Orang lain mengklik tautan file di chat untuk mengunduhnya.

Jendela kecil "sign in to network" di HP sering tidak bisa memilih atau mengunduh file. Kalau tombol file atau tautan unduh tidak jalan, buka `http://192.168.4.1` di Chrome atau Safari.

## Pengaturan

Ada di bagian atas file `.ino`:

- `AP_SSID`: nama WiFi.
- `AP_PASS`: password WiFi. Kosong berarti terbuka. Kalau diisi, minimal 8 karakter.
- `CLEAR_FILES_ON_BOOT`: kalau `true`, semua file kiriman dihapus setiap ESP32 menyala.
- `HIST`: jumlah pesan terakhir yang dikirim ke orang yang baru masuk.
- `MAX_USERS`: jumlah slot pengguna. WiFi ESP32 sendiri dibatasi 8 perangkat.

Batas ukuran file (5 MB) ada di JavaScript, di konstanta `MAX`.

## Format pesan

Untuk yang mau mengubah kodenya. Semua pesan WebSocket berupa teks dengan pemisah `|`.

```
client -> server   JOIN|nama|token
                   MSG|teks

server -> client   OK|nama
                   ERR|alasan
                   MSG|no|nama|teks
                   FILE|no|nama|tersimpan|asli|ukuran
                   SYS|teks
                   USERS|a,b,c
```

`token` adalah kode acak yang disimpan di browser. Kalau browser yang sama masuk lagi, misalnya setelah refresh, koneksi lamanya digantikan dan tidak ditolak dengan "nama sudah dipakai". `no` adalah nomor urut pesan. Browser membuang pesan yang nomornya sudah tampil, supaya tidak dobel.

Endpoint HTTP: `GET /` untuk halaman chat, `POST /upload?nick=nama` untuk unggah file, `GET /dl?name=file` untuk unduh.

## Struktur repository

```
esp32s3_offline_chat/
  esp32s3_offline_chat.ino   firmware dan halaman web
platformio.ini               opsional
README.md
LICENSE
```

Nama folder sketch harus sama dengan nama file `.ino`, kalau tidak Arduino IDE menolak membukanya.

## Masalah yang sering muncul

- `Could not open COMx` saat upload: nomor port berubah. Tahan BOOT sambil mencolok kabel, lalu pilih ulang port di Tools > Port.
- Serial Monitor kosong: samakan USB CDC On Boot dengan port yang dipakai (port USB = Enabled, port UART/COM = Disabled), lalu tekan RESET.
- Error `AsyncTCP` atau `ESPAsyncWebServer` saat kompilasi: pastikan memakai library dari ESP32Async, dan tidak ada folder library lama atau ganda di `Documents/Arduino/libraries`.
- `FFat gagal dimulai` di Serial Monitor: Partition Scheme belum yang punya FATFS.
- Unggah file ditolak: penyimpanan penuh, file lebih dari 5 MB, atau kamu belum masuk dengan nama.
- Kompilasi pertama lama sekali: wajar, bisa 3 sampai 10 menit. Kalau terasa macet, matikan antivirus real-time sebentar.
- Halaman lama masih muncul setelah firmware diperbarui: muat ulang halaman, atau lupakan jaringan WiFi-nya lalu sambungkan lagi.

## Batasan

Lalu lintas tidak dienkripsi. Jangan kirim data sensitif lewat ini. Tidak ada autentikasi, nama hanya label dan bukan bukti identitas. Kalau mau membatasi siapa yang bisa tersambung, isi `AP_PASS`.

Riwayat chat ada di RAM dan hilang saat ESP32 di-reset. File kiriman tetap ada di flash sampai dihapus. Ruangnya sekitar 9 MB, jadi bisa penuh. Jangkauan WiFi kira-kira 20 sampai 50 meter, tergantung penghalang.

## Bug dan saran

Kalau menemukan bug atau punya saran, kabari lewat Issues di repository ini. Pull request juga boleh. Ini proyek hobi, jadi balasannya mungkin tidak langsung.
