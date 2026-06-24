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
- Domyślna nazwa assetu w GitHub Release to `merged-firmware.bin`.

Przykład dla GitHub Releases:

```text
https://github.com/twoj-user/twoj-repo
```

W release dodaj asset `.bin`, najlepiej nazwany jednoznacznie, np. `gt7_v2_project.bin`.

## GitHub Actions

Repo ma workflow, który przy tagu `v*` buduje firmware i publikuje GitHub Release.

Przykład:

```text
git tag v0.2.5
git push origin v0.2.5
```

Workflow opublikuje release z plikiem `merged-firmware.bin`.

Przykład prostego manifestu:

```text
version=0.2.5
bin_url=https://twoj-serwer.pl/gt7_v2_project/firmware.bin
```

## Ostatnie zmiany

W bieżących plikach rozbudowano live delta timing, poprawiono prezentację delty w UI,
dodano dokładniejsze liczenie paliwa na okrążenie oraz zaktualizowano konfigurację
PlatformIO dla wyświetlacza LilyGO AMOLED.
