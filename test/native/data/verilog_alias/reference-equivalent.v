// SPDX-License-Identifier: MulanPSL-2.0

module top (
  input IN,
  input BAD,
  output OUT
);
  wire net16;

  BUF U0 (.A(IN), .Y(net16));
  assign OUT = net16;
endmodule
