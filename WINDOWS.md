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
a następnie tworzy portable ZIP z konsole.exe i zależnościami w
`dist/custom-windows`. Rozpakuj cały ZIP i uruchom `bin/konsole.exe`.
Nie przenoś samego EXE bez bibliotek i zasobów. Opcjonalny instalator
buduje parametr `-WithInstaller`. Wersja portable nie wymaga instalacji;
ustawienia Qt/KDE mogą nadal być zapisywane w profilu użytkownika Windows.
Budowanie Konsole ma wyłączony cache binarny;
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
| Uzupełnianie poleceń i ścieżek w PowerShell | Tab (styl Bash: Complete) |
| Przeszukiwanie historii poleceń wstecz | Ctrl+R |

Portable zawiera PowerShell **7.6.10** z własnym środowiskiem .NET i modułem
**PSReadLine 2.4.5** w katalogu `powershell`. Nie trzeba instalować PowerShella,
.NET ani PSReadLine. Domyślny profil wybiera dołączoną powłokę przed wersjami
systemowymi. Rozpakuj cały ZIP, zachowując obok siebie katalogi `bin` i
`powershell`, i uruchom `bin/konsole.exe`. Folder można przenieść.

Przy starcie profil ładuje PSReadLine z katalogu tej powłoki, konfiguruje
Tab i Ctrl+R oraz zapis historii między sesjami. Nie zmienia systemowego
profilu PowerShella. Ustawienia i historia mogą być zapisywane w profilu
użytkownika Windows. Własne, wcześniej zapisane profile Konsole zachowują
swoje polecenia; wybierz wbudowany profil, aby korzystać z dołączonej powłoki.
Gdy brakuje kompletnego katalogu `powershell`, profil szuka zainstalowanego
PowerShella 7, a następnie Windows PowerShell 5.1.
W WSL i SSH obsługę tych klawiszy zapewnia zdalna powłoka;
`cmd.exe` nie otrzymuje funkcji PSReadLine.

`tools/windows/bundle-powershell.py` pobiera oficjalny ZIP Microsoftu,
weryfikuje przypiętą sumę SHA256 i zachowuje licencje oraz informacje o
komponentach zewnętrznych. `bundled-runtime.json` opisuje dołączone wersje.
Budowanie kończy się testem PowerShella wyodrębnionego z gotowego ZIP:
sprawdza wersje, źródło PSReadLine i rzeczywiste działanie Tab oraz Ctrl+R
przez ConPTY, również ze ścieżki zawierającej spacje.

Zaznaczenie wykonuje się myszą. Kopiowanie i wklejanie jest też dostępne
w menu kontekstowym. Ctrl+C zachowuje znaczenie przerwania programu.
Skróty można zmieniać w konfiguracji skrótów Konsole.
Windows nie udostępnia natywnego schowka zaznaczenia X11. Port emuluje go
wewnątrz procesu Konsole: zaznaczenie tekstu myszą aktualizuje drugi schowek,
a kliknięcie środkowym przyciskiem wkleja jego tekst. Dostępny jest też
skrót **Ctrl+Shift+Insert** (Wklej zaznaczenie). Działa między kartami i
podziałami tego samego procesu; nie jest to globalny schowek innych aplikacji
ani osobnych procesów Konsole i nie jest zapisywany po zamknięciu programu.
Zwykły schowek Windows i Ctrl+Shift+C/V są niezależne. Opcja profilu
„automatycznie kopiuj zaznaczenie” może dodatkowo kopiować do zwykłego schowka.
W aplikacjach przechwytujących mysz użyj **Shift+środkowy przycisk**.
W ustawieniach myszy profilu środkowy przycisk musi mieć wybrane wklejanie
zaznaczenia (ustawienie domyślne), a nie zwykłego schowka.

## Interaktywny podgląd plików tekstowych

Zatrzymaj kursor nad rozpoznaną ścieżką do lokalnego pliku przez **2 sekundy**.
Pojawi się okno bez ramki z całym tekstem i paskiem przewijania. Przenieś
kursor do podglądu: możesz przewijać, zaznaczać i kopiować tekst przez
Ctrl+C, Ctrl+Insert, Ctrl+Shift+C lub menu kontekstowe. Wyjazd kursorem poza
podgląd zamyka okno; Esc także je zamyka. Przejście ze ścieżki do podglądu
ma domyślnie 250 ms tolerancji, aby okno nie zniknęło po drodze.

**Ustawienia → Konfiguracja Konsole → Thumbnails / File previews**:

- włączenie / wyłączenie interaktywnego podglądu;
- osobne włączenie / wyłączenie informacji `file`, gdy tekstu nie da się pokazać;
- opóźnienie nieruchomego kursora: 100–30000 ms, domyślnie 2000 ms;
- czas na przejście do podglądu i jego zamknięcie: 100–2000 ms;
- szerokość i wysokość okna, domyślnie 640×420 px;
- limit rozmiaru pliku: 1–256 MiB, domyślnie 16 MiB.

Podgląd nie ucina tekstu do pierwszych wierszy. Plik przekraczający limit
wyświetla informację o konieczności zwiększenia limitu. Rozpoznawana jest
zawartość, nie tylko rozszerzenie: np. TXT, INI, CONF, LOG, JSON, kod źródłowy
i pliki bez rozszerzenia. Obsługiwane są UTF-8, Unicode z BOM (w tym UTF-16)
i kodowanie systemowe Windows. Gdy tekstu nie można pokazać (np. plik binarny, nieobsługiwane kodowanie,
przekroczony limit), domyślnie pojawia się informacja o pliku: wynik
**`file --brief`**, nazwa, rozmiar i MIME. Można ją wyłączyć osobnym
przełącznikiem **Show file identification when text preview is unavailable**.
Portable zawiera natywne **file 5.48**, bazę `magic.mgc` i wszystkie wymagane
biblioteki w katalogu `file`; instalacja MSYS2 ani programu file nie jest
potrzebna. Oryginalne licencje i kompletne archiwa źródeł komponentów są w
`file/licenses` i `file/sources`, a pochodzenie i sumy opisuje
`file-runtime.json`. Identyfikacja działa w tle, ma limit czasu 5 sekund
i nie uruchamia badanego pliku. Czytanie pliku
odbywa się w tle, a podgląd jest tylko do odczytu.

W profilu włącz **Mysz → Różne → Podkreślaj pliki**. Wbudowany profil Windows
ma tę opcję domyślnie włączoną; wcześniejsze własne profile zachowują swoje
ustawienia. Nazwy ze spacjami najlepiej wypisywać w cudzysłowie. Obsługiwane
są lokalne ścieżki Windows i ścieżki względne; wbudowany PowerShell zgłasza
bieżący katalog po zmianie przez `cd`. Podgląd nie pobiera plików przez SSH,
HTTP ani UNC. Nie uruchamia treści pliku i nie renderuje HTML.

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

Testy ConPTY i pełna kompilacja wymagają Windows; workflow wykonuje je
przed opublikowaniem artefaktu. Interfejs i schowek wymagają osobnej
weryfikacji na komputerze użytkownika.

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
