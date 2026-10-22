// The ITP v1 wire format (IED docs/16): event types, varints, chunk framing.

pub const HELLO: u8 = 0x01;
pub const CALIBRATION: u8 = 0x02;
pub const BYE: u8 = 0x03;
pub const STRING_DEF: u8 = 0x10;
pub const SRC_LOC_DEF: u8 = 0x11;
pub const THREAD_DEF: u8 = 0x12;
pub const THREAD_NAME: u8 = 0x13;
pub const ZONE_BEGIN: u8 = 0x20;
pub const ZONE_BEGIN_NAMED: u8 = 0x21;
pub const ZONE_END: u8 = 0x22;
pub const ZONE_TEXT: u8 = 0x23;
pub const ZONE_VALUE: u8 = 0x24;
pub const TASK_BEGIN: u8 = 0x30;
pub const TASK_END: u8 = 0x31;
pub const TASK_SUSPEND: u8 = 0x32;
pub const TASK_RESUME: u8 = 0x33;
pub const FRAME_MARK: u8 = 0x40;
pub const FRAME_BEGIN: u8 = 0x41;
pub const FRAME_END: u8 = 0x42;
pub const PLOT_DEF: u8 = 0x50;
pub const PLOT_VALUE: u8 = 0x51;
pub const MESSAGE: u8 = 0x52;
pub const POOL_DEF: u8 = 0x60;
pub const ALLOC: u8 = 0x61;
pub const FREE: u8 = 0x62;
pub const POOL_RESET: u8 = 0x63;
pub const LOCK_DEF: u8 = 0x70;
pub const LOCK_WAIT: u8 = 0x71;
pub const LOCK_OBTAIN: u8 = 0x72;
pub const LOCK_RELEASE: u8 = 0x73;

pub const THREAD_CHUNK: u8 = 0;
pub const GLOBAL_CHUNK: u8 = 1;
pub const PROTOCOL_VERSION: u16 = 1;

pub fn put_varint(out: &mut Vec<u8>, mut value: u64) {
    while value >= 0x80 {
        out.push((value as u8) | 0x80);
        value >>= 7;
    }
    out.push(value as u8);
}

pub fn put_zigzag(out: &mut Vec<u8>, value: i64) {
    put_varint(out, ((value << 1) ^ (value >> 63)) as u64);
}

pub fn put_text(out: &mut Vec<u8>, text: &str) {
    put_varint(out, text.len() as u64);
    out.extend_from_slice(text.as_bytes());
}

pub fn put_double(out: &mut Vec<u8>, value: f64) {
    out.extend_from_slice(&value.to_bits().to_le_bytes());
}

/// Frames `body` as one chunk: its length, kind, thread and the time its first delta is from.
pub fn put_chunk(out: &mut Vec<u8>, kind: u8, thread: u64, base: i64, body: &[u8]) {
    let mut head = Vec::with_capacity(21);
    head.push(kind);
    put_varint(&mut head, thread);
    put_varint(&mut head, base as u64);
    out.extend_from_slice(&((head.len() + body.len()) as u32).to_le_bytes());
    out.extend_from_slice(&head);
    out.extend_from_slice(body);
}

/// The stream's first 8 bytes: the magic, the protocol version and no flags.
pub fn preamble() -> Vec<u8> {
    let mut out = b"ITP1".to_vec();
    out.extend_from_slice(&PROTOCOL_VERSION.to_le_bytes());
    out.extend_from_slice(&[0, 0]);
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn varints_take_seven_bit_groups() {
        for (value, want) in [(0u64, vec![0u8]), (127, vec![0x7f]), (128, vec![0x80, 0x01]), (u64::MAX, vec![0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x01])] {
            let mut out = Vec::new();
            put_varint(&mut out, value);
            assert_eq!(want, out, "{value}");
        }
    }

    #[test]
    fn zigzag_puts_small_magnitudes_first() {
        for (value, want) in [(0i64, 0u8), (-1, 1), (1, 2), (-2, 3), (2, 4)] {
            let mut out = Vec::new();
            put_zigzag(&mut out, value);
            assert_eq!(vec![want], out, "{value}");
        }
    }

    #[test]
    fn chunks_carry_their_length_and_header() {
        let mut out = Vec::new();
        put_chunk(&mut out, THREAD_CHUNK, 3, 300, &[ZONE_END, 2]);
        assert_eq!(vec![6, 0, 0, 0, THREAD_CHUNK, 3, 0xac, 0x02, ZONE_END, 2], out);
    }
}
