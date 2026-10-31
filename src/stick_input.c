/* Stick input: controller receiver on /dev/ttyS1 and the simulated keys of headless runs.
 *
 * Receiver: 115200 8N1, 9-byte packet
 *   0A 09 PP xx xx xx B6 B7 CK   PP 02 = player 1, 03 = player 2
 *   CK = ~(XOR(bytes 0..7) ^ 0x5A)
 * The B6/B7 bit map is the BUTTON_MAP table below; the diagnostics screen (SELECT) shows the raw
 * packet for checking.
 *
 * Headless: STICK_KEYS=D,D,R,A,S gives simulated keys, one every 15 input reads, then quits
 *   (U D L R = directions, A B X Y, S = select, T = start, M = mode) */
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include "plat.h"
#include "stick_internal.h"

#define PLAYERS 2
#define PACKET_BYTES 9
#define PACKET_MAGIC_0 0x0a
#define PACKET_MAGIC_1 0x09
#define PACKET_CHANNEL 2
#define PACKET_CHANNEL_PLAYER_1 2
#define PACKET_CHANNEL_PLAYER_2 3
#define PACKET_LOGGED_START 3 /* bytes 3 to 7 hold the buttons and possible analog values */
#define PACKET_LOGGED_BYTES 5
#define PACKET_B6 6
#define PACKET_B7 7
#define PACKET_CHECKSUM 8
#define PACKET_CHECKSUM_KEY 0x5a
#define B6_BITS 8
#define B7_BITS 5

#define PRESENT_TIMEOUT_MS 500
#define ALWAYS_LOGGED_PACKETS 30
#define PACKET_LOG_CAP_BYTES 1500000
#define STRAY_LOG_CAP_BYTES 200000
#define STRAY_BYTES 48
#define LOG_SYNC_INTERVAL_MS 2000

#define READS_PER_KEY 15
#define LAST_PRESSED_PHASE 3 /* released for the rest of the key's reads */
#define READS_BEFORE_QUIT 30

/* B6 bit (0..7) and B7 bit (8..12) -> button.
 * L2 and R2 arrive on the SAME bits as L1 and R1 (the receiver does not tell them apart).
 * B7.4 is the MODE button: it changes no mode, so it is free. L3 and R3 do not arrive. */
static const uint32_t BUTTON_MAP[B6_BITS + B7_BITS] = {
    BTN_UP,    BTN_DOWN,   BTN_LEFT, BTN_RIGHT, BTN_A,    BTN_B, BTN_Y, BTN_X, /* B6.0 .. B6.7 */
    BTN_START, BTN_SELECT, BTN_L,    BTN_R,     BTN_MODE,                      /* B7.0 .. B7.4 */
};

static struct {
    uint8_t buffer[256];
    int length;
    int fd;
    uint32_t buttons[PLAYERS];
    uint32_t last_seen[PLAYERS];
    uint8_t last_packet[PLAYERS][PACKET_BYTES];
    int packets_logged[PLAYERS];
} receiver = {.fd = -1};

static struct {
    long packet_bytes;
    long stray_bytes;
    long synced_bytes;
    uint32_t last_sync;
    uint8_t stray[STRAY_BYTES];
    int stray_count;
} receiver_log;

static const char *sim_keys;

static void open_serial(void)
{
    const char *path = getenv("STICK_SERIAL") ? getenv("STICK_SERIAL") : "/dev/ttyS1";
    receiver.fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (receiver.fd < 0) {
        return;
    }
    struct termios settings;
    tcgetattr(receiver.fd, &settings);
    cfmakeraw(&settings);
    cfsetispeed(&settings, B115200);
    cfsetospeed(&settings, B115200);
    settings.c_cflag |= CLOCAL | CREAD;
    tcsetattr(receiver.fd, TCSANOW, &settings);
}

void stick_input_init(void)
{
    if (stick_video_headless()) {
        sim_keys = getenv("STICK_KEYS");
        if (!sim_keys) {
            sim_keys = "";
        }
    }
    open_serial();
}

