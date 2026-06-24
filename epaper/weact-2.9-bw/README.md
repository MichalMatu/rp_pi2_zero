# WeAct 2.9 BW e-paper

Ten katalog trzyma nasz kod dla czarno-bialego WeAct 2.9. Nie vendorujemy calego
repo WeActStudio, bo upstream nie ma widocznej licencji. Skrypt budowania uzywa
lokalnego klonu upstreamu na Raspberry Pi.

## Pinout

Aktualne podlaczenie:

| EPD | Raspberry Pi |
| --- | --- |
| `1 BUSY` | `GPIO24`, physical pin `18` |
| `2 RES` | `GPIO17`, physical pin `11` |
| `3 CD` / `DC` | `GPIO25`, physical pin `22` |
| `4 CS` | `GPIO8` / `CE0`, physical pin `24` |
| `5 SCL` | `GPIO11` / `SCLK`, physical pin `23` |
| `6 SDA` | `GPIO10` / `MOSI`, physical pin `19` |
| `7 GND` | `GND` |
| `8 VCC` | `3V3` |

Programy w tym katalogu renderuja obraz z `EPD_ROTATE_0`, czyli odwrotnie o
180 stopni wzgledem pierwszej wersji testowej.

## Tryby odswiezania

Kod WeAct ma trzy uzyteczne sciezki:

- `epd_init()` + `epd_displayBW()` - pelny refresh. Najczystszy obraz, ale
  najwolniejszy. Starszy datasheet dla panelu 296x128 podaje typowo `3 s` przy
  `25 C`, czyli okolo `0.33 FPS`.
- `epd_init_fast()` + `epd_displayBW_fast()` - szybki pelnoekranowy refresh.
  Mniej migania i zwykle szybciej, ale trzeba zmierzyc na konkretnym panelu.
- `epd_init_partial()` + `epd_displayBW_partial()` - partial waveform. Dobre do
  licznika, statusu, zegara i malych zmian tekstu. W aktualnej bibliotece WeAct
  funkcja nadal wysyla caly framebuffer, ale waveform jest partial, wiec zysk
  jest w czasie pracy matrycy, nie w SPI.

Sterownik SSD1680 ma wewnetrzny frame rate `25-200 Hz`, ale to nie jest FPS
gotowego obrazu. Jeden refresh e-paper sklada sie z wielu faz waveform. Realny
limit trzeba mierzyc po czasie `BUSY`.

Praktycznie:

- pelny refresh BW: okolo `0.3 FPS`;
- panel czerwono-czarno-bialy: zwykle rzad wielkosci `0.05 FPS`;
- partial BW: sensowny cel to kilka aktualizacji na sekunde, z okresowym pelnym
  refresh co kilkadziesiat zmian albo gdy widac ghosting.

## Build na Raspberry Pi

Z repo uruchom na Pi:

```bash
./scripts/build-epaper-weact-2.9-bw.sh
```

Powstanie:

```bash
~/bin/epd_290_bw_fast_partial
~/bin/epd_290_bw_status
```

Benchmark pokazuje czasy kazdej operacji:

```bash
~/bin/epd_290_bw_fast_partial 10
```

Argument `10` oznacza liczbe prob partial refresh. Po tescie program robi pelny
cleanup refresh, zeby ograniczyc ghosting.

## Ekran statusu

Status connection screen:

```bash
~/bin/epd_290_bw_status --auto status
```

Strony:

```bash
~/bin/epd_290_bw_status --auto status
~/bin/epd_290_bw_status --fast status
~/bin/epd_290_bw_status --fast net
~/bin/epd_290_bw_status --fast time
~/bin/epd_290_bw_status --fast help
```

Na ekranie sa: godzina, data, uptime, user, komenda SSH, IP `usb0` i IP `wlan0`.
Domyslnie program uzywa pelnego odswiezania, bo to
najczytelniejsze po starcie. Do przelaczania stron i czestszych aktualizacji
uzywaj `--fast`.

Glowny ekran statusu ma 9 rownych wierszy. Gora pokazuje polaczenie, dol pokazuje
system: RSSI, temperature CPU, load average, wolna pamiec, wolne miejsce na
karcie i status uslug.

Program zapisuje hash ostatniego framebufferu w:

```text
~/.cache/rp_pi2_zero/epd_status.state
```

Jesli kolejne uruchomienie daje identyczny obraz, refresh jest pomijany i ekran
nie miga. Tryb `--auto` robi pelny refresh przy pierwszym uruchomieniu oraz po
kazdych 5 szybkich zmianach, a pozostale zmiany wysyla przez `fast refresh`.

Wartosci systemowe sa zaokraglane przed renderem, zeby drobne zmiany nie
powodowaly odswiezania:

- czas: co 5 minut;
- uptime: co 10 minut;
- RSSI: kategorie `good` / `ok` / `weak`;
- temperatura CPU: prog 5 C;
- load average: prog 0.2;
- RAM: prog 50 MB;
- dysk: prog 250 MB.

Opcje:

```bash
~/bin/epd_290_bw_status --auto --full-every 5 status
~/bin/epd_290_bw_status --auto --full-every 0 status
~/bin/epd_290_bw_status --force --full status
EPD_STATUS_STATE=/tmp/epd.state ~/bin/epd_290_bw_status --auto status
```

Build domyslnie ustawia SPI na `4 MHz`. Mozesz to zmienic:

```bash
EPD_SPI_SPEED_HZ=8000000 ./scripts/build-epaper-weact-2.9-bw.sh
```

Auto-update po boot i potem co 5 minut:

```bash
sudo cp systemd/epd-status.service /etc/systemd/system/
sudo cp systemd/epd-status.timer /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl disable epd-status.service
sudo systemctl enable --now epd-status.timer
```

Timer uruchamia `epd_290_bw_status --auto status` na starcie i potem na
5-minutowych tickach zegara. Program pomija refresh, jesli obraz po progach
delty jest identyczny.
