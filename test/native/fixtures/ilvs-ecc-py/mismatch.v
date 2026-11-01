// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2026 Peng Cheng Laboratory

module top (
  input IN,
  output OUT
);
  wire MID;

  BUF U0 (.A(IN), .Y(MID));
  // U1/A intentionally uses IN rather than MID.  The physical DEF stays
  // unchanged, so iLVS must report a net connectivity mismatch.
  BUF U1 (.A(IN), .Y(OUT));
endmodule
