# Thruster 12V Controller Firmware
## UiS Subsea 2025/2026

STM32G431 firmware for thruster ESC control via CAN-bus.

## Funksjonalitet

- Mottar **CAN ID 33 (ROVCMD)** med 8x int8 verdier (-100 til 100)
- Konverterer til **PWM 1100-1900µs** for ESC-er
- Sender **heartbeat på CAN ID 158** (HB12VTHR)
- **Safety timeout**: Stopper motorer etter 1 sekund uten kommandoer

## Verdimapping

| Input | PWM Output | ESC Oppførsel |
|-------|------------|---------------|
| -100  | 1100 µs    | Full revers   |
| 0     | 1500 µs    | Nøytral/Arm   |
| +100  | 1900 µs    | Full forover  |

## Hardware Konfigurasjon

### STM32G431KB/RB Pinner

**FDCAN1 (500 kbps):**
- PA11: CAN RX
- PA12: CAN TX

**TIM1 PWM (Thruster 1-4):**
- PA8:  TIM1_CH1 → Thruster 1
- PA9:  TIM1_CH2 → Thruster 2
- PA10: TIM1_CH3 → Thruster 3
- PA11: TIM1_CH4 → Thruster 4

**TIM2 PWM (Thruster 5-8):**
- PA0:  TIM2_CH1 → Thruster 5
- PA1:  TIM2_CH2 → Thruster 6
- PB10: TIM2_CH3 → Thruster 7
- PB11: TIM2_CH4 → Thruster 8

**LED:**
- PA5: Status LED (LD2)

## Kompilering

### Metode 1: STM32CubeIDE
1. Opprett nytt STM32G431KB prosjekt
2. Konfigurer pinner i .ioc fil:
   - FDCAN1: PA11/PA12
   - TIM1/TIM2: PWM kanaler
3. Kopier main.c og main.h til Core/Src og Core/Inc
4. Bygg og flash

### Metode 2: Makefile
```bash
# Installer ARM toolchain
sudo apt install gcc-arm-none-eabi

# Bygg
make

# Flash med ST-Link
st-flash write build/thruster_12v.bin 0x8000000
```

## LED Indikasjoner

| Mønster | Betydning |
|---------|-----------|
| Sakte blink (500ms) | OK - CAN tilkoblet |
| Rask blink (100ms)  | Feil - Ingen CAN |
| Konstant på         | Error_Handler |

## CAN Protocol

### Mottar (RX)
| CAN ID | Data | Beskrivelse |
|--------|------|-------------|
| 33     | 8x int8 | Thruster verdier (-100 til 100) |
| 126    | -   | Heartbeat forespørsel |

### Sender (TX)
| CAN ID | Data | Beskrivelse |
|--------|------|-------------|
| 158    | "polo!" + status | Heartbeat svar |

## Feilsøking

### ESC piper fortsatt?
1. Sjekk at STM32 har strøm (grønn LED)
2. Sjekk PWM-signal med oscilloskop (skal være 1500µs)
3. Sjekk CAN-bus terminering (60Ω mellom CAN_H og CAN_L)
4. Verifiser CAN bitrate (500 kbps)

### Ingen heartbeat svar?
1. Sjekk CAN filter konfigurasjon
2. Sjekk at FDCAN1 er riktig initialisert
3. Bruk `candump can0` for å se trafikk

## Kontakt

Elektronikk-gruppen UiS Subsea
