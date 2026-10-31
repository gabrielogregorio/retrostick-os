#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include "check.h"
#include "plat.h"
#include "stick_internal.h"

#define PACKET_BYTES 9

static int write_end;

static void build_packet(uint8_t *packet, int player, uint8_t b6, uint8_t b7)
{
    uint8_t body[PACKET_BYTES - 1] = {0x0a, 0x09, (uint8_t)(player + 2), 0x32, 0x31, 0x82, b6, b7};
    uint8_t checksum = 0;
    for (unsigned byte_index = 0; byte_index < sizeof body; byte_index += 1) {
        checksum ^= body[byte_index];
    }
    memcpy(packet, body, sizeof body);
    packet[PACKET_BYTES - 1] = (uint8_t)~(checksum ^ 0x5a);
}

static void send_bytes(const void *bytes, int count)
{
    CHECK(write(write_end, bytes, count) == count);
}

static input_t feed(uint32_t now)
{
    input_t input;
    memset(&input, 0, sizeof input);
    stick_input_poll(&input, now);

    return input;
}

static void test_button_bits(void)
{
    static const struct {
        uint8_t b6, b7;
        uint32_t button;
    } CASES[] = {
        {0x01, 0, BTN_UP}, {0x02, 0, BTN_DOWN}, {0x04, 0, BTN_LEFT}, {0x08, 0, BTN_RIGHT}, {0x10, 0, BTN_A},
        {0x20, 0, BTN_B},  {0x40, 0, BTN_Y},    {0x80, 0, BTN_X},    {0, 0x01, BTN_START}, {0, 0x02, BTN_SELECT},
        {0, 0x04, BTN_L},  {0, 0x08, BTN_R},    {0, 0x10, BTN_MODE},
    };

    uint8_t packet[PACKET_BYTES];
    uint32_t now = 1000;
    for (unsigned case_index = 0; case_index < sizeof CASES / sizeof CASES[0]; case_index += 1) {
        build_packet(packet, 0, CASES[case_index].b6, CASES[case_index].b7);
        send_bytes(packet, PACKET_BYTES);
        input_t input = feed(now);
        CHECK(input.present[0]);
        CHECK(input.bt[0] == CASES[case_index].button);
        now += 10;
    }
}

static void test_players_are_separate(void)
{
    uint8_t packet[PACKET_BYTES];
    build_packet(packet, 0, 0x10, 0);
    send_bytes(packet, PACKET_BYTES);
    build_packet(packet, 1, 0x20, 0);
    send_bytes(packet, PACKET_BYTES);
    input_t input = feed(2000);
    CHECK(input.bt[0] == BTN_A);
    CHECK(input.bt[1] == BTN_B);
    CHECK(input.present[1]);
}

static void test_bad_checksum_is_ignored(void)
{
    uint8_t packet[PACKET_BYTES];
    build_packet(packet, 0, 0, 0);
    send_bytes(packet, PACKET_BYTES);
    feed(3000);
    build_packet(packet, 0, 0x10, 0);
    packet[PACKET_BYTES - 1] ^= 0xff;
    send_bytes(packet, PACKET_BYTES);
    build_packet(packet, 0, 0, 0);
    send_bytes(packet, PACKET_BYTES);
    input_t input = feed(3010);
    CHECK(input.bt[0] == 0);
}

static void test_garbage_before_a_packet_resyncs(void)
{
    uint8_t packet[PACKET_BYTES], junk[5] = {0x00, 0xff, 0x0a, 0x13, 0x77};
    send_bytes(junk, sizeof junk);
    build_packet(packet, 0, 0x01, 0);
    send_bytes(packet, PACKET_BYTES);
    input_t input = feed(4000);
    CHECK(input.bt[0] == BTN_UP);
}

static void test_split_packet_is_joined(void)
{
    uint8_t packet[PACKET_BYTES];
    build_packet(packet, 0, 0x80, 0);
    send_bytes(packet, 4);
    feed(5000);
    send_bytes(packet + 4, 5);
    input_t input = feed(5010);
    CHECK(input.bt[0] == BTN_X);
}

static void test_silent_controller_goes_away(void)
{
    uint8_t packet[PACKET_BYTES];
    build_packet(packet, 0, 0x10, 0);
    send_bytes(packet, PACKET_BYTES);
    input_t input = feed(6000);
    CHECK(input.present[0]);
    input = feed(6499);
    CHECK(input.present[0]);
    input = feed(6500);
    CHECK(!input.present[0]);
    CHECK(input.bt[0] == 0);
}

int main(void)
{
    int fd[2];
    CHECK(pipe(fd) == 0);
    fcntl(fd[0], F_SETFL, O_NONBLOCK);
    stick_input_use_fd(fd[0]);
    write_end = fd[1];
    test_button_bits();
    test_players_are_separate();
    test_bad_checksum_is_ignored();
    test_garbage_before_a_packet_resyncs();
    test_split_packet_is_joined();
    test_silent_controller_goes_away();
    TEST_END();
}
