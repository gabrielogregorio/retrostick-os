/* Controller packets: includes the stick platform to reach its static parser. No device is opened. */
#include "../../src/plat_stick.c"
#include "check.h"

static int wr;

static void packet(uint8_t *p, int player, uint8_t b6, uint8_t b7)
{
    uint8_t in[8] = {0x0a, 0x09, (uint8_t)(player + 2), 0x32, 0x31, 0x82, b6, b7};
    uint8_t x = 0;
    for (int i = 0; i < 8; i += 1) {
        x ^= in[i];
    }
    memcpy(p, in, 8);
    p[8] = (uint8_t)~(x ^ 0x5a);
}

static void send_bytes(const void *d, int n) { CHECK(write(wr, d, n) == n); }

static input_t feed(uint32_t now)
{
    input_t e;
    memset(&e, 0, sizeof e);
    read_serial(&e, now);

    return e;
}

static void test_button_bits(void)
{
    static const struct { uint8_t b6, b7; uint32_t button; } CASES[] = {
        {0x01, 0, BTN_UP}, {0x02, 0, BTN_DOWN}, {0x04, 0, BTN_LEFT}, {0x08, 0, BTN_RIGHT},
        {0x10, 0, BTN_A}, {0x20, 0, BTN_B}, {0x40, 0, BTN_Y}, {0x80, 0, BTN_X},
        {0, 0x01, BTN_START}, {0, 0x02, BTN_SELECT}, {0, 0x04, BTN_L}, {0, 0x08, BTN_R}, {0, 0x10, BTN_MODE},
    };
    uint8_t p[9];
    uint32_t now = 1000;
    for (unsigned i = 0; i < sizeof CASES / sizeof CASES[0]; i += 1) {
        packet(p, 0, CASES[i].b6, CASES[i].b7);
        send_bytes(p, 9);
        input_t e = feed(now);
        CHECK(e.present[0]);
        CHECK(e.bt[0] == CASES[i].button);
        now += 10;
    }
}

static void test_players_are_separate(void)
{
    uint8_t p[9];
    packet(p, 0, 0x10, 0);
    send_bytes(p, 9);
    packet(p, 1, 0x20, 0);
    send_bytes(p, 9);
    input_t e = feed(2000);
    CHECK(e.bt[0] == BTN_A);
    CHECK(e.bt[1] == BTN_B);
    CHECK(e.present[1]);
}

static void test_bad_checksum_is_ignored(void)
{
    uint8_t p[9];
    packet(p, 0, 0, 0);
    send_bytes(p, 9);
    feed(3000);
    packet(p, 0, 0x10, 0);
    p[8] ^= 0xff;
    send_bytes(p, 9);
    packet(p, 0, 0, 0);
    send_bytes(p, 9);
    input_t e = feed(3010);
    CHECK(e.bt[0] == 0);
}

static void test_garbage_before_a_packet_resyncs(void)
{
    uint8_t p[9], junk[5] = {0x00, 0xff, 0x0a, 0x13, 0x77};
    send_bytes(junk, sizeof junk);
    packet(p, 0, 0x01, 0);
    send_bytes(p, 9);
    input_t e = feed(4000);
    CHECK(e.bt[0] == BTN_UP);
}

static void test_split_packet_is_joined(void)
{
    uint8_t p[9];
    packet(p, 0, 0x80, 0);
    send_bytes(p, 4);
    feed(5000);
    send_bytes(p + 4, 5);
    input_t e = feed(5010);
    CHECK(e.bt[0] == BTN_X);
}

static void test_silent_controller_goes_away(void)
{
    uint8_t p[9];
    packet(p, 0, 0x10, 0);
    send_bytes(p, 9);
    input_t e = feed(6000);
    CHECK(e.present[0]);
    e = feed(6499);
    CHECK(e.present[0]);
    e = feed(6500);
    CHECK(!e.present[0]);
    CHECK(e.bt[0] == 0);
}

int main(void)
{
    int fd[2];
    CHECK(pipe(fd) == 0);
    fcntl(fd[0], F_SETFL, O_NONBLOCK);
    ser = fd[0];
    wr = fd[1];
    test_button_bits();
    test_players_are_separate();
    test_bad_checksum_is_ignored();
    test_garbage_before_a_packet_resyncs();
    test_split_packet_is_joined();
    test_silent_controller_goes_away();
    TEST_END();
}
