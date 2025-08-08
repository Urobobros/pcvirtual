# FAR Plan – Codex WHPX-based XT/AT Emulator

## 0. Cíl a rozsah
- [ ] Zprovoznit minimalistický, stabilní emulátor starého PC (XT/AT) nad WHPX.
- [ ] Mimo rozsah verze 1: přesná cyklová emulace CPU, chráněný režim 386+, pokročilé grafické adaptéry a plná přesnost DMA/ISA wait‑states.

## 1. Požadavky (Product/Feature)
- [ ] CPU: Intel 8088 (kompatibilní, např. NEC V20).
- [ ] Jmenovitá frekvence 4,77 MHz (14,31818 MHz / 3).
- [ ] Režim: real‑mode, reset vektor CS:IP = F000:FFF0.
- [ ] 640 kB konvenční RAM + ROM BIOS (min. 32–64 kB).
- [ ] Časování: 8253/8254 PIT, kanál 0 → IRQ0 ~18,2065 Hz.
- [ ] Přerušení: PIC 8259 (master) pro IRQ0 a IRQ1 (klávesnice).
- [ ] Základní I/O: porty 0x40–0x43 (PIT), 0x60/0x64 (klávesnice), 0x20/0x21 (PIC), 0x61 (speaker).
- [ ] Boot: AMI/Phoenix XT BIOS → POST → INT 19h → DOS.
- [ ] Logování: časově značkované I/O logy, IRQ logy, změny režimů PIT.
- [ ] Determinismus: record & replay I/O událostí (min. na úrovni portů).

## 2. Architektura (Approach)
### 2.1 CPU nad WHPX
- [ ] Inicializace partition, VP=1, mapování GPA 0–1 MB + BIOS na 0xF0000.
- [ ] Nastavení registrů: RIP=0xFFF0, CS=F000 (base=0xF0000), RFLAGS=0x2.
- [ ] Hlavní smyčka: `WHvRunVirtualProcessor` + obsluha `IoPortAccess`, `Halt`, `InterruptWindow`.

### 2.2 Časování (PIT → IRQ0)
- [ ] Použít `QueryPerformanceCounter` pro výpočet deadlinu IRQ0.
- [ ] Default perioda ~54,9254 ms; přepočet při změně dělitele.
- [ ] Využít `WaitableTimer`/`Sleep(0)` pro přesné čekání; injektovat INT 08h přes WHPX.

### 2.3 I/O intercepty
- [ ] PIT (0x40–0x43): parser příkazů, LSB/MSB, divider, gate.
- [ ] PIC (0x20/0x21): IMR, OCW1/2/3, EOI, priority, fronta pending IRQ.
- [ ] Klávesnice (0x60/0x64): ring buffer, scancode set 1, generace IRQ1.
- [ ] Speaker (0x61): mirror pro stav PIT ch2 + speaker enable (bez audio v MVP).

### 2.4 BIOS, RAM, média
- [ ] Načíst BIOS binárku; volitelně shadow do RAM.
- [ ] Boot z image (A:): čtení boot sektoru přes INT 19h.

### 2.5 Ladění a logování
- [ ] Strukturovaný log: index, t(ms), IN/OUT, port, size, val, note.
- [ ] IRQ log: t(ms), inject IRQx, ack, EOI.
- [ ] Volba verbosity: I/O only / +IRQ / +PIT režimy / +PIC stavy.

## 3. Akční plán (Roadmap)
- [ ] **M0 – Skeleton běh BIOSu**: vidět první I/O přístupy BIOSu; žádný crash.
- [ ] **M1 – PIT + přesné IRQ0**: DOS TIME/DATE se hýbou; INT 15h AH=86h má správnou délku.
- [ ] **M2 – PIC 8259 a IRQ doručování**: konzistentní pořadí a maskování IRQ0/IRQ1.
- [ ] **M3 – Klávesnice (IRQ1)**: BIOS přijímá klávesy; DOS reaguje; opakovací klávesa funguje.
- [ ] **M4 – Stabilizace a logy**.
- [ ] *(Volitelné: RTC/IRQ8, DMA, floppy řadič, CGA/EGA, speaker audio, HDD.)*

## 4. Akceptační kritéria (Results)
- [ ] AC‑1: XT BIOS projde POST, log zobrazuje smysluplné I/O.
- [ ] AC‑2: 0040:006C přibývá ~18,2065×/s (±0,1 % za 10 min).
- [ ] AC‑3: INT 15h AH=86h čeká ±2 ms pro intervaly 10–500 ms.
- [ ] AC‑4: Stisk klávesy vyvolá IRQ1, BIOS vrátí kód do bufferu, DOS přijme znak.
- [ ] AC‑5: Tick drží ±0,5 % při 100 % CPU loadu hosta.

## 5. Rizika a mitigace
- [ ] R‑1: Drift/latence QPC/WaitableTimer → pinning vlákna, watchdog na jitter, adaptivní „catch‑up“ tick.
- [ ] R‑2: Ztráta přerušení při maskování → fronta pending IRQ + přesné EOI/IMR.
- [ ] R‑3: BIOS závislý na neimplementovaných portech → stubovat a logovat přístupy.
- [ ] R‑4: Neznámé WHPX exity → robustní switch, fallback log.

## 6. Implementační detaily (Notes)
- [ ] `divider==0` interpretovat jako 65536.
- [ ] INT 08h řetězit na INT 1Ch; zajistit včasnou injekci IRQ0.
- [ ] Po ISR (INT 08h/09h) očekávat `OUT 0x20, 0x20` (EOI).
- [ ] U DMA 8237 implementovat wrap při přístupu mimo RAM (budoucnost).
- [ ] Umožnit přehrát log I/O pro reprodukci.

## 7. Test plan (Sanity & Integration)
- [ ] Sanity: BIOS jump z F000:FFF0, čtení BDA, přístup na PIT porty 0x40–0x43.
- [ ] Timer: sledovat 0040:006C 60 s → ~1092–1095 ticků.
- [ ] Delay: INT 15h AH=86h s 100 ms → měřit host QPC (±2 ms).
- [ ] Keyboard: stisk "A" → scancode 0x1E → DOS vypíše "a/A".
- [ ] IRQ robustnost: host CPU 100 % → drift < 0,5 % / 10 min.

## 8. Dodávky (Deliverables)
- [ ] `codex-core`: WHPX init, run‑loop, GPA map.
- [ ] `codex-io`: I/O intercepty + log framework.
- [ ] `codex-pit`: PIT state machine + IRQ0 timing.
- [ ] `codex-pic`: PIC 8259 (IMR/EOI/priority).
- [ ] `codex-kbd`: 8042/keyboard.
- [ ] Dokumentace: README, I/O map, known‑issues, test‑howto.

## 9. Odhad a kapacity
- [ ] M0–M4: ~4–5 týdnů čistého času pro 1 vývojáře (bez grafiky/disku).
- [ ] Testování: 1 den/milestone (manuální + skript měření ticku).

## 10. Otevřené otázky
- [ ] Vyžadujeme už v M1 dynamický přepočet periody podle OUT na 0x40/0x43?
- [ ] Minimální sada portů mimo PIT/PIC/KBD (např. 0x3DA VGA status) pro kompatibilitu POST?

## 11. Přílohy
- [ ] Mapování BDA (segment 0x40) – klíčové offsety: 0x006C (tick count), 0x0017 (klávesy).
- [ ] Reset vektor a pořadí volání BIOS rutin (POST → ...).
