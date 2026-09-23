# SPI NOR Flash Test Cihazi

SOIC-8 SPI flash cipinin **saglam olup olmadigini** ve **kac MB** oldugunu,
module lehimlemeden once kesin olarak ogrenmek icin.

**Kart:** ESP32-WROOM-32UE (COM8, MAC `40:22:d8:03:c6:ec`)
ESP32 3.3V logic kullanir, flash'a **dogrudan** baglanir. Arduino Uno/Nano
ile yapilsaydi 5V -> 3.3V gerilim bolucu gerekirdi.

## Baglanti

Flash cipi bir **SOP8 -> DIP8 donusturucu karta** lehimlenip breadboard'a
takilir (envanterde 2 adet var).

| Flash pin | Isim | ESP32 |
|---|---|---|
| 1 | CS# | GPIO5 |
| 2 | DO (MISO) | GPIO19 |
| 3 | WP# | **3V3** |
| 4 | GND | GND |
| 5 | DI (MOSI) | GPIO23 |
| 6 | CLK | GPIO18 |
| 7 | HOLD# | **3V3** |
| 8 | VCC | 3V3 |

Pin 3 ve 7 bosta birakilirsa cip cevap vermez - ikisi de 3.3V'a cekilmeli.
Pin 1 yonu (cipin uzerindeki nokta/centik) DIP adaptorun 1 numarasiyla ayni
tarafa gelmeli.

## Kullanim

```
pio run -e flash_tester -t upload
pio device monitor -e flash_tester
```

Test her 2 saniyede tekrarlanir; cipi takip cikarirken canli izleyebilirsin.

## Ciktinin okunmasi

```
JEDEC ID : EF 40 18
Uretici  : Winbond
Kapasite : 16 MB (128 Mbit)
Status   : 0x00
Ilk 16 B : FF FF FF FF ...
SONUC    : SAGLAM
```

**Kapasite bayti** (3. bayt): `16`=4MB, `17`=8MB, `18`=16MB, `19`=32MB.

`SONUC: CEVAP YOK` cikarsa (`FF FF FF` veya `00 00 00`): cip olu, takili
degil, ters takilmis, ya da WP#/HOLD# 3.3V'a baglanmamis.

## Neden gerekli

ESP32-S3 kurtarma calismasinda (bkz proje hafizasi) elde birden fazla
sokulmus flash cipi birikti ve hangisinin saglam oldugu karisti. Multimetre
ile sadece kisa devre anlasilir; cipin gercekten **cevap verdigini** yalnizca
JEDEC sorgusu gosterir.
