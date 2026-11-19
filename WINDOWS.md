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
| Uzupełnianie poleceń i ścieżek / przyjęcie szarej sugestii | Tab (sugestia, jeśli jest widoczna; inaczej Complete w stylu Bash) |
| Lista wyboru uzupełnień | Ctrl+Space |
| Poprzednie dopasowanie | Shift+Tab |
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

## Wyniki ls/dir, metadane i menu linków

W dołączonym PowerShellu 7 domyślne tabele `Get-ChildItem`, `ls` i `dir`
mają nazwy plików i folderów jako linki OSC 8 do pełnej lokalnej ścieżki.
Nazwy ze spacjami i polskimi znakami działają bez ręcznego dopisywania
cudzysłowów. Link zachowuje katalog z chwili wyświetlenia listy, także po
późniejszym `cd`. Polecenia nadal zwracają obiekty FileInfo/DirectoryInfo:
potoki, filtrowanie, Select-Object i eksport danych pozostają dostępne.
Zmienia się tylko domyślny widok tabeli plików. Jawne własne formatowanie
może wyświetlać zwykły tekst zamiast linków. Integracja dotyczy PowerShella
7.2+; listing z cmd.exe lub Windows PowerShell 5.1 korzysta ze zwykłego
rozpoznawania widocznych ścieżek.

Podgląd pokazuje metadane także wtedy, gdy można wyświetlić tekst:
pełną ścieżkę, MIME, rozmiar w bajtach i czytelnych jednostkach, datę
utworzenia i modyfikacji, właściciela, grupę oraz prawa. Na Windows są to
rzeczywisty właściciel, grupa podstawowa i wpisy ACL systemu Windows,
łącznie z pełnym DACL w zapisie SDDL. Nie są przedstawiane jako uniksowe
bity rwx. Niedostępne dane są oznaczane jako niedostępne. Rozmiar folderu
jest wartością zgłaszaną przez system, bez sumowania jego zawartości.

Prawy przycisk nad nazwą pliku/folderu, ścieżką lub lokalnym linkiem
`file://` udostępnia **Copy Full File Path**. Kopiowana jest pełna ścieżka
Windows, bez sufiksu numeru wiersza/kolumny. Menu pliku ma też
**Copy Entire File Contents / Kopiuj całą zawartość**: odczytuje plik w tle
i kopiuje sam pełny tekst, bez nagłówka metadanych i bez konieczności
otwierania podglądu. Obsługuje te same kodowania co podgląd. Pliki binarne,
błędy odczytu i przekroczenie skonfigurowanego limitu rozmiaru wywołują
komunikat, bez kopiowania fragmentu i bez nadpisywania schowka. Nad adresem HTTP/HTTPS lub
`www...` dostępne są **Open in Browser** i kopiowanie adresu. Na Windows
przeglądarka jest uruchamiana przez natywny mechanizm domyślnej aplikacji.

Wbudowany profil Windows ma włączone podkreślanie plików i linki OSC 8.
We wcześniejszych własnych profilach włącz także **Allow escaped links**
w ustawieniach myszy, z dozwolonymi schematami `file://`, `http://`, `https://`.

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

## Remote tabs through a shared folder (Windows)

**File → New Remote Tab** opens a PowerShell terminal on another Windows host
using only files. **Settings → Configure Remote Tabs…** saves the client-side
exchange path, host ID, polling interval and connection/heartbeat timeout. The
connection dialog also allows changing these values for the next tab. The pairing
key is remembered in memory until Konsole exits, not in ordinary settings.

No SSH server, sockets, TCP listener, Internet access or network discovery is
used. Each side needs an already accessible view of the same filesystem folder,
with read, write **and delete** permission. The path can differ on the two machines,
for example `X:\TerminalExchange` and `D:\Shared\TerminalExchange`. A shared local
volume, mounted disk or existing share can supply that folder. Konsole does not
create a network connection or provide the underlying storage. This is not a
OneDrive-style asynchronous sync protocol: atomic rename and coherent visibility
of files are required. Do not use independent copies or conflict-producing sync.

### Start the remote side

Copy and extract the **whole portable package** on the remote Windows x64 host.
Run the following in PowerShell as the account whose shell you want to use:

```powershell
& 'C:\Tools\Konsole\bin\Start-RemoteHost.ps1' -ExchangePath 'D:\Shared\TerminalExchange' -HostId 'host1'
```

