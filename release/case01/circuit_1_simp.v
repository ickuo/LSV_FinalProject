// Benchmark "top" written by ABC on Sun Dec 21 18:02:11 2025

module top ( 
    a0, a1, b1, b0, c,
    h0, h1, m0, m1  );
  input  a0, a1, b1, b0, c;
  output h0, h1, m0, m1;
  wire new_n11, new_n12, new_n13, new_n14, new_n16, new_n17, new_n18,
    new_n20;
  assign h0 = a0 & b0;
  assign new_n11 = a1 & b0;
  assign new_n12 = a0 & b1;
  assign new_n13 = new_n11 & ~new_n12;
  assign new_n14 = ~new_n11 & new_n12;
  assign h1 = new_n13 | new_n14;
  assign new_n16 = b0 & c;
  assign new_n17 = ~b1 & ~new_n16;
  assign new_n18 = b1 & new_n16;
  assign m0 = ~new_n17 & ~new_n18;
  assign new_n20 = ~b0 & ~c;
  assign m1 = ~new_n16 & ~new_n20;
endmodule


