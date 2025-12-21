// Benchmark "top" written by ABC on Sun Dec 21 18:03:02 2025

module top ( 
    x0, x1, y0, y1, z,
    u0, u1, w0, w1  );
  input  x0, x1, y0, y1, z;
  output u0, u1, w0, w1;
  wire new_n11, new_n12, new_n13, new_n14, new_n16, new_n17, new_n18,
    new_n20;
  assign u0 = x0 & y0;
  assign new_n11 = x1 & y0;
  assign new_n12 = x0 & y1;
  assign new_n13 = ~new_n11 & new_n12;
  assign new_n14 = new_n11 & ~new_n12;
  assign u1 = new_n13 | new_n14;
  assign new_n16 = y0 & z;
  assign new_n17 = ~y1 & ~new_n16;
  assign new_n18 = y1 & new_n16;
  assign w0 = ~new_n17 & ~new_n18;
  assign new_n20 = ~y0 & ~z;
  assign w1 = ~new_n16 & ~new_n20;
endmodule


