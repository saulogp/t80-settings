// SPDX-License-Identifier: GPL-2.0-only
/*
 * HID-BPF fix for the Thrustmaster T80 racing wheel in PS3 mode (044f:b667).
 *
 * In PS3 mode the wheel uses a PS3-controller style report descriptor. The
 * analog pedal values are sent in the "pressure" bytes of the L2/R2 buttons,
 * which are declared on the vendor usage page 0xff00 (usages 0x2a and 0x2b).
 * hid-input ignores that page, so on Linux the pedals only show up as the
 * digital BTN_TL/BTN_TR buttons and the analog travel is lost.
 *
 * This program rewrites the descriptor so those two bytes are declared as
 * Generic Desktop Rx (left pedal, brake) and Ry (right pedal, accelerator).
 * The report data itself is not touched; only how it is described.
 *
 * Original report descriptor (137 bytes), 27-byte input report, no report ID:
 *
 * 0x05, 0x01,              // Usage Page (Generic Desktop)        0
 * 0x09, 0x05,              // Usage (Game Pad)                    2
 * 0xa1, 0x01,              // Collection (Application)            4
 * 0x15, 0x00,              //  Logical Minimum (0)                6
 * 0x25, 0x01,              //  Logical Maximum (1)                8
 * 0x35, 0x00,              //  Physical Minimum (0)               10
 * 0x45, 0x01,              //  Physical Maximum (1)               12
 * 0x75, 0x01,              //  Report Size (1)                    14
 * 0x95, 0x0d,              //  Report Count (13)                  16
 * 0x05, 0x09,              //  Usage Page (Button)                18
 * 0x19, 0x01,              //  Usage Minimum (1)                  20
 * 0x29, 0x0d,              //  Usage Maximum (13)                 22
 * 0x81, 0x02,              //  Input (Data,Var,Abs)               24  bytes 0-1
 * 0x95, 0x03,              //  Report Count (3)                   26
 * 0x81, 0x01,              //  Input (Cnst,Arr,Abs)               28
 * 0x05, 0x01,              //  Usage Page (Generic Desktop)       30
 * 0x25, 0x07,              //  Logical Maximum (7)                32
 * 0x46, 0x3b, 0x01,        //  Physical Maximum (315)             34
 * 0x75, 0x04,              //  Report Size (4)                    37
 * 0x95, 0x01,              //  Report Count (1)                   39
 * 0x65, 0x14,              //  Unit (EnglishRotation: deg)        41
 * 0x09, 0x39,              //  Usage (Hat switch)                 43
 * 0x81, 0x42,              //  Input (Data,Var,Abs,Null)          45  byte 2
 * 0x65, 0x00,              //  Unit (None)                        47
 * 0x95, 0x01,              //  Report Count (1)                   49
 * 0x81, 0x01,              //  Input (Cnst,Arr,Abs)               51
 * 0x26, 0xff, 0x00,        //  Logical Maximum (255)              53
 * 0x46, 0xff, 0x00,        //  Physical Maximum (255)             56
 * 0x09, 0x30,              //  Usage (X)          steering        59
 * 0x09, 0x31,              //  Usage (Y)                          61
 * 0x09, 0x32,              //  Usage (Z)                          63
 * 0x09, 0x35,              //  Usage (Rz)                         65
 * 0x75, 0x08,              //  Report Size (8)                    67
 * 0x95, 0x04,              //  Report Count (4)                   69
 * 0x81, 0x02,              //  Input (Data,Var,Abs)               71  bytes 3-6
 * 0x06, 0x00, 0xff,        //  Usage Page (Vendor Defined 0xff00) 73
 * 0x09, 0x20, ... 0x09, 0x29, // Usage (0x20) .. Usage (0x29)     76
 * 0x09, 0x2a,              //  Usage (0x2a)       left pedal      96
 * 0x09, 0x2b,              //  Usage (0x2b)       right pedal     98
 * 0x95, 0x0c,              //  Report Count (12)                  100
 * 0x81, 0x02,              //  Input (Data,Var,Abs)               102 bytes 7-18
 * 0x0a, 0x21, 0x26,        //  Usage (0x2621)                     104
 * 0x95, 0x08,              //  Report Count (8)                   107
 * 0xb1, 0x02,              //  Feature (Data,Var,Abs)             109
 * 0x0a, 0x21, 0x26,        //  Usage (0x2621)                     111
 * 0x91, 0x02,              //  Output (Data,Var,Abs)              114
 * 0x26, 0xff, 0x03,        //  Logical Maximum (1023)             116
 * 0x46, 0xff, 0x03,        //  Physical Maximum (1023)            119
 * 0x09, 0x2c, 0x09, 0x2d, 0x09, 0x2e, 0x09, 0x2f, //               122
 * 0x75, 0x10,              //  Report Size (16)                   130
 * 0x95, 0x04,              //  Report Count (4)                   132
 * 0x81, 0x02,              //  Input (Data,Var,Abs)               134 bytes 19-26
 * 0xc0,                    // End Collection                      136
 */

