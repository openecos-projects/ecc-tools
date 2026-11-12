module power_test(input a, inout rail, input VDD, inout VSS, output y0, output y1, output y2);
  supply1 high_supply;
  supply0 low_supply;
  BUF u0 (.A(a), .Y(y0), .VPWR(rail));
  BUF u1 (.A(high_supply), .Y(y1), .VPWR(rail));
  BUF u2 (.A(low_supply), .Y(y2), .VPWR(rail));
  BUF u3 (.A(VDD), .Y(y0), .VPWR(rail));
  BUF u4 (.A(VSS), .Y(y2), .VPWR(rail));
endmodule
