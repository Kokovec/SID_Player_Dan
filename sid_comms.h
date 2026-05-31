#ifndef SID_COMMS_H
#define SID_COMMS_H

#include <stdint.h>

// -----------------------------------------------------------------------
// Inter-Pico PIO UART protocol  (Player Pico side)
//   Player  GP21 (PIO TX) ──► Display GPIO 1 (RX)
//   Player  GP22 (PIO RX) ◄── Display GPIO 0 (TX)
//   Common GND
// -----------------------------------------------------------------------

#define SID_BAUD_RATE   115200

// Packet framing
#define PKT_HDR0        0xAA
#define PKT_HDR1        0x55

// Packet types
#define PKT_TYPE_META   0x01    // Player → Display : song metadata
#define PKT_TYPE_CMD    0x02    // Display → Player : command

// Commands (single-byte payload in a CMD packet)
#define CMD_PLAY        0x01    // play current song
#define CMD_NEXT        0x02    // advance to next song, send metadata, wait
#define CMD_PREV        0x04    // go to previous song, send metadata, wait
#define CMD_STOP        0x05    // stop playback, stay on current song, wait

// -----------------------------------------------------------------------
// Metadata payload  (Player → Display)
// Sent on boot and after every song change.
// All strings are null-terminated, fixed-width.
// -----------------------------------------------------------------------
#define SID_TITLE_LEN       32
#define SID_AUTHOR_LEN      32
#define SID_RELEASED_LEN    32

typedef struct __attribute__((packed)) {
    uint8_t song_num;                   // current song (1-based)
    uint8_t song_count;                 // total songs in the SID file
    char    title   [SID_TITLE_LEN];
    char    author  [SID_AUTHOR_LEN];
    char    released[SID_RELEASED_LEN];
} SidMeta;

// -----------------------------------------------------------------------
// Packet wire format:
//
//  [HDR0][HDR1][type][len][ ...payload (len bytes)... ][checksum]
//
//  checksum = XOR of type, len, and every payload byte
// -----------------------------------------------------------------------

#endif // SID_COMMS_H