#include "vmlinux.h"
#include "hid_bpf.h"
#include "hid_bpf_helpers.h"
#include <bpf/bpf_tracing.h>

#define VID_THRUSTMASTER 0x044F
#define PID_T80_PS3      0xB667

#define ORIG_RDESC_SIZE  137

HID_BPF_CONFIG(
	HID_DEVICE(BUS_USB, HID_GROUP_GENERIC, VID_THRUSTMASTER, PID_T80_PS3)
);

static const __u8 fixed_rdesc[] = {
	0x05, 0x01,              // Usage Page (Generic Desktop)
	0x09, 0x05,              // Usage (Game Pad)
	0xa1, 0x01,              // Collection (Application)
	0x15, 0x00,              //  Logical Minimum (0)
	0x25, 0x01,              //  Logical Maximum (1)
	0x35, 0x00,              //  Physical Minimum (0)
	0x45, 0x01,              //  Physical Maximum (1)
	0x75, 0x01,              //  Report Size (1)
	0x95, 0x0d,              //  Report Count (13)
	0x05, 0x09,              //  Usage Page (Button)
	0x19, 0x01,              //  Usage Minimum (1)
	0x29, 0x0d,              //  Usage Maximum (13)
	0x81, 0x02,              //  Input (Data,Var,Abs)
	0x95, 0x03,              //  Report Count (3)
	0x81, 0x01,              //  Input (Cnst,Arr,Abs)
	0x05, 0x01,              //  Usage Page (Generic Desktop)
	0x25, 0x07,              //  Logical Maximum (7)
	0x46, 0x3b, 0x01,        //  Physical Maximum (315)
	0x75, 0x04,              //  Report Size (4)
	0x95, 0x01,              //  Report Count (1)
	0x65, 0x14,              //  Unit (EnglishRotation: deg)
	0x09, 0x39,              //  Usage (Hat switch)
	0x81, 0x42,              //  Input (Data,Var,Abs,Null)
	0x65, 0x00,              //  Unit (None)
	0x95, 0x01,              //  Report Count (1)
	0x81, 0x01,              //  Input (Cnst,Arr,Abs)
	0x26, 0xff, 0x00,        //  Logical Maximum (255)
	0x46, 0xff, 0x00,        //  Physical Maximum (255)
	0x09, 0x30,              //  Usage (X)
	0x09, 0x31,              //  Usage (Y)
	0x09, 0x32,              //  Usage (Z)
	0x09, 0x35,              //  Usage (Rz)
	0x75, 0x08,              //  Report Size (8)
	0x95, 0x04,              //  Report Count (4)
	0x81, 0x02,              //  Input (Data,Var,Abs)
	/* --- changed part starts here --- */
	0x06, 0x00, 0xff,        //  Usage Page (Vendor Defined 0xff00)
	0x09, 0x20,              //  Usage (0x20)
	0x09, 0x21,              //  Usage (0x21)
	0x09, 0x22,              //  Usage (0x22)
	0x09, 0x23,              //  Usage (0x23)
	0x09, 0x24,              //  Usage (0x24)
	0x09, 0x25,              //  Usage (0x25)
	0x09, 0x26,              //  Usage (0x26)
	0x09, 0x27,              //  Usage (0x27)
	0x09, 0x28,              //  Usage (0x28)
	0x09, 0x29,              //  Usage (0x29)
	0x95, 0x0a,              //  Report Count (10)        was 12
	0x81, 0x02,              //  Input (Data,Var,Abs)     bytes 7-16
	0x05, 0x01,              //  Usage Page (Generic Desktop)
	0x09, 0x33,              //  Usage (Rx)               left pedal (brake)
	0x09, 0x34,              //  Usage (Ry)               right pedal (accelerator)
	0x95, 0x02,              //  Report Count (2)
	0x81, 0x02,              //  Input (Data,Var,Abs)     bytes 17-18
	0x06, 0x00, 0xff,        //  Usage Page (Vendor Defined 0xff00), restored
	/* --- changed part ends here --- */
	0x0a, 0x21, 0x26,        //  Usage (0x2621)
	0x95, 0x08,              //  Report Count (8)
	0xb1, 0x02,              //  Feature (Data,Var,Abs)
	0x0a, 0x21, 0x26,        //  Usage (0x2621)
	0x91, 0x02,              //  Output (Data,Var,Abs)
	0x26, 0xff, 0x03,        //  Logical Maximum (1023)
	0x46, 0xff, 0x03,        //  Physical Maximum (1023)
	0x09, 0x2c,              //  Usage (0x2c)
	0x09, 0x2d,              //  Usage (0x2d)
	0x09, 0x2e,              //  Usage (0x2e)
	0x09, 0x2f,              //  Usage (0x2f)
	0x75, 0x10,              //  Report Size (16)
	0x95, 0x04,              //  Report Count (4)
	0x81, 0x02,              //  Input (Data,Var,Abs)
	0xc0,                    // End Collection
};