void stick_input_use_fd(int fd)
{
    receiver.fd = fd;
}

void stick_input_log_state(FILE *log)
{
    fprintf(log, "--- bytes in serial buffer now: %d, serial fd %d\n", receiver.length, receiver.fd);
}

static int packet_is_valid(const uint8_t *packet)
{
    if (packet[0] != PACKET_MAGIC_0 || packet[1] != PACKET_MAGIC_1) {
        return 0;
    }
    uint8_t checksum = 0;
    for (int byte_index = 0; byte_index < PACKET_CHECKSUM; byte_index += 1) {
        checksum ^= packet[byte_index];
    }
    if ((uint8_t)~(checksum ^ PACKET_CHECKSUM_KEY) != packet[PACKET_CHECKSUM]) {
        return 0;
    }

    return packet[PACKET_CHANNEL] == PACKET_CHANNEL_PLAYER_1 || packet[PACKET_CHANNEL] == PACKET_CHANNEL_PLAYER_2;
}

/* logs to STICK_LOG each packet that CHANGED (button map and controller 2) */
static void log_packet(int player, const uint8_t *packet, uint32_t now)
{
    FILE *log = stick_log_file();
    /* in game, no writing to the card on each button */
    if (!log || receiver_log.packet_bytes > PACKET_LOG_CAP_BYTES || stick_video_in_game()) {
        return;
    }
    receiver_log.packet_bytes +=
        fprintf(log, "t=%u P%d %02X %02X %02X %02X %02X %02X %02X %02X %02X\n", now, player + 1, packet[0], packet[1],
                packet[2], packet[3], packet[4], packet[5], packet[6], packet[7], packet[8]);
}

static void remember_stray_byte(uint8_t stray_byte)
{
    if (receiver_log.stray_count < STRAY_BYTES) {
        receiver_log.stray[receiver_log.stray_count] = stray_byte;
        receiver_log.stray_count += 1;
    }
}

/* bytes outside the known format also go to the log ("raw:"): if the controller's ANALOG mode sends
 * a different packet, it shows up here instead of vanishing silently */
static void flush_stray_bytes(uint32_t now)
{
    FILE *log = stick_log_file();
    int count = receiver_log.stray_count;
    if (count && log && !stick_video_in_game() && receiver_log.stray_bytes < STRAY_LOG_CAP_BYTES) {
        receiver_log.stray_bytes += fprintf(log, "t=%u raw:", now);
        for (int stray_index = 0; stray_index < count; stray_index += 1) {
            receiver_log.stray_bytes += fprintf(log, " %02X", receiver_log.stray[stray_index]);
        }
        receiver_log.stray_bytes += fprintf(log, "%s\n", count == STRAY_BYTES ? " ..." : "");
    }
    receiver_log.stray_count = 0;
}

static uint32_t packet_buttons(const uint8_t *packet)
{
    uint32_t buttons = 0;
    for (int button_bit = 0; button_bit < B6_BITS; button_bit += 1) {
        if (packet[PACKET_B6] & (1 << button_bit)) {
            buttons |= BUTTON_MAP[button_bit];
        }
    }
    for (int button_bit = 0; button_bit < B7_BITS; button_bit += 1) {
        if (packet[PACKET_B7] & (1 << button_bit)) {
            buttons |= BUTTON_MAP[B6_BITS + button_bit];
        }
    }

    return buttons;
}

static void accept_packet(const uint8_t *packet, uint32_t now)
{
    int player = packet[PACKET_CHANNEL] - PACKET_CHANNEL_PLAYER_1;
    receiver.buttons[player] = packet_buttons(packet);
    /* logs the first 30 of each player and, after that, whenever any byte from 3 to 7
     * changes (buttons and possible analog values); the 1.5 MB cap holds back noise */
    const uint8_t *logged_bytes = receiver.last_packet[player] + PACKET_LOGGED_START;
    if (receiver.packets_logged[player] < ALWAYS_LOGGED_PACKETS ||
        memcmp(logged_bytes, packet + PACKET_LOGGED_START, PACKET_LOGGED_BYTES)) {
        log_packet(player, packet, now);
        receiver.packets_logged[player] += 1;
    }
    memcpy(receiver.last_packet[player], packet, PACKET_BYTES);
    receiver.last_seen[player] = now;
}

