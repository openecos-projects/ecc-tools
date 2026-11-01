// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2026 Peng Cheng Laboratory

module top (
  input IN,
  output OUT
);
  wire MID;

  BUF U0 (.A(IN), .Y(MID));
  BUF U1 (.A(MID), .Y(OUT));
endmodule