/* Bytes that must match the original descriptor for us to touch it. */
static const __u8 pedal_block[] = {
	0x09, 0x2a, 0x09, 0x2b,  // Usage (0x2a), Usage (0x2b)
	0x95, 0x0c, 0x81, 0x02,  // Report Count (12), Input (Data,Var,Abs)
};
#define PEDAL_BLOCK_OFFSET 96

static __always_inline bool is_t80_ps3_rdesc(const __u8 *rdesc, unsigned int size)
{
	if (size != ORIG_RDESC_SIZE)
		return false;
	if (rdesc[0] != 0x05 || rdesc[1] != 0x01 || rdesc[2] != 0x09 || rdesc[3] != 0x05)
		return false; /* Generic Desktop / Game Pad */
	if (rdesc[73] != 0x06 || rdesc[74] != 0x00 || rdesc[75] != 0xff)
		return false; /* Vendor usage page */
	if (__builtin_memcmp(rdesc + PEDAL_BLOCK_OFFSET, pedal_block, sizeof(pedal_block)))
		return false;
	return rdesc[ORIG_RDESC_SIZE - 1] == 0xc0;
}

SEC(HID_BPF_RDESC_FIXUP)
int BPF_PROG(hid_fix_rdesc_t80, struct hid_bpf_ctx *hctx)
{
	__u8 *data = hid_bpf_get_data(hctx, 0 /* offset */, HID_MAX_DESCRIPTOR_SIZE /* size */);

	if (!data)
		return 0; /* EPERM */

	/* Leave anything unexpected alone (other firmware, already fixed, ...) */
	if (!is_t80_ps3_rdesc(data, hctx->size))
		return 0;

	__builtin_memcpy(data, fixed_rdesc, sizeof(fixed_rdesc));
	return sizeof(fixed_rdesc);
}

HID_BPF_OPS(thrustmaster_t80) = {
	.hid_rdesc_fixup = (void *)hid_fix_rdesc_t80,
};

SEC("syscall")
int probe(struct hid_bpf_probe_args *ctx)
{
	if (is_t80_ps3_rdesc(ctx->rdesc, ctx->rdesc_size))
		ctx->retval = 0;
	else
		ctx->retval = -EINVAL;

	return 0;
}

char _license[] SEC("license") = "GPL";
