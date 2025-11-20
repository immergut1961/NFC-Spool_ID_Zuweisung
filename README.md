# NFC-Spool_ID_Zuweisung

Automatisierte NFC-basierte Zuordnung von Filamentspulen zu MMU-Gates für Klipper/MMU-Setups (z.B. Neptune, Prusa, Creality).

## Features

- Liest NFC-IDs der Filamentspulen via Serielle Schnittstelle (pico_nfc o.ä.)
- Überwacht alle MMU-Gate-Sensoren per Moonraker/Klipper API
- Ordnet eine SpoolID dem Gate zu, in das sie nach Scan eingelegt wird (Flankenerkennung)
- Sendet ein individuelles GCode-Macro zum Drucker/Controller

## Installation

1. Kopiere `auto_spool_gate.py` nach `/home/<dein_user>/`
2. Passe ggf. Serialport und Hostadresse im Skript an
3. Installiere Python-Packages:
    ```bash
    sudo apt install python3-serial python3-requests
    ```
4. Systemd-Service installieren: siehe `auto_spool_gate.service`
5. Service starten:
    ```bash
    sudo systemctl daemon-reload
    sudo systemctl enable auto_spool_gate.service
    sudo systemctl start auto_spool_gate.service
    ```

## Funktionsweise

- Nach Scan einer SpoolID legt das Skript ein Beobachtungsfenster (default 20s) an
- Das Gate, das innerhalb dieser Zeit von "leer" auf "belegt" (Flanke False→True) springt, wird erkannt und ans Macro übergeben

## Hinweise

- Achte darauf, dass keine sensiblen Daten (Passwörter, IPs außerhalb von localhost, persönliche Namen) im Repository gespeichert werden
- Konfigurationswerte können jederzeit angepasst werden

## Lizenz

Siehe LICENSE.
