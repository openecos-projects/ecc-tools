module driver_test(input a, input b, inout bus, output y, output floating);
  wire multi;
  BUF u0 (.A(a), .Y(multi));
  BUF u1 (.A(b), .Y(multi));
  BUF u2 (.A(multi), .Y(y));
endmodule
