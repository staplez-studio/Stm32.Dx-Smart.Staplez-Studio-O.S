#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t u32; typedef uint8_t u8;
#define HOST 1
#include "diag.c"
/* ---- simulated bus: master open-drain on pins, one slave at slave_addr on (sim_scl, sim_sda) ---- */
static int sim_scl_pin = 0, sim_sda_pin = 1, slave_addr = -1, stuck_low_sda = 0, stuck_low_scl = 0;
static int m_scl = 1, m_sda = 1, s_sda = 1, others[32];
static int st = 0, bits = 0, byte = 0, last_scl = 1, last_sda = 1, ack_next = 0;
static int total = 0, ffc = 0, pages_written = 0, filled = 0, got_af = 0; static u8 lastdata[200]; static int nd = 0;
static int mode_data = 0, first = 0, bytes_in_txn = 0, addr_ok = 0;
static int bus_sda(void) { return (m_sda && s_sda && !stuck_low_sda); }
static int bus_scl(void) { return (m_scl && !stuck_low_scl); }
static void slave_step(void)
{
    int c = bus_scl(), d = bus_sda();
    if (last_scl && c && last_sda && !d) { st = 1; bits = 0; byte = 0; bytes_in_txn = 0; addr_ok = 0; s_sda = 1; }       /* START */
    else if (last_scl && c && !last_sda && d) { st = 0; s_sda = 1; }                                                      /* STOP */
    else if (!last_scl && c && st) {                                                                                      /* SCL rising: sample */
        if (bits < 8) { byte = (byte << 1) | d; bits++; }
        else { bits = 9; }
    } else if (last_scl && !c && st) {                                                                                    /* SCL falling: next */
        if (bits == 8) {                                                                                                  /* after 8th bit: drive ACK */
            if (bytes_in_txn == 0) { addr_ok = ((byte >> 1) == slave_addr); s_sda = addr_ok ? 0 : 1; }
            else { s_sda = addr_ok ? 0 : 1; if (addr_ok) { total++; if (byte == 0xFF) ffc++; if (nd < 200) lastdata[nd++] = (u8)byte; } }
        } else if (bits == 9) { s_sda = 1; bits = 0; byte = 0; bytes_in_txn++; }
    }
    last_scl = c; last_sda = d;
}
void pin_od(int pin) { (void)pin; }
void pin_w(int pin, int v) { if (pin == sim_scl_pin) m_scl = v; else if (pin == sim_sda_pin) m_sda = v; slave_step(); }
int pin_r(int pin) { if (pin == sim_scl_pin) return bus_scl(); if (pin == sim_sda_pin) return bus_sda(); return 1; }
void leds(int on) { (void)on; } void hw_init(void) {} void delay_ms(u32 ms) { (void)ms; } void dly(void) {}
static int fails, checks;
#define CHECK(c,m) do{checks++; if(!(c)){fails++; printf("FAIL: %s\n",m);} else printf("ok   %s\n",m);}while(0)
static void reset_sim(void){ total=ffc=0; m_scl=m_sda=s_sda=1; st=0; last_scl=last_sda=1; nd=0; stuck_low_sda=stuck_low_scl=0; }
int main(void)
{
    reset_sim(); slave_addr = 0x3C; sim_scl_pin = 0; sim_sda_pin = 1;
    CHECK(test_pair(0, 1) == 1, "display at 0x3C on PB0/PB1: code 1 (1 blink)");
    CHECK(total > 1000 && ffc > 1000, "init commands plus 8 full pages (all pixels on) were received by the display");
    int ff = 0; for (int i = 0; i < nd; i++) if (lastdata[i] == 0xFF) ff++;
    reset_sim(); slave_addr = 0x3D; CHECK(test_pair(0, 1) == 2, "display at 0x3D: code 2");
    reset_sim(); slave_addr = -1; CHECK(test_pair(0, 1) == 3, "nothing connected, lines fine: code 3");
    reset_sim(); slave_addr = 0x3C; stuck_low_sda = 1; CHECK(test_pair(0, 1) == 5, "SDA stuck low: code 5");
    reset_sim(); slave_addr = 0x3C; stuck_low_scl = 1; CHECK(test_pair(0, 1) == 4, "SCL stuck low: code 4");
    reset_sim(); slave_addr = 0x3C; sim_scl_pin = 8; sim_sda_pin = 9; CHECK(test_pair(8, 9) == 1, "display on PB8/PB9: code 1");
    reset_sim(); slave_addr = 0x3C; sim_scl_pin = 8; sim_sda_pin = 9; CHECK(test_pair(0, 1) == 3, "display on the other pair: the pair under test sees nothing (code 3)");
    printf("\n%d checks, %d failed\n", checks, fails); return fails != 0;
}
