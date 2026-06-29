# GT7 V2 Project

Mini display telemetryczny dla Gran Turismo 7 na ESP32-S3 LilyGO AMOLED.

## Aktualna wersja

Wydzielone moduły projektu:

- paliwo: dokładniejsze liczenie średniego zużycia i okrążeń na paliwie,
- WiFi: konfiguracja i wykrywanie PlayStation przeniesione do osobnego modułu,
- delta: osobny moduł live delta timing,
- UI: poprawione płynne kolory delty oraz uporządkowane renderowanie ekranu.

## Aktualizacje OTA

Projekt ma teraz prosty mechanizm automatycznych aktualizacji firmware po Wi-Fi.

- W portalu Wi-Fi można zapisać adres manifestu aktualizacji i włączyć auto-check.
- Domyślnie portal już ma wpisane Twoje repozytorium GitHub, a urządzenie samo przejdzie do `releases/latest`.
- Manifest może być JSON-em albo prostym tekstem z polami `version=` i `bin_url=`.
- Gdy urządzenie wykryje nowszą wersję, pobierze binarkę i wykona restart po udanym OTA.
- Domyślna nazwa assetu OTA w GitHub Release to `firmware.bin`.
- `merged-firmware.bin` zostaje tylko jako plik do pełnego flashowania.

Przykład dla GitHub Releases:

```text
https://github.com/twoj-user/twoj-repo
```

W release dodaj asset `.bin`, najlepiej nazwany jednoznacznie, np. `gt7_v2_project.bin`.

## GitHub Actions

Repo ma workflow, który przy tagu `v*` buduje firmware i publikuje GitHub Release.

Przykład:

```text
git tag v0.2.9
git push origin v0.2.9
```

Workflow opublikuje release z tym samym firmware pod dwiema nazwami:
`firmware.bin` oraz `merged-firmware.bin`.

Przykład prostego manifestu:

```text
version=0.2.9
bin_url=https://twoj-serwer.pl/gt7_v2_project/firmware.bin
```

## Ostatnie zmiany

W bieżących plikach rozbudowano live delta timing, poprawiono prezentację delty w UI,
dodano dokładniejsze liczenie paliwa na okrążenie oraz zaktualizowano konfigurację
PlatformIO dla wyświetlacza LilyGO AMOLED.

## Pozycja XYZ na PC

ESP32 wysyła pozycję auta `x/y/z` jako JSON przez UDP do hosta
`gt7positions.duckdns.org` na port `5005`.

Przykładowy pakiet:

```text
{"x":12.345,"y":0.000,"z":67.890,"mac":"AA:BB:CC:DD:EE:FF"}
```

Na PC uruchom lokalny serwer, który nasłuchuje na UDP `5005` i wyświetla dane w przeglądarce:

```text
python pc_position_server.py
```

Następnie otwórz w przeglądarce:

```text
http://localhost:8000
```

Serwer PC odbiera dane z ESP i pokazuje je na stronie jako trzy wartości `X`, `Y`, `Z`.
Na stronie widoczny jest też `MAC` urządzenia wysyłającego dane.
