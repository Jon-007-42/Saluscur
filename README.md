# Saluscur – modulær firmware til ventil-produktionslinje 🚀

Dette repo samler al kode, G-kode og dokumentation til *Saluscur* – en semi-automatisk linje, der fremstiller ventilpuder ved at svejse og laserskære plastfolie på rulle.

| Modul | MCU / board | Protokol | Funktion |
|-------|-------------|----------|-----------|
| **master-mega** | Arduino Mega 2560 | I²C + UART | Touch-HMI, sekvens­styring, BUSY-poll |
| **valve-mega** | Arduino Mega 2560 | I²C-slave | 4 × TMC5160 stepperdrivere der roterer ventilrotor |
| **rolls-mega** | Arduino Mega 2560 | I²C-slave | Fremfører folie + strammer ruller |
| **laser-btt** | BTT SKR 1.x (Marlin 2.1) | UART (M115) | Skærer ventil- og konturhuller |
| **ultra-btt** | BTT SKR 1.x (Marlin 2.1) | UART | UL-svejser ventil og kontur |
| **docs** | – | – | Pin-outs, sekvens­diagrammer, mermaid-grafer |
| **test** | – | – | Selvtests (under opbygning) |

### Makro-sekvens (kort)

1. **HOMING** – Rolls, Valves, Laser og Ultra homes parallelt  
2. **Valves_Feed (manuel)** – operatør lægger plast-ventil og trykker *START*  
3. **Valves_90** – rotor 90 ° CCW  
4. **LAS_VAL** – laser skærer hul (+ sug)  
5. **Valves_180** – yderligere 90 °, i alt 180 °  
6. **UL_VAL** – UL-svejser ventil  
7. **ROLLS_MAIN_FEED** – ruller 375 mm frem  
8. **UL_KONT** – UL-svejser kontur  
9. **LAS_KONT** – laser skærer kontur  
10. **Manual remove** – operatør tager færdige emner

> Detaljeret sekvens- og kommando­skema findes i *docs/sequence.md*.

### Kommunikation

* **I²C**  – Master (0x00) ←→ Rolls (0x04) & Valves (0x05)  
  * Kommandoer: `ROLLS_HOME`, `VAL_HOME`, `VAL_90`, `VAL_180`, `MAIN`, `STOP`
  * Slaver svarer `BUSY` / `DONE` på `onRequest`.
* **UART1** – Master → Laser-BTT (115 200 baud)  
* **UART2** – Master → Ultra-BTT (115 200 baud)

### Kom i gang

```bash
git clone https://github.com/Jon-007-42/Saluscur
cd Saluscur/master-mega
# Åbn i Arduino IDE, vælg Mega 2560, upload …




flowchart TD
    %% ---------- KNAPPER ----------
    HOMING_BTN["Tryk HOMING"] --> HOMING_PROMPT["HOMING – vent …"]

    %% ---------- FIRE HOMINGS (kører parallelt i virkeligheden) ----------
    HOMING_PROMPT --> ROLLS_HOME["ROLLS_HOME"]
    HOMING_PROMPT --> VAL_HOME["VAL_HOME"]
    HOMING_PROMPT --> LAS_HOME["LAS_HOME.gc"]
    HOMING_PROMPT --> UL_HOME["UL_HOME.gc"]

    %% Når alle fire er færdige samles de til én node
    ROLLS_HOME --> HOMED
    VAL_HOME  --> HOMED
    LAS_HOME  --> HOMED
    UL_HOME   --> HOMED

    HOMED["Læg plast-ventil og tryk START"] --> START_BTN["Tryk START"]

    %% ---------- MANUEL FEED ----------
    START_BTN --> VAL_FEED["Valves_Feed (manuel)"]

    %% ---------- AUTOMATISK SEKVENT ----------
    VAL_FEED --> VAL_90["VAL_90 (90°)"]
    VAL_90   --> LAS_VAL["LAS_VAL.gc"]
    LAS_VAL  --> VAL_180["VAL_180 (180°)"]
    VAL_180  --> UL_VAL["UL_VAL.gc"]
    UL_VAL   --> ROLLS_MAIN["ROLLS_MAIN (375 mm)"]
    ROLLS_MAIN --> UL_KONT["UL_KONT.gc"]
    UL_KONT  --> LAS_KONT["LAS_KONT.gc"]
    LAS_KONT --> REMOVE["Fjern produkter – tryk HOMING"]

