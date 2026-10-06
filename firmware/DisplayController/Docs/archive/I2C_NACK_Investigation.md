> Archived 2026-10-06. Current state: [I2C.md](../../../Docs/I2C.md#interrupt-priority).

# I2C NACK after one byte (2026-10-05)

Until 2026-10-05 the DisplayController's I2C1 interrupt had priority 3, the
same as its refresh interrupt (about 170 us). About one host refresh in seven
then failed to reach the board on the first attempt with error `0x4` (NACK),
and the host's retry covered it.

Traced on the board: every failure was a short write of exactly one byte,
after a STOP handled late (`stopWithAddrPending`), with no bus error. The host
starts the next message about 100 us after a STOP, so a STOP handled after the
refresh interrupt could come after the host's next address had matched. The
HAL sets `CR2.NACK` when it handles a STOP, and software cannot clear it (only
an address match, a STOP or a sent NACK can), so the late NACK stayed set and
the board refused the next message's second byte.

A digital noise filter of 15 clocks made no difference. Raising the I2C
interrupt to priority 1 fixed it: 30 refreshes in a row went through on the
first attempt. The host's retry stays as a safety net.