static void read_serial_bytes(void)
{
    if (receiver.fd < 0) {
        return;
    }
    int received = read(receiver.fd, receiver.buffer + receiver.length, sizeof receiver.buffer - receiver.length);
    if (received > 0) {
        receiver.length += received;
    }
}

static void parse_buffer(uint32_t now)
{
    int offset = 0;
    while (receiver.length - offset >= PACKET_BYTES) {
        const uint8_t *packet = receiver.buffer + offset;
        if (!packet_is_valid(packet)) {
            remember_stray_byte(packet[0]);
            offset += 1;
            continue;
        }
        flush_stray_bytes(now);
        accept_packet(packet, now);
        offset += PACKET_BYTES;
    }
    memmove(receiver.buffer, receiver.buffer + offset, receiver.length - offset);
    receiver.length -= offset;
}

/* in the menu, the controller log goes to the card every 2 s if there is anything new (turning the stick off
 * does not lose it); in game never (each fsync costs ~20 ms) */
static void sync_log_periodically(uint32_t now)
{
    FILE *log = stick_log_file();
    long logged_bytes = receiver_log.packet_bytes + receiver_log.stray_bytes;
    if (!log || stick_video_in_game() || logged_bytes == receiver_log.synced_bytes ||
        now - receiver_log.last_sync <= LOG_SYNC_INTERVAL_MS) {
        return;
    }
    fflush(log);
    fsync(fileno(log));
    receiver_log.synced_bytes = logged_bytes;
    receiver_log.last_sync = now;
}

void stick_input_poll(input_t *input, uint32_t now)
{
    read_serial_bytes();
    parse_buffer(now);
    sync_log_periodically(now);
    for (int player = 0; player < PLAYERS; player += 1) {
        uint32_t last_seen = receiver.last_seen[player];
        input->present[player] = last_seen && now - last_seen < PRESENT_TIMEOUT_MS;
        input->bt[player] = input->present[player] ? receiver.buttons[player] : 0;
        memcpy(input->raw[player], receiver.last_packet[player], PACKET_BYTES);
    }
}

static uint32_t script_button(char key)
{
    switch (key) {
    case 'U': return BTN_UP;
    case 'D': return BTN_DOWN;
    case 'X': return BTN_X;
    case 'Y': return BTN_Y;
    case 'L': return BTN_LEFT;
    case 'R': return BTN_RIGHT;
    case 'A': return BTN_A;
    case 'B': return BTN_B;
    case 'S': return BTN_SELECT;
    case 'T': return BTN_START;
    case 'M': return BTN_MODE;
    }

    return 0;
}

static int script_key_count(void)
{
    int key_count = 1;
    for (const char *key = sim_keys; *key; key += 1) {
        key_count += *key == ',';
    }

    return key_count;
}

/* Same semantics as scripted_key() in plat_sdl.c: change both together. */
static uint32_t simulated_key(void)
{
    static int reads; /* counts input reads (also advances with the game paused, with no new frame) */
    int read_index = reads;
    reads += 1;
    int key_index = read_index / READS_PER_KEY;
    int phase = read_index % READS_PER_KEY;
    int commas = 0;
    const char *key = sim_keys;
    while (*key && commas < key_index) {
        commas += *key == ',';
        key += 1;
    }
    if (!*key) {
        return read_index > script_key_count() * READS_PER_KEY + READS_BEFORE_QUIT ? BTN_QUIT : 0;
    }
    if (phase > LAST_PRESSED_PHASE) {
        return 0;
    }

    return script_button(*key);
}

void plat_input(input_t *input)
{
    memset(input, 0, sizeof *input);
    stick_input_poll(input, plat_ms());
    if (sim_keys) {
        input->bt[0] |= simulated_key();
        input->present[0] = 1;
    }
}
