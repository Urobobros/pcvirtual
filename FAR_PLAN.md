# Codex FAR Plan Checklist

## Milestone M0 – Skeleton BIOS Run (T+1 week)
- [ ] Load BIOS binary into memory
- [ ] Create WHPX partition and map 0–1 MB RAM plus BIOS at 0xF0000
- [ ] Initialize registers for reset vector (CS:F000, IP:FFF0, RFLAGS=0x2)
- [ ] Run virtual processor; handle IoPortAccess, Halt, InterruptWindow exits
- [ ] Display initial BIOS I/O log

## Milestone M1 – PIT + Precise IRQ0 (T+2 weeks)
- [ ] Emulate PIT 8253/8254 on ports 0x40–0x43
- [ ] Schedule IRQ0 at ~18.2065 Hz using QueryPerformanceCounter
- [ ] Inject INT 08h and verify tick count at 0040:006C

## Milestone M2 – PIC 8259 and IRQ Delivery (T+3 weeks)
- [ ] Implement PIC registers, IMR, priority and EOI handling
- [ ] Maintain pending IRQ queue and ensure no lost IRQ0/IRQ1

## Milestone M3 – Keyboard Controller (IRQ1) (T+4 weeks)
- [ ] Implement 8042 controller on ports 0x60/0x64 with ring buffer
- [ ] Generate scancode set 1 and deliver IRQ1 to BIOS
- [ ] Confirm DOS reads keystrokes

## Milestone M4 – Stabilization and Logs (T+5 weeks)
- [ ] Add structured I/O and IRQ logs with timestamps
- [ ] Provide record & replay of I/O events
- [ ] Document I/O map and known issues

## Optional Next Steps
- [ ] RTC/IRQ8 support
- [ ] DMA controller
- [ ] Floppy controller
- [ ] CGA/EGA graphics
- [ ] Speaker audio
- [ ] HDD controller