The script starts the foreground `konsole-file-host.exe`; keep its window open.
It creates a local pairing key under `%LOCALAPPDATA%\KonsoleFileRemote\pairing.key`
on first use and displays the key. Enter that **64-character key** on the client,
select the client's path to the same exchange folder, use the same host ID and
click **Connect**. Each new remote tab creates its own shell, running as the user
who started the host. Ctrl+C in the **host's launcher window** stops the listener
and its active shells. Ctrl+C in the **remote tab** interrupts the remote command.
No service, scheduled task, administrator privilege or automatic startup is added.

For environments restricting script execution, the EXE can be run directly,
without changing the machine's PowerShell policy:

```powershell
# Create a key in a PRIVATE LOCAL folder, once:
& '.\bin\konsole-file-host.exe' --generate-key 'C:\Users\me\pairing.key'
# Start the host (the parent exchange folder must already exist):
& '.\bin\konsole-file-host.exe' --root 'D:\Shared\TerminalExchange' --host host1 --key-file 'C:\Users\me\pairing.key'
```

Keep `bin`, `powershell`, the Qt DLLs and the launcher together. A bare EXE copied
without its libraries is not sufficient. PowerShell 7 and PSReadLine come from the
portable package. The remote machine must support ConPTY (Windows 10 1903+ / 11).

| Option | Client | Host launcher |
| --- | --- | --- |
| Local view of exchange folder | Exchange folder | `-ExchangePath` (required) |
| Target host ID | Host ID | `-HostId`, default `host1` |
| Authentication | Pairing key | `-KeyFile`, private local key file |
| Poll interval | 20–5000 ms, default 50 ms | `-PollMilliseconds`, same range/default |
| Heartbeat timeout | 5–600 s, default 30 s | `-TimeoutSeconds`, same range/default |
| Maximum active shells | — | `-MaximumSessions`, 1–16, default 4 |
| Initial remote directory | — | `-WorkingDirectory`, default remote home |
| PowerShell executable | — | `-PowerShellPath`, defaults to bundled PowerShell |

The client does not choose an arbitrary executable or forward its local environment;
the host determines shell, account, environment and initial working directory.
Different hosts can use different IDs and pairing keys in the same root folder.
Use the same polling/timeout values on both sides for predictable behavior.

### Terminal behavior and limits

The host uses a real ConPTY console, not one-shot command execution or text-log
scraping. The transport preserves input bytes, ANSI/VT output, UTF-8 text,
terminal-size changes and shell exit status. **Ctrl+C**, Tab, Ctrl+R, arrows,
interactive command-line programs and child processes use the remote console.
Konsole's local Ctrl+Shift+C/V, selection clipboard, tabs and scrollback work on
the displayed remote terminal. GUI programs execute on the remote desktop; this
is not desktop streaming. SSH/WSL or terminal applications launched inside the
remote shell may have their own key bindings, as in a local shell.

The speed depends on storage latency plus polling intervals; high-latency disks
cannot behave as quickly as a local terminal. Loss of heartbeats closes the tab's
transport and causes the host to stop the abandoned shell after its configured
timeout. Closing the tab requests closure of its remote console. There is no
session reattachment/replay after disconnect, and session state is not preserved
for later reconnection. Maximum packet size is 128 KiB, the queue is bounded to
1024 files per session, and pending host output is bounded to 16 MiB; a stalled
reader or flooding command produces a controlled error rather than unlimited
queued output. Filesystem calls can themselves wait for the OS/storage timeout.

Remote file paths, OSC 8 links in `ls`/`dir` and ordinary `file://` links use the
host file service. Hover for the configured delay to read text and metadata from
the **remote host**, never from a similarly named file on the client. The popup
supports scrolling, selection and copying. Right-click offers **Copy Full File
Path** and **Copy Entire File Contents**. The latter copies the complete decoded
text without metadata headers. Binary files cannot be copied as text; errors and
size-limit violations preserve the previous clipboard. There is no silent truncation.

Metadata includes size, creation/modification times, owner, primary group and
Windows ACL/SDDL from the host. Non-text formats (including images and PDF) show
the actual host-side `file` output when the fallback option is enabled. File reading
and identification run off the terminal event loop; large replies are chunked and
authenticated using the same file-only transport. Text, preview delay, dimensions,
file information and the 1–256 MiB read limit use the existing preview settings.
Host file paths must be absolute local paths; UNC/network URLs are not fetched.

**Update and restart both host and client with portable 0.6.0 or later.** Terminal
sessions can still connect to an older host, but file operations display an upgrade
message rather than accessing the client filesystem. Stop the previous host before
replacing the extracted package. Existing local pairing keys can be retained.

### Authentication and storage

