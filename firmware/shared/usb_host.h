#pragma once
// ============================================================
//  usb_host.h  —  USB MIDI host via Pico-PIO-USB
//
//  Supports one USB MIDI device on the host port.
//  Multi-device hub support requires a newer pio_usb library.
//
//  PIO layout (RP2350 has 3 PIO blocks):
//    PIO0: USB TX   PIO1: USB RX   PIO2: Audio I2S
//
//  Hardware:
//    GP6 ── D+  (15kΩ pull-down to GND)
//    GP7 ── D-  (automatic, DP+1)
//    5V  ── VBUS    GND ── GND
//
//  Call usb_host_init() AFTER audio_driver_init() so PIO2
//  is already claimed before PIO0/PIO1 are used for USB.
// ============================================================

#include <Arduino.h>
#include <Adafruit_TinyUSB.h>
#include "config.h"
#include "pio_usb.h"
#include "midi_handler.h"      // _dispatchUsbPacket() and the midi_* callback declarations

#if ENABLE_USB_HOST

static Adafruit_USBH_Host USBHost;

inline void usb_host_init() {
    static pio_usb_configuration_t config = PIO_USB_DEFAULT_CONFIG;
    config.pin_dp = MIDI_HOST_DP_PIN;
    USBHost.configure_pio_usb(1, &config);
    USBHost.begin(1);
}

inline void usb_host_poll() {
    USBHost.task();
}

extern "C" {

void tuh_mount_cb(uint8_t daddr) {
#if ENABLE_SERIAL
    Serial.print("USB host: device mounted addr=");
    Serial.println(daddr);
#endif
}

void tuh_umount_cb(uint8_t daddr) {
#if ENABLE_SERIAL
    Serial.print("USB host: device unmounted addr=");
    Serial.println(daddr);
#endif
    // drum hits are one-shot: nothing to release when the controller goes away
}

void tuh_midi_mount_cb(uint8_t idx, const tuh_midi_mount_cb_t *cb_data) {
#if ENABLE_SERIAL
    Serial.print("MIDI mounted idx="); Serial.print(idx);
    Serial.print(" addr=");            Serial.println(cb_data->daddr);
#endif
}

void tuh_midi_umount_cb(uint8_t idx) {
#if ENABLE_SERIAL
    Serial.print("MIDI unmounted idx="); Serial.println(idx);
#endif
    // drum hits are one-shot: nothing to release when the controller goes away
}

void tuh_midi_rx_cb(uint8_t idx, uint32_t xferred_bytes) {
    (void)xferred_bytes;
    if (!tuh_midi_mounted(idx)) return;
    uint8_t packet[4];
    while (tuh_midi_packet_read(idx, packet)) {
        _dispatchUsbPacket(packet);       // the same parser as the USB-device port (midi_handler.h, host-tested): channel
    }                                     // filter, notes, CCs, bend, program change and the real-time messages
}

void tuh_midi_tx_cb(uint8_t idx, uint32_t xferred_bytes) {
    (void)idx; (void)xferred_bytes;
}

} // extern "C"

#else

inline void usb_host_init() {}
inline void usb_host_poll() {}

#endif // ENABLE_USB_HOST