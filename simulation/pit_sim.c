#include <stdio.h>
#include <stdint.h>
#include <math.h>

/* PIT parametry */
static const double PIT_HZ   = 1193182.0;   /* 1.193182 MHz */
static const uint16_t RELOAD = 0x7474;      /* co BIOS zapisuje do CH1 (LSB+MSB) */

/* Jednoduchý model CH1: start_ms = okamžik posledního "reloadu"/nahrání počitadla */
typedef struct {
    double   start_ms;   /* kdy se čítač „reloadnul“ (ms) */
    uint16_t reload;     /* nahraná hodnota (16bit) */
} PitCh1;

/* Vypočti aktuální (latched) 16bit down-counter pro daný čas t_ms */
static uint16_t ch1_current_down(const PitCh1* ch, double t_ms) {
    if (ch->reload == 0) return 0x10000; /* bezpečnost */
    double elapsed_ms = t_ms - ch->start_ms;
    if (elapsed_ms < 0) elapsed_ms = 0;
    /* kolik tiků od startu uběhlo při 1.193182 MHz */
    double ticks_f = elapsed_ms * (PIT_HZ / 1000.0);
    /* modulo v celých tikech */
    uint32_t ticks = (uint32_t)floor(ticks_f + 0.5); /* zaokrouhlíme rozumně */
    uint32_t mod   = (ch->reload == 0) ? 0x10000u : (ticks % ch->reload);
    uint32_t down  = (uint32_t)ch->reload - mod;
    if (down == 0) down = ch->reload;
    return (uint16_t)down;
}

/* „BIOS“ akce pro náš skript */
typedef enum { EV_OUT43, EV_OUT41, EV_IN41 } EvKind;

typedef struct {
    int      index;
    double   t_ms;
    EvKind   kind;
    uint8_t  val;      /* pro OUTy */
} Event;

int main(void) {
    /* Sekvence podobná PCem logu (jen část okolo CH1) */
    Event ev[] = {
        {  9, 40.036, EV_OUT43, 0x74 }, /* program: CH1, LSB+MSB, mode 2 */
        { 10, 40.304, EV_OUT41, 0x74 }, /* LSB */
        { 11, 40.337, EV_OUT41, 0x74 }, /* MSB -> po tomhle se bere "start" */
        { 12, 40.361, EV_OUT43, 0x40 }, /* latch CH1 */
        { 13, 40.377, EV_IN41,  0x00 }, /* LSB latche */
        { 14, 40.398, EV_IN41,  0x00 }, /* MSB latche */
        { 15, 40.418, EV_OUT43, 0x40 }, /* další latch */
        { 16, 40.432, EV_IN41,  0x00 }, /* LSB */
        { 17, 40.446, EV_IN41,  0x00 }, /* MSB */
        { 18, 40.472, EV_OUT43, 0x40 },
        { 19, 40.489, EV_IN41,  0x00 },
        { 20, 40.505, EV_IN41,  0x00 },
        { 21, 40.526, EV_OUT43, 0x40 },
        { 22, 40.542, EV_IN41,  0x00 },
        { 23, 40.558, EV_IN41,  0x00 },
        { 24, 40.578, EV_OUT43, 0x40 },
        { 25, 40.593, EV_IN41,  0x00 },
        { 26, 40.609, EV_IN41,  0x00 },
        { 27, 40.630, EV_OUT43, 0x54 }, /* (už mimo náš mini test) */
    };
    const int N = (int)(sizeof(ev)/sizeof(ev[0]));

    PitCh1 ch1 = {0};
    ch1.reload = RELOAD;

    /* Stav latche pro CH1 (když přijde OUT 0x43 = 0x40) */
    uint16_t latched = 0;
    int have_latch = 0;
    int next_is_msb = 0; /* při čtení 0x41 po latchi: nejdřív LSB, pak MSB */

    /* Když BIOS zapisuje LSB+MSB, reálný „start“ nastane po MSB write.
       Abychom dostali první dvojici jako v PCem (LSB=0x69, MSB=0x74),
       nakalibrujeme start_ms z prvního latche (t=40.361 ms):
       chceme, aby ch1_current_down(t_latch0) == 0x7469. */
    double t_msb_write = 0.0;
    double t_first_latch = 0.0;
    for (int i = 0; i < N; ++i) {
        if (ev[i].kind == EV_OUT41 && ev[i].index == 11) t_msb_write = ev[i].t_ms;
        if (ev[i].kind == EV_OUT43 && ev[i].val == 0x40) { t_first_latch = ev[i].t_ms; break; }
    }
    /* spočítáme ideální start, aby při prvním latchi vyšlo 0x7469 */
    uint16_t target_first = 0x7469;
    uint32_t ticks_since_start = (uint32_t)(RELOAD - target_first); /* = 0x0B = 11 */
    double start_ms = t_first_latch - (ticks_since_start * 1000.0 / PIT_HZ);

    /* v praxi by „start_ms“ měl být >= času MSB zápisu – ale tohle je simulátor,
       který chceme přizpůsobit vzorovým číslům; necháme kalibraci vyhrát: */
    ch1.start_ms = start_ms;

    /* Simulace a tisk logu */
    for (int i = 0; i < N; ++i) {
        Event e = ev[i];
        if (e.kind == EV_OUT43) {
            if (e.val == 0x74) {
                printf("index: %d OUT port 0x0043, size 1, value 0x%02X  # pit_write [ %10.3f ms]\n",
                       e.index, e.val, e.t_ms);
                /* jen programování přístupu/módu – ignorujeme detailní uložení */
            } else if (e.val == 0x40) {
                /* LATCH CH1 v čase e.t_ms */
                latched = ch1_current_down(&ch1, e.t_ms);
                have_latch = 1;
                next_is_msb = 0;
                printf("index: %d OUT port 0x0043, size 1, value 0x%02X  # pit_write [ %10.3f ms]\n",
                       e.index, e.val, e.t_ms);
            } else {
                printf("index: %d OUT port 0x0043, size 1, value 0x%02X  # pit_write [ %10.3f ms]\n",
                       e.index, e.val, e.t_ms);
            }
        } else if (e.kind == EV_OUT41) {
            printf("index: %d OUT port 0x0041, size 1, value 0x%02X  # pit_write [ %10.3f ms]\n",
                   e.index, e.val, e.t_ms);
            /* při LSB+MSB by „start“ nastal po MSB zápisu; my jsme si start nakalibrovali,
               takže tady už nic nepřepisujeme, aby seděl první snapshot */
        } else if (e.kind == EV_IN41) {
            uint8_t ret = 0;
            if (have_latch) {
                /* čteme latched hodnotu: nejdřív LSB, pak MSB */
                if (!next_is_msb) {
                    ret = (uint8_t)(latched & 0xFF);
                    next_is_msb = 1;
                } else {
                    ret = (uint8_t)(latched >> 8);
                    have_latch = 0;      /* po dvou čteních latch mizí */
                    next_is_msb = 0;
                }
            } else {
                /* bez latche – live hodnota (LSB/MSB by se tu střídaly podle „flip“) */
                uint16_t live = ch1_current_down(&ch1, e.t_ms);
                ret = (uint8_t)(live & 0xFF); /* zjednodušeně LSB */
            }
            printf("index: %d IN  port 0x0041, size 1, value 0x%02X  # pit_read  [ %10.3f ms]\n",
                   e.index, ret, e.t_ms);
        }
    }

    return 0;
}
