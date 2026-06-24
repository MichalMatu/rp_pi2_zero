# Raspberry Pi Zero 2 W setup

Repo opisuje aktualną konfigurację Raspberry Pi Zero 2 W używanego jako lekki domowy serwer, host po USB i baza pod Pi-hole, OctoPrint, e-paper oraz małe aplikacje LAN.

## Aktualny stan

- Hostname: `zero2`
- Użytkownik: `michal`
- SSH: włączone
- USB gadget: włączony
- USB IP Raspberry Pi: `169.254.64.64`
- USB IP komputera: `169.254.64.1`
- Wi-Fi IP ostatnio widziane: `192.168.0.23`
- Interfejsy sprzętowe: `SPI` i `I2C` włączone pod e-paper/touch

## Dane wrażliwe

Nie commituj haseł, SSID prywatnej sieci, prywatnych kluczy SSH ani tokenów. Lokalne wartości trzymaj w:

```text
.secrets/zero2.env
```

Ten katalog jest ignorowany przez Git. Do repo jest dodany tylko szablon:

```bash
cp .env.example .secrets/zero2.env
chmod 600 .secrets/zero2.env
```

## Połączenie przez USB

Podłącz Raspberry Pi Zero 2 W do komputera portem `USB`, nie `PWR IN`.

Na macOS powinien pojawić się port:

```text
Raspberry Pi USB Gadget
```

Po pierwszej konfiguracji można wejść:

```bash
ssh michal@169.254.64.64
```

Albo przez mDNS:

```bash
ssh michal@zero2.local
```

## Połączenie przez Wi-Fi

Po poprawnym starcie Raspberry Pi powinno ogłosić się jako:

```bash
ssh michal@zero2.local
```

Jeśli `zero2.local` nie działa, sprawdź IP w routerze albo skanem:

```bash
nmap -sn 192.168.0.0/24
```

Potem:

```bash
ssh michal@ADRES_IP
```

Warto w routerze ustawić rezerwację DHCP dla `zero2`, żeby IP się nie zmieniało.

## Kontrola po zalogowaniu

```bash
hostname
hostname -I
systemctl is-active ssh
ip -4 addr show usb0
ip -4 addr show wlan0
```

## E-paper

SPI i I2C są włączone w `/boot/firmware/config.txt`. Po zalogowaniu można sprawdzić:

```bash
ls /dev/spidev*
ls /dev/i2c-*
groups
```

Użytkownik `michal` jest w grupach `gpio`, `i2c` i `spi`.

### WeAct 2.9 e-paper

Na `zero2` zainstalowano `wiringPi`, skopiowano przykład WeActStudio i zbudowano aktualne programy:

```bash
~/bin/epd_290_bw_fast_partial
~/bin/epd_290_bw_status
```

Podłączenie WeAct 2.9 do Raspberry Pi Zero 2 W dla płytki z nadrukiem:

| WeAct | Raspberry Pi GPIO |
| --- | --- |
| `1 BUSY` | `GPIO24`, physical pin `18` |
| `2 RES` | `GPIO17`, physical pin `11` |
| `3 CD` / `DC` / `D-C` | `GPIO25`, physical pin `22` |
| `4 CS` | `GPIO8` / `CE0`, physical pin `24` |
| `5 SCL` / `CLK` / `SCK` | `GPIO11` / `SCLK`, physical pin `23` |
| `6 SDA` / `DIN` / `MOSI` | `GPIO10` / `MOSI`, physical pin `19` |
| `7 GND` | dowolny `GND`, np. physical pin `6` |
| `8 VCC` | `3V3`, physical pin `1` lub `17` |

Użyj `3V3`, nie `5V`, chyba że dokładny moduł ma wyraźnie opisane wejście jako 5V-safe.

Finalne podłączenie używa standardowego pinu WeAct: `BUSY` na `GPIO24`, physical pin `18`.

Kod i notatki do szybszego odświeżania są w:

```text
epaper/weact-2.9-bw/
```

Pełny refresh czarno-białego panelu 296x128 jest z natury wolny: datasheet dla
tego formatu podaje typowo około `3 s` przy `25 C`. Szybsze aktualizacje trzeba
robić trybem `fast` albo `partial`, licząc się z większym ghostingiem i
okresowym pełnym cleanup refreshem.

Zbudowanie benchmarku fast/partial na Raspberry Pi:

```bash
./scripts/build-epaper-weact-2.9-bw.sh
~/bin/epd_290_bw_fast_partial 10
```

Ekran statusu z danymi do polaczenia:

```bash
~/bin/epd_290_bw_status --auto status
~/bin/epd_290_bw_status --fast net
~/bin/epd_290_bw_status --fast time
```

`--auto` zapisuje stan w `~/.cache/rp_pi2_zero/epd_status.state`. Jesli obraz
sie nie zmienil, program pomija refresh, wiec ekran nie miga bez potrzeby.
Po kazdych 5 szybkich zmianach robi pelny cleanup refresh; mozna to zmienic
opcja `--full-every`.

Auto-update po boot i potem co 5 minut:

```bash
sudo cp systemd/epd-status.service /etc/systemd/system/
sudo cp systemd/epd-status.timer /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl disable epd-status.service
sudo systemctl enable --now epd-status.timer
```

Timer odpala program na starcie i potem na 5-minutowych tickach zegara. Ekran
zmienia sie tylko wtedy, gdy hash wyrenderowanego obrazu jest inny.

Uruchomienie benchmarku czarno-bialego:

```bash
ssh michal@zero2.local
~/bin/epd_290_bw_fast_partial 10
```

## Gitleaks

Repo ma konfigurację Gitleaks w `.gitleaks.toml` i workflow GitHub Actions w `.github/workflows/gitleaks.yml`.

Lokalnie:

```bash
brew install gitleaks
gitleaks detect --source . --config .gitleaks.toml --redact
```

Hook pre-commit:

```bash
./scripts/install-git-hooks.sh
```

Od tej chwili `git commit` uruchomi:

```bash
gitleaks protect --staged --redact --config .gitleaks.toml
```

## Diagnostyka kodu

Najwazniejszy lokalny check:

```bash
./scripts/check.sh
```

Sprawdza:

- whitespace w Git diff;
- skladnie skryptow shellowych;
- `shellcheck` i `shfmt`;
- Markdown przez `markdownlint-cli2`;
- C przez `gcc -Wall -Wextra -fsyntax-only` na stubach drivera e-paper;
- C przez `cppcheck` dla ostrzezen, wydajnosci i przenosnosci;
- unity systemd, jesli `systemd-analyze` i binarka statusu sa dostepne;
- sekrety przez `gitleaks`, jesli jest zainstalowany.

Na macOS narzedzia mozna doinstalowac:

```bash
brew install shellcheck shfmt cppcheck markdownlint-cli2 gitleaks
```

## Pliki boot użyte podczas konfiguracji

Na karcie microSD zostały użyte:

- `cmdline.txt` z `modules-load=dwc2,g_ether`
- `config.txt` z `dtoverlay=dwc2`, `dtparam=spi=on`, `dtparam=i2c_arm=on`
- jednorazowy `firstrun.sh`
- `userconf.txt`
- `wpa_supplicant.conf`

Po wykonaniu `firstrun.sh` skrypt usuwa siebie i jednorazowe pliki z partycji boot.
