module io_boundary_test(input clock_pin, output data_pin, inout gpio_pin,
                        inout direct_pin, output direct_out, input supply);
  wire from_clock, from_gpio, driven;
  IOPAD pad0 (.BOND(clock_pin), .SENSE(from_clock), .DRIVE(driven), .SUPPLY(supply));
  IOPAD pad1 (.BOND(data_pin), .SENSE(from_gpio), .DRIVE(driven), .SUPPLY(supply));
  IOPAD pad2 (.BOND(gpio_pin), .SENSE(from_gpio), .DRIVE(driven), .SUPPLY(supply));
  BUF u0 (.A(from_clock), .Y(driven));
  BUF u1 (.A(from_gpio), .Y(driven));
  BUF u2 (.A(direct_pin), .Y(direct_out));
endmodule