Messages are versioned, sequenced, HMAC-SHA256 authenticated and committed with an
atomic file rename. Each tab gets a random session ID. The host accepts only
recent signed requests, records consumed request IDs on its **local disk** to
prevent replay after restart, and never executes an unsigned request. Keep clocks
reasonably aligned (requests older than two minutes or over 30 seconds in the
future are rejected). The pairing key must stay outside the exchange folder.
Anyone with that key and write access to the folder can open a shell as the host
user; use a private folder with suitable filesystem permissions.

Terminal and preview/copy file contents on the exchange disk are **not encrypted**. Readers of that
folder can read commands/output. Do not place it in a broadly readable folder;
use volume/share encryption where required. Neither side sends telemetry or
credentials through a network service. Consumed packets are deleted. Empty session
folders may remain; after stopping both sides they can be removed. The host keeps
small local replay receipts under its application data directory. Do not delete
those receipts while running the host. An unclean host shutdown can leave
`host.lock`; remove it only after confirming the previous host is stopped.


## Using the bundled file command

The built-in PowerShell initialization adds the portable `file` folder to **that
shell's PATH** and sets its `MAGIC` variable to the bundled `magic.mgc`. This applies
to both local and remote PowerShell sessions, without changing system PATH or
requiring MSYS2 to be installed. Examples:

```powershell
file .\image.png
file --brief -- '.\my files\document.pdf'
file --mime-type -- .\settings.ini
file --version
```

`file` identifies files; it is not the Unix `find` command or Windows `find.exe`.
The native executable and all its normal options are available. A custom PowerShell
profile that does not use Konsole's initialization can call the EXE directly:

```powershell
& 'C:\Tools\Konsole\file\file.exe' --magic-file 'C:\Tools\Konsole\file\magic.mgc' --brief -- '.\image.png'
```

Interactive file previews now take precedence over the older KIO thumbnail popup,
so binary file identification is not obscured by a competing image thumbnail.
Content-based MIME detection rejects formats such as PDF even when their bytes
are mostly printable text or the filename ends in `.txt`.


## PSReadLine completion and gray predictions

The portable shell uses **PSReadLine**, not a replacement line editor. Tab accepts
an existing visible inline (gray) prediction at the end of the input. Otherwise it
uses PSReadLine `Complete`: a unique result is completed, and another Tab lists
ambiguous matches, in the Bash style. **Ctrl+Space** invokes `MenuComplete` for a
selection list; arrows navigate the list. **Shift+Tab** selects a previous
completion. **Enter alone executes the edited line**; accepting a suggestion does
not execute it. Ctrl+R remains reverse history search.

Gray history/plugin predictions and command argument completion are different
sources. The Tab binding works with visible gray predictions for any command.
Native command names newly installed in an existing PATH directory are discovered
by PowerShell; installing a new PATH directory can require restarting Konsole to
inherit the updated Windows environment.

The package adds upstream **posh-git 1.1.0** from the pinned commit recorded in
`git-completion-runtime.json`, including its MIT license and complete source
archive. When execution policy permits module scripts, it loads automatically
when Git is available. Under Restricted/AllSigned (or a blocked module import),
an integrated provider reads bundled option data instead; it also completes
branches and remotes for common Git commands. No execution policy is changed.
Git itself is supplied by
the user's Git for Windows installation on the relevant local/remote host. Tests
cover `git commit --am` → `--amend`, and actual Tab completion of `git stat` to
`git status`. User prompt styling is preserved; Konsole's directory reporting and
file-listing links continue to work.

For native applications without a specific registered completer, PowerShell 7.6's
`NativeFallback` tries to obtain options from the application's **top-level
`--help`** on the first relevant Tab. It does not forward the entered arguments or
execute the partially typed command. On Windows only console EXEs are queried,
not GUI applications or batch wrappers. The probe runs in the temporary directory,
closes standard input, captures output, times out after one second and caches the
result until the executable changes. It needs no online service. Existing
command-specific completers have priority; PowerShell cmdlet/script parameters
and ordinary file/path completion still use the normal PowerShell providers.

This cannot invent parameter definitions: applications without useful `--help`,
complex subcommands, batch wrappers or custom argument-value grammars need their
own completer, just as with Bash/Zsh. A program can implement `--help` incorrectly.
To disable automatic help probing in a session, use:

```powershell
$KonsoleNativeHelpCompletionEnabled = $false
```

For strict Bash-style Tab without accepting inline predictions, use:

```powershell
$KonsoleAcceptPredictionWithTab = $false
```

Both settings may be defined in the user's profile before Konsole initializes;
Konsole does not edit that profile. The same completion setup is used in remote
PowerShell sessions and runs against programs installed on the **remote host**.
