# Konsole na Windows — stan portu

Repozytorium bazuje na KDE Konsole (gałąź master). Upstream zawiera już
interfejs Qt/KDE i wstępną obsługę Windows ConPTY. Zmiany w tym katalogu
naprawiają przekazywanie argumentów, dziedziczenie środowiska, rozmiar
terminala oraz cykl życia sesji. To wersja rozwojowa; nie ma jeszcze
zweryfikowanego instalatora ani potwierdzenia zgodności wszystkich funkcji.

## Budowanie aplikacji

Wymagany jest Windows 10 1903 lub nowszy albo Windows 11, środowisko MSVC,
Qt co najmniej 6.6 oraz KDE Frameworks co najmniej 6.12.
Najprościej przygotować zależności przez KDE Craft:
https://develop.kde.org/docs/getting-started/building/craft/craft-windows/

W PowerShell z aktywnym środowiskiem Craft, w katalogu tego repozytorium:

```powershell
./tools/windows/build.ps1
```

Skrypt kompiluje lokalnie zmienione źródła, uruchamia testy backendu,
a następnie tworzy instalator EXE i archiwum z zależnościami w
`dist/custom-windows`. Budowanie Konsole ma wyłączony cache binarny;
cache jest używany do zależności. `build-info.json` identyfikuje źródłowy
commit, a pliki `.sha256` pozwalają sprawdzić integralność pakietów.

Workflow `.github/workflows/windows-custom-build.yml` wykonuje cały proces
na Windows x64. Gotowe pliki pojawią się jako artefakt udanego uruchomienia
w zakładce Actions forka. Sama obecność workflow nie oznacza, że kompilacja
zakończyła się powodzeniem.

## Skróty i schowek

Zachowano obsługę skrótów i schowka z Konsole:

| Czynność | Domyślny skrót |
| --- | --- |
| Kopiowanie zaznaczenia | Ctrl+Shift+C lub Ctrl+Insert |
| Wklejanie ze schowka | Ctrl+Shift+V lub Shift+Insert |
| Wyszukiwanie w terminalu | Ctrl+Shift+F |

Zaznaczenie wykonuje się myszą. Kopiowanie i wklejanie jest też dostępne
w menu kontekstowym. Ctrl+C zachowuje znaczenie przerwania programu.
Skróty można zmieniać w konfiguracji skrótów Konsole.
Windows nie udostępnia schowka zaznaczenia X11; używaj zwykłego schowka.

## Testy backendu

Testy backendu nie wymagają KDE Frameworks. W środowisku z Qt i MSVC:

```powershell
cmake -S tests/windows -B build-windows-tests -A x64
cmake --build build-windows-tests --config Release
ctest --test-dir build-windows-tests -C Release --output-on-failure
```

Workflow `.github/workflows/windows-backend.yml` wykonuje te testy na Windows.
Obejmują kodowanie argumentów, środowisko, uruchamianie procesu, wejście,
wyjście, kod zakończenia, zmianę rozmiaru i wielokrotne zamykanie.
Nie zastępują kompilacji całej aplikacji i testów interfejsu.

Lokalnie na Linux ARM64 przeszedł test WindowsProcessTest z Qt 6.2.4.
Pełna konfiguracja aplikacji zatrzymała się na braku ECM 6.12/KDE Frameworks;
testy ConPTY wymagają Windows i nie zostały tu wykonane.

## Wymagane sprawdzenie przed wydaniem

- Zbudować całą aplikację na Windows i uruchomić testy backendu.
- Sprawdzić PowerShell, cmd.exe i opcjonalnie wsl.exe jako polecenia profili.
- Skopiować polski tekst, emoji i wiele wierszy do Notatnika oraz wkleić je
  do terminala wszystkimi skrótami i z menu kontekstowego.
- Sprawdzić Ctrl+C przy działającym programie, zmianę skrótów, karty,
  podziały widoku, historię, wyszukiwanie i zachowanie przy dużym wyjściu.
- Sprawdzić zamykanie karty podczas pracy programu i szybkie kończenie sesji.
- Przygotować i sprawdzić pakiet z bibliotekami, wtyczkami i zasobami KDE.

Funkcje zależne od Unix (sygnały POSIX, termios, utmp, /proc), integracja
X11/Wayland oraz usługi D-Bus nie mają pełnej zgodności z Windows. Samo
zbudowanie interfejsu nie oznacza przeniesienia tych funkcji.
